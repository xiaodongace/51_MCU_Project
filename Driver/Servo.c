/*
 * Servo.c - 舵机驱动（P2.5 = PWM3N，PWMA 组，50Hz）
 * 真值与取舍理由见 Servo.h 顶部注释。
 */
#include "Servo.h"
#include "GPIO.h"
#include "Switch.h"
#include "NVIC.h"
#include "STC8H_PWM.h"

/* 50Hz：PWM_Hz = SYSclk / (PSCR+1) / (ARR+1)
 *   24MHz / 10 / 50 = 48000  =>  PRESCALER = 10、PERIOD = 48000
 * 参考工程注释原文：「确保分母 PRESCALER * FREQ >= 367 即可以正确输出」。 */
#define SERVO_PRESCALER   10
#define SERVO_FREQ        50
#define SERVO_PERIOD      (MAIN_Fosc / SERVO_PRESCALER / SERVO_FREQ)

/* 舵机脉宽：0° -> 500us，180° -> 2500us */
#define SERVO_PULSE_MIN   500
#define SERVO_PULSE_MAX   2500
#define SERVO_FRAME_US    20000     /* 20ms 一帧 = 50Hz */

#define SERVO_STEP        5         /* 每按一次键走几度 */

static u8  s_inited = 0;
static u8  s_angle  = 90;           /* 默认中位 */
static PWMx_Duty s_duty;

/* 把配置结构体的**每一个字段**都显式填上再交给库函数。
 * 规范 §三.11：局部结构体没赋值的字段 = 内存垃圾，会被库函数原样写进寄存器，
 * 而且它的值随代码布局变化（"以前好用、改完就坏"）。这里一次填满，杜绝该类问题。 */
static void servo_fill_cfg(PWMx_InitDefine *c, u16 period, u16 duty, u8 eno)
{
    c->PWM_Mode          = CCMRn_PWM_MODE1;
    c->PWM_Period        = period;
    c->PWM_Duty          = duty;
    c->PWM_DeadTime      = 0;
    c->PWM_EnoSelect     = eno;
    c->PWM_CEN_Enable    = ENABLE;
    c->PWM_MainOutEnable = ENABLE;
}

/*------------------------------------------------------------------------
 * 上电安全电平 —— 必须在 sys_init() 最前面调（和 Motor_SafeLevel 同一批）
 *
 * 【用户 2026-09-19 报的现象与判断】
 *   "没进入 4 云台任务时容易触发舵机旋转；进过任务再退出就不触发了。"
 * -> 完全正确：复位后 P2 的输出锁存器是 0xFF（全高），而 P2.5 默认是**准双向口**
 *   （弱上拉），一直没被配置过 —— 舵机信号线"一直是高"相当于一个超长脉宽，
 *   舵机会直接甩到极限位置。
 *   而 Servo_Off() 里那句拉低，只在"进过一次云台又退出"之后才生效，
 *   所以那种情况下反而正常。
 *
 * 顺序照规范：**先写值（此时还是准双向口，写 0 能可靠拉低），再配端口模式**。
 *------------------------------------------------------------------------*/
void Servo_SafeLevel(void)
{
    P2 &= (u8)(~0x20);              /* P2.5 = 0：先把输出锁存器按到低 */
    P2_MODE_OUT_PP(GPIO_Pin_5);     /* 再配成推挽，保证是"强驱动低电平" */
}

/* 角度 -> 占空比寄存器的值。抽出来是为了让 Servo_Init 也能直接算，
 * 不必"先写 0、再改一次"（见下面 Init 里的说明）。 */
static u16 servo_duty_of(u8 angle)
{
    u16 pulseUs;

    if (angle > 180)
    {
        angle = 180;
    }

    /* 脉宽 = 500 + angle * 2000 / 180   （必须用 u32：180*2000=360000 超 u16） */
    pulseUs = (u16)(SERVO_PULSE_MIN +
                    (u16)(((u32)angle * (SERVO_PULSE_MAX - SERVO_PULSE_MIN)) / 180U));

    /* Duty = PERIOD * pulseUs / 20000   （一帧 20ms = 20000us） */
    return (u16)(((u32)SERVO_PERIOD * (u32)pulseUs) / SERVO_FRAME_US);
}

void Servo_Init(void)
{
    PWMx_InitDefine cfg;

    /* 【2026-09-19 修"第二次进云台就没反应、逻辑分析仪也没波形"】
     *
     * 原来这里用 s_inited 做了"只初始化一次"的短路 —— 整块 PWM 配置被跳过。
     * 但 Servo_Off() 会把 PWM3N 的输出关掉，于是第二次进云台时输出再也开不回来。
     * 真机现象完全吻合：
     *   第一次进云台：能调角度、P2.5 有 50Hz 波形；
     *   退出再进：调不动、P2.5 没有波形。
     * 串口日志也对得上：[SERVO] init 只出现 1 次，[SERVO] off 出现 4 次。
     *
     * 参考工程 v3.1 的做法（用户指出的）：它把每个外设做成独立任务，
     *   进业务 -> os_create_task(TASK_Motor)，任务体第一件事就是 GPIO_config + PWM_config，
     *             **每次都完整配置一遍**（其中 PWM_Configuration(PWM6,...,ENO6P) 会把输出重新使能）；
     *   离业务 -> TASK_MOTOR_reset 里 PWMB_CC6E_Disable() + os_delete_task()。
     *
     * 本工程的任务是按职责分的（不按外设），没有"任务体重跑"这个时机，
     * 所以把同一件事挂在 App_Menu.c 的 menu_task_enter() 上 —— 本函数被它调用。
     * **原则照做：进入 = 完整初始化，不依赖上一次留下的任何状态。** */

    P2_MODE_OUT_PP(GPIO_Pin_5);         /* P2.5 = PWM3N，推挽输出 */
    PWM3_USE_P24P25();                  /* 通道 3 输出脚切到 P2.4 / P2.5（只用 N 那一半）*/
    PWMA_Prescaler(SERVO_PRESCALER - 1);

    /* 先配组（周期 50Hz）、再配通道 —— 保证通道使能那一刻占空比还是 0，
     * 不会在启动瞬间甩出一个大脉宽让舵机猛跳。 */
    /* 【2026-09-19 修"调整角度时舵机抖一下、回一下再转过去"】
     *
     * 原来这里是"先配成占空比 0、再单独调 Servo_SetAngle 设成真实角度"。
     * 两次之间有一段 **占空比 = 0** 的窗口 —— 对舵机来说就是"信号线上没有脉冲"，
     * 有些舵机会在这时候回中或者抖一下，然后才收到真正的新角度。
     *
     * 现在把目标占空比**直接算好、一次配置到位**，中间没有"无脉冲"的空档。
     * 组配置(PWMA)其实不读 duty，这里一起填上只是为了让结构体没有未赋值字段
     * （规范 §三.11：传给库函数的结构体每个字段都要显式赋值）。 */
    servo_fill_cfg(&cfg, (u16)(SERVO_PERIOD - 1), servo_duty_of(s_angle), 0);
    PWM_Configuration(PWMA, &cfg);

    /* 通道配置：直接带上目标占空比 + 使能 ENO3N（P2.4 留给超声波 TRIG）。
     * 这一句既是"重新使能输出"（第二次进云台能恢复的关键），
     * 也避免了"先 0 再改"那段无脉冲窗口。 */
    servo_fill_cfg(&cfg, (u16)(SERVO_PERIOD - 1), servo_duty_of(s_angle), ENO3N);
    PWM_Configuration(PWM3, &cfg);

    NVIC_PWM_Init(PWMA, DISABLE, Priority_0);

    /* 占空比结构体每个字段都清零，之后只改 PWM3_Duty */
    s_duty.PWM1_Duty = 0;
    s_duty.PWM2_Duty = 0;
    s_duty.PWM3_Duty = 0;
    s_duty.PWM4_Duty = 0;
    s_duty.PWM5_Duty = 0;
    s_duty.PWM6_Duty = 0;
    s_duty.PWM7_Duty = 0;
    s_duty.PWM8_Duty = 0;

    s_inited = 1;


    Servo_SetAngle(s_angle);
}

void Servo_Off(void)
{
    /* 照 demo30 的 TASK_MOTOR_reset() 的做法：先关掉通道的比较输出
     * （它那里是 PWMB_CC6E_Disable()，这里对应 PWM3 的 N 通道 CC3NE），
     * 再把引脚的输出使能也清掉 —— 双保险，之后 P2.5 就彻底安静了，
     * 舵机收不到脉冲也不再受力，不会抖。
     *
     * 下一次 Servo_Init() 会把这两处一起重新打开（所以这里放心关）。
     * 不加 s_inited 判断：没初始化过时清这几位也无害，反而更安全。 */
    PWMA_CC3NE_Disable();
    PWM3N_OUT_DIS();

    /* 【关键 · 2026-09-19 修"没进云台页舵机也会自己甩一下"】
     *
     * 关掉 PWM 输出之后，P2.5 就变回**普通 IO** 了。
     * 而它的输出锁存器复位值是 1（高电平），我又在 Init 里把 P2.5 配成了推挽（强驱动）——
     * 于是这根线就**一直卡在高电平**。
     * 对舵机来说，"信号线一直是高"相当于一个超长脉宽，它会直接甩到极限位置。
     * 真机现象就是这个：人不在云台页里，舵机偶尔自己转一下。
     *
     * 所以关输出之后**必须把引脚按到低电平** —— "一直是低"舵机才认得出这是"没有信号"，
     * 才会松开、不乱动。（这也是为什么原参考工程在 reset 里要做 GPIO 收尾。）
     *
     * 用寄存器掩码清 bit5：P2.5 是舵机专用脚，
     * P2 的其它位是 LED1/2(P2.7/P2.6) 和 LED5~8(P2.3~P2.0)，不受影响。 */
    P2 &= (u8)(~0x20);

}

void Servo_SetAngle(u8 angle)
{
    if (angle > 180)
    {
        angle = 180;
    }

    s_angle = angle;

    if (!s_inited)
    {
        return;
    }

    s_duty.PWM3_Duty = servo_duty_of(angle);
    UpdatePwm(PWM3, &s_duty);
}

u8 Servo_GetAngle(void)
{
    return s_angle;
}
