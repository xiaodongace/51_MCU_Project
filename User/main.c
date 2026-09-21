#include "App_Public.h"
#include "Key.h"
#include "Buzzer.h"
#include "NIXIE.h"
#include "LED.h"
#include "DHT_11.h"
#include "NTC.h"
#include "Uarts.h"
#include "Oscilloscope.h"
#include "Storage.h"
#include "Alarmclock.h"

/*
 * 系统硬件初始化。
 * 任务使用的共享外设在创建任务之前统一初始化。
 */
void Sys_Init(void) {
    /* 初始化期间先关闭全局中断 */
    EA = 0;

    /* 允许访问STC8H扩展寄存器 */
    EAXSFR();

    /* 初始化1ms系统计时器 */
    Timers_Init();

    /* 初始化四个独立按键 */
    Key_Init();

    /* 初始化蜂鸣器和PWM */
    Buzzer_Init();

    /* 初始化8个LED */
    LED_Init();

    /*
     * 初始化数码管移位寄存器引脚。
     * Timer3中断开始运行后会周期调用Nixie_Refresh()。
     */
    Nixie_init();

    /* 以下功能尚未接入当前菜单流程 暂不在开机时初始化 */
    // DHT11_Init();
    // NTC_init();
    // Oscilloscope_init();
    // PCF8563_Init();

    /* 初始化UART1，用于调试信息输出 */
    Uarts_Init(UART_USE_1);

    /* 所有基础外设就绪后开启全局中断 */
    EA = 1;

    /* 输出启动完成标志 */
    printf("=====Sys_Init=====\r\n");
}

/*
 * RTX51主任务。
 * 只负责初始化、创建应用任务，完成后删除自身。
 */
void Main_Start() _task_ App_Main_Task_Id {
    /* 先完成全局硬件初始化 */
    Sys_Init();

    /* 创建SPI菜单和独立按键分发任务 */
    os_create_task(App_Menu_Task_Id);

    /* 蜂鸣器任务常驻，仅在PAGE_BUZZER状态下执行功能 */
    os_create_task(App_Buzzer_Task_Id);

    /* 数码管日期/时间数据更新任务 */
    os_create_task(App_Nixie_Task_Id);

    /* 其他功能任务后续按菜单开发进度启用 */
    // os_create_task(App_Oscilloscope_Task_Id);

    
    /* DHT11任务常驻，仅在PAGE_DHT11状态下采集并刷新I2C屏 */
    os_create_task(App_DHT11_Task_Id);

    /* 子任务创建完成后删除主启动任务 */
    os_delete_task(App_Main_Task_Id);
}
