/*
 * Timers.c - Timer3 1ms 系统时钟初始化
 *
 * 【2026-09-18 真机定位到的根因 · 必读】
 *
 * 现象：g_sysTick 每 5 秒只增加 23（应该 5000），即 Timer3 实际周期约 217ms 而不是 1ms。
 * 所有依赖 g_sysTick 的功能全坏：按键消抖永不成立（按键无反应）、
 * 每秒节拍几乎不触发（时间冻结）、闹钟调度不动作。
 *
 * 根因：Timer3 有一个"预分频寄存器" TM3PS，而 Lib/Timer.c 的 Timer_Inilize() 里
 * 对 Timer3 分支是这样写的（原文）：
 *
 *     if(TIM == Timer3)
 *     {
 *         ...
 *         T3_Load(TIMx->TIM_Value);
 *         TM3PS = TIMx->TIM_PS;      // <-- 把结构体里的 TIM_PS 直接写进预分频寄存器
 *         Timer3_Run(TIMx->TIM_Run);
 *     }
 *
 * 实际周期 = (TIM_Value 数) x (TM3PS + 1) / Fosc = (TM3PS + 1) 毫秒。
 * 而下面这份配置**没有给 TIM_PS 赋值** —— TIM_InitTypeDef 是局部变量，
 * TIM_PS 里是内存垃圾值，于是预分频被设成了任意值（实测约 216，正好对应 217ms）。
 *
 * 为什么之前几版"看起来是好的"：这个局部变量落在 C51 的可覆盖段里，
 * 它的值是随代码布局变化的 —— 改动别的代码就会换一个值。属于典型的潜伏 bug。
 *
 * 教训：不能"逐字照抄"某份代码就以为安全 —— 那份代码（用户原工程的 Timers.c）
 * 本身就漏了这一行。结构体的每个字段都要对着库函数实现核一遍。
 */
#include "Timers.h"
#include "Timer.h"      /* Timer_Inilize / TIM_InitTypeDef / TIM_* 宏 */
#include "NVIC.h"       /* NVIC_Timer3_Init */
#include "App_Public.h" /* g_sysTick / SysTick_Get */

/* 配置 Timer3 为 1ms 自动重装，并打开其中断 */
void Timers_Init(void)
{
    TIM_InitTypeDef TIM_InitStructure;                                  /* 结构定义 */

    TIM_InitStructure.TIM_Mode      = TIM_16BitAutoReload;
    TIM_InitStructure.TIM_ClkSource = TIM_CLOCK_1T;                     /* 1T 模式 */
    TIM_InitStructure.TIM_ClkOut    = DISABLE;                          /* 不输出高速脉冲 */
    /* 初值：65536 - 主频/1000 -> 每 1ms 溢出一次 */
    TIM_InitStructure.TIM_Value     = 65536UL - (MAIN_Fosc / 1000UL);
    /* 【关键】预分频必须显式写 0。
     * 漏了这一行，TM3PS 就是内存垃圾值，周期变成 (垃圾+1) 毫秒。 */
    TIM_InitStructure.TIM_PS        = 0;
    TIM_InitStructure.TIM_Run       = ENABLE;                           /* 初始化后即启动 */
    Timer_Inilize(Timer3, &TIM_InitStructure);

    /* 打开 Timer3 中断：之后 Timer3_ISR_Handler 每 1ms 把 g_sysTick 拨一格 */
    NVIC_Timer3_Init(ENABLE, Priority_0);
}
