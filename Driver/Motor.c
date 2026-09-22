/*
 * Motor.c - 震动马达驱动
 *
 * 真值来源：v3.1 App/App_Motor.c（真机跑通的 PWM6 配置，逐字对齐）
 *
 * 与蜂鸣器的关系：蜂鸣器在 PWM5、马达在 PWM6，两者同属 PWMB 组，
 *                共用一个周期寄存器 PWMB_ARR。所以：
 *                  放音乐时 -> 马达必须闭嘴（关闭 CC6E，占空比设 0）
 *                  单独震动时 -> 把 PWMB_ARR 改成 1kHz
 *                这是《02-技术方案》3.2 明确写下的取舍。
 */
#include "Motor.h"
#include <stdio.h>
#include "GPIO.h"
#include "Switch.h"         /* PWM6_SW / PWM6_SW_P01 在这里 */
#include "NVIC.h"           /* NVIC_PWM_Init 在这里 */
#include "Buzzer.h"

static u8 s_motorOn = 0;
static PWMx_Duty s_duty;

/* 【第 7 点】定时震动的计时状态。
 * 用"数 5ms 拍数"而不是记时间戳，是为了让驱动层不依赖 App 层的 SysTick。 */
static u8  s_vibrating = 0;
static u16 s_vibTicks  = 0;

void Motor_SafeLevel(void)
{
    /* 高电平震 -> 写 0 = 不震。此时端口还是准双向口，写 0 能可靠拉低。 */
    MOTOR = 0;
}

void Motor_Init(void)
{
    PWMx_InitDefine PWMx_InitStructure;

    /* 0) 先把引脚按到安全电平，再改端口模式 */
    Motor_SafeLevel();

    /* 1) P0.1 推挽输出（照抄 v3.1 App_LED.c 对 GPIO 的处理方式，PWM 输出需要强驱动） */
    P0_MODE_OUT_PP(GPIO_Pin_1);
    MOTOR = 0;

    /* 2) 配置 PWM6，占空比 0（照抄 v3.1 App_Motor.c 的 PWM_config()） */
    PWMx_InitStructure.PWM_Mode       = CCMRn_PWM_MODE1;
    PWMx_InitStructure.PWM_Duty       = 0;
    PWMx_InitStructure.PWM_EnoSelect  = ENO6P;
    PWM_Configuration(PWM6, &PWMx_InitStructure);

    /* 3) 配置 PWMB 公共寄存器：周期 1kHz、主输出使能、计数器使能 */
    PWMx_InitStructure.PWM_Period        = MOTOR_PERIOD - 1;
    PWMx_InitStructure.PWM_DeadTime      = 0;
    PWMx_InitStructure.PWM_MainOutEnable = ENABLE;
    PWMx_InitStructure.PWM_CEN_Enable    = ENABLE;
    PWM_Configuration(PWMB, &PWMx_InitStructure);

    /* 4) 把 PWM6 通道切到 P0.1 */
    PWM6_SW(PWM6_SW_P01);

    /* 5) PWMB 中断不用 */
    NVIC_PWM_Init(PWMB, DISABLE, Priority_0);

    /* 6) 确保输出关闭，马达静止 */
    PWMB_CC6E_Disable();
    MOTOR = 0;
    s_motorOn = 0;
}

void Motor_On(u8 level)
{
    /* 【2026-09-19 修】duty 的声明必须放在函数最前面 —— C51 是严格 C89，
     * 声明不能出现在语句之后（error C141: syntax error near 'u16'）。
     * 这个坑之前一直没暴露，因为 MOTOR_ENABLE = 0 时 #else 分支根本没被编译过；
     * 第 7 点把总开关打开后才报出来。 */
    u16 duty;


    /* 【第 7 点】显式调用 = 取消"定时自动停"。
     * 否则闹铃正在震动时，之前某次 Motor_Vibrate 的计时到点会把闹铃一起关掉。 */
    s_vibrating = 0;
    s_vibTicks  = 0;

    if (level > 100)
    {
        level = 100;
    }

#if !MOTOR_ENABLE
    /* 总开关关闭：不驱动马达，直接返回（并确保输出是关的） */
    duty = 0;
    Buzzer_Stop();
    PWMB_CC6E_Disable();
    MOTOR = 0;
    s_motorOn = 0;
    return;
#else
    /* 蜂鸣器与马达共用 PWMB 频率，先让蜂鸣器闭嘴（《02》3.2） */
    Buzzer_Stop();

    /* 改成马达自己的频率 1kHz */
    PWMB_AutoReload((u16)(MOTOR_PERIOD - 1));

    /* 【第 7 点】占空比由**震动强度**决定，不再写死 50%。
     *   duty = MOTOR_PERIOD * level / 100
     * 用 u32 中间量：MOTOR_PERIOD(24000) * 100 = 2.4e6 已超 u16 的 65535。
     * level = 50 时结果正好是 PERIOD/2，和改之前完全一致 —— 默认值下行为不变。 */
    duty = (u16)(((u32)MOTOR_PERIOD * (u32)level) / 100U);

    /* 满档时 duty 会等于 PERIOD（恒高），钳一下留一个计数余量 */
    if (duty >= (u16)MOTOR_PERIOD)
    {
        duty = (u16)(MOTOR_PERIOD - 1);
    }

    s_duty.PWM6_Duty = duty;
    UpdatePwm(PWM6, &s_duty);

    PWMB_CC6E_Enable();
    s_motorOn = 1;
#endif  /* MOTOR_ENABLE */
}

void Motor_Off(void)
{
    /* 显式关 = 取消"定时自动停"，避免计时到点又去关一次别的用途 */
    s_vibrating = 0;
    s_vibTicks  = 0;

    PWMB_CC6E_Disable();
    MOTOR = 0;
    s_motorOn = 0;
}

/*------------------------------------------------------------------------
 * 震动 ms 毫秒后自动停（非阻塞）—— 用户要的"100ms 确认反馈"
 *------------------------------------------------------------------------*/
void Motor_Vibrate(u16 ms, u8 level)
{
    /* 【反馈用途的最低档】强度 0 时也给一点，否则用户按了确认**完全没有感觉**
     * ——和 Music_Beep 里"音量 0 时也给一个最低可闻档"是同一个道理
     * （见 App_Music.c 的 Music_Beep）。
     *
     * 只在这里兜底：Motor_On(0) 仍然是"不震"，
     * 那是闹铃"震动强度 0%"的正常语义，不能改。 */
    if (level == 0)
    {
        level = 10;
    }

    Motor_On(level);            /* 里面会把 s_vibrating 清 0 */

    /* 换算成 5ms 拍数。ms = 0 时也至少给 1 拍，
     * 否则"开了又立刻关"会让马达一下都不动（用户会以为没反应）。 */
    s_vibTicks = (u16)((ms + 4U) / 5U);
    if (s_vibTicks == 0)
    {
        s_vibTicks = 1;
    }

    s_vibrating = 1;
}

void Motor_Tick(void)
{
    if (!s_vibrating)
    {
        return;
    }

    if (s_vibTicks > 0)
    {
        s_vibTicks--;
    }

    if (s_vibTicks == 0)
    {
        s_vibrating = 0;
        Motor_Off();
    }
}

u8 Motor_IsOn(void)
{
    return s_motorOn;
}
