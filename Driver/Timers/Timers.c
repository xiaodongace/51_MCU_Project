#include "Timers.h"

/* 全局系统毫秒计数器 */
volatile u16 system_ms = 0;

/* 配置Timer3为1 ms自动重装系统节拍 */
static void Timer_Config(void) {
    TIM_InitTypeDef TIM_InitStructure; //结构定义
    TIM_InitStructure.TIM_Mode = TIM_16BitAutoReload;
    TIM_InitStructure.TIM_ClkSource = TIM_CLOCK_1T; //指定时钟源,     TIM_CLOCK_1T,TIM_CLOCK_12T,TIM_CLOCK_Ext
    TIM_InitStructure.TIM_ClkOut = DISABLE; //是否输出高速脉冲, ENABLE或DISABLE
    TIM_InitStructure.TIM_Value = 65536UL - (MAIN_Fosc / 1000UL); //初值,
    TIM_InitStructure.TIM_PS = 0;
    TIM_InitStructure.TIM_Run = ENABLE; //是否初始化后启动定时器, ENABLE或DISABLE
    Timer_Inilize(Timer3, &TIM_InitStructure);
    /* 开启 Timer3 中断后，Timer3_ISR_Handler 每 1 ms 递增 system_ms */
    NVIC_Timer3_Init(ENABLE,Priority_0);
}

/* 初始化应用层定时器模块 */
void Timers_Init(void) {
    Timer_Config();
}


/* 获取系统毫秒数 */
u16 Timers_GetSystemMs(void) {
    return system_ms;
}

