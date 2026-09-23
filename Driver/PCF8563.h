#ifndef __PCF8563_H__
#define __PCF8563_H__

#include "Config.h"

#define NUMBER 7

#define PCF8563_ADDR            (0x51 << 1)  // 设备写地址 0xA2 == 0x51 << 1
#define PCF8563_REG             (0x02)        // 寄存器地址: 秒钟寄存器开始读

// 配置功能开关
/* 本项目修正：v3.1 这里是 DISABLE，导致 PCF8563_int_call() 的函数体
 * 被 #if 整段裁掉，闹钟中断实际从未被处理。要真用 P3.7 到点唤醒，必须打开。 */
#define PCF8563_ALARM_ENABLE     ENABLE
#define PCF8563_TIMER_ENABLE     DISABLE
    
// 定义时钟的结构体
typedef struct Clock
{
    u16 year;
    u8 month;
    u8 day;
    u8 week;
    u8 hour;
    u8 minute;
    u8 second;
} Clock_t;

// 定义Alarm闹铃的结构体
typedef struct Alarm
{
    // [-128, 127]
    int8 minute; // 59  
    int8 hour;   // 23
    int8 day;    // 31
    int8 week;   // 6
} Alarm_t;

// 定时器频率枚举
typedef enum {

    HZ4096 = 0x00,
    HZ64   = 0x01,
    HZ1    = 0x02,
    HZ1_60 = 0x03,
    
} TimerFreq; // Frequence

// Clock ---------------------------------------------

// 闹钟/定时器中断标志（定义在 PCF8563.c）。INT3 中断里置 1，由任务清零并处理。
extern volatile bit g_rtcIrqFlag;

// 初始化PCF8563 (引脚和I2C)
void PCF8563_init(void);

// 写日期和时间
void PCF8563_set_clock(Clock_t clock);

// 读日期和时间
void PCF8563_get_clock(Clock_t *p_clock);

// Alarm ---------------------------------------------

// 设置闹铃
void PCF8563_set_alarm(Alarm_t alarm);

// 启用闹铃
void PCF8563_enable_alarm(u8 enable);

// Timer ---------------------------------------------

// 处理中断消息 ----------------------------------------
void PCF8563_int_call(void);

// 要求用户实现的闹铃和定时器处理函数
extern void PCF8563_on_alarm();
extern void PCF8563_on_timer();

#endif