/*
 * Buzzer.c - 无源蜂鸣器驱动
 *
 * 真值来源：v3.1 Driver/Buzzer.c 的 PWM_config() / Buzzer_play() / Buzzer_stop()
 *          （寄存器配置与引脚切换逐字对齐，只改必要参数）
 *
 * 相对 v3.1 的两处规范化（按规范第三节，偏离必须显式列出理由）：
 *
 *   1) 占空比算式去掉浮点。
 *      v3.1:  PWMx_InitStructure.PWM_Duty = (u16)(period * 0.02f);
 *             Buzzer_play(): u16 duty = (u16)(period * 0.02f);
 *      问题：只要算式里出现 float，C51 就会把整个浮点库 C51FPL.LIB 链进来。
 *            现有工程实测已链入 ?C?FPMUL / ?C?FPDIV / ?C?FPCONVERT。
 *      改法：period / 50 等价于 period * 2%，纯整数。
 *
 *   2) 换音时先关输出。
 *      v3.1 直接改周期寄存器（输出仍开着），《04-M1分工规格》G 角色注意事项写的是
 *      "换音符时先关掉输出、改频率、再打开，否则会听到'啪'的爆音"。
 *
 * 另外本工程把占空比与"音量 0..10"挂钩（《04》G 角色：50% 最响），见 BUZZER_DUTY_MAX_PCT。
 */
#include "Buzzer.h"
#include "GPIO.h"
#include "Switch.h"
#include "STC8H_PWM.h"
#include "NVIC.h"

/* 音阶频率表，照抄 v3.1 Driver/Buzzer.c 的 FREQS[] */
u16 code FREQS[BUZZER_TONE_MAX] =
{
    523,  587,  659,  698,  784,  880,  988,     /* 低八度 1-7 */
    1047, 1175, 1319, 1397, 1568, 1760, 1976,    /* 中八度 1-7 */
    2093, 2349, 2637, 2794, 3136, 3520, 3951     /* 高八度 1-7 */
};

void Buzzer_SafeLevel(void)
{
    /* 无源蜂鸣器要靠 PWM 才发声，静态电平不会响；
     * 但输出寄存器默认是 1，为了避免上电瞬间的电平不确定性，显式拉低（照抄江文聪 Buzzer.c 的 P00 = 0） */
    BUZZER = 0;
}

/* 配置 PWM5 + PWMB 公共寄存器（照抄 v3.1，仅占空比算式改整数） */
static void Buzzer_PWM_Config(void)
{
    PWMx_InitDefine PWMx_InitStructure;
    u16 period = MAIN_Fosc / 1000;      /* 初始 1kHz */

    /* 配置 PWM5 通道 */
    PWMx_InitStructure.PWM_Mode      = CCMRn_PWM_MODE1;
    PWMx_InitStructure.PWM_Duty      = period / 50;         /* 2%，等价 period * 0.02f */
    PWMx_InitStructure.PWM_EnoSelect = ENO5P;
    PWM_Configuration(PWM5, &PWMx_InitStructure);

    /* 配置 PWMB 公共寄存器 */
    PWMx_InitStructure.PWM_Period        = period - 1;
    PWMx_InitStructure.PWM_DeadTime      = 0;
    PWMx_InitStructure.PWM_MainOutEnable = ENABLE;
    PWMx_InitStructure.PWM_CEN_Enable    = ENABLE;
    PWM_Configuration(PWMB, &PWMx_InitStructure);

    /* 把 PWM5 通道切到 P0.0 */
    PWM5_SW(PWM5_SW_P00);

    /* PWMB 中断不用 */
    NVIC_PWM_Init(PWMB, DISABLE, Priority_0);

    /* 关闭 PWM5 输出，上电保持静音 */
    PWMB_CC5E_Disable();
}

void Buzzer_Init(void)
{
    EAXSFR();                   /* 扩展寄存器访问使能 */
    Buzzer_SafeLevel();
    P0_MODE_OUT_PP(GPIO_Pin_0);
    Buzzer_SafeLevel();
    Buzzer_PWM_Config();
}

void Buzzer_Play(u16 hz_value, u8 volume)
{
#if !BUZZER_ENABLE
    /* 总开关关闭：直接静音，不做任何 PWM 操作。
     * 整个函数体用 #if 包起来（而不是提前 return），
     * 否则编译器会把后面的代码判成 unreachable 并报 warning C294。 */
    hz_value = hz_value;
    volume   = volume;
    Buzzer_Stop();
#else
    u16 period;
    u16 duty;

    if (hz_value == 0 || volume == 0)
    {
        Buzzer_Stop();
        return;
    }

    period = (u16)(MAIN_Fosc / hz_value);       /* 频率过低时周期会溢出 16 位，故限制下限 */

    if (volume > BUZZER_VOLUME_FULL)
    {
        volume = BUZZER_VOLUME_FULL;
    }

    /* 音量 0..10 -> 占空比 0..BUZZER_DUTY_MAX_PCT%
     * 纯整数：用 u32 中转，避免 16 位乘法溢出 */
    duty = (u16)(((u32)period * BUZZER_DUTY_MAX_PCT * volume) / (100UL * BUZZER_VOLUME_FULL));

    /* 占空比不得超过周期 */
    if (duty >= period)
    {
        duty = period - 1;
    }

    /* 1) 先关输出，避免换音爆音（《04》G 角色注意事项） */
    PWMB_CC5E_Disable();

    /* 2) 改周期 = 改音高（照抄 v3.1 Buzzer_play） */
    PWMB_AutoReload((u16)(period - 1));

    /* 3) 改占空比 = 改响度 */
    PWMB_Duty5(duty);

    /* 4) 再打开输出 */
    PWMB_CC5E_Enable();
#endif      /* BUZZER_ENABLE */
}

u8 Buzzer_IsEnabled(void)
{
    return (u8)BUZZER_ENABLE;
}

void Buzzer_Stop(void)
{
    PWMB_CC5E_Disable();
    BUZZER = 0;
}

u16 Buzzer_Freq(u8 tone)
{
    if (tone < 1 || tone > BUZZER_TONE_MAX)
    {
        return 0;
    }

    return FREQS[tone - 1];
}
