#ifndef __UARTS_H
#define __UARTS_H

#include "App_Public.h"
#include "UART.h"
#include "NVIC.h"
#include "Switch.h"

/* 选择串口宏配置 */
#define UART_USE_1  0x01
#define UART_USE_2  0x02
#define UART_USE_3  0x04
#define UART_USE_4  0x08

/*
    通用的串口初始化函数
    需要初始化某个串口使用宏进行逻辑或操作( | )
    例如初始化串口1和串口2: 
        Uarts_Init(UART_USE_1 | UART_USE_2);
*/
void Uarts_Init(unsigned char uartMask);

/* 在RTX51任务上下文处理完整Cube-ISP RTC帧并写入PCF8563。 */
u8 Uarts_RtcSyncProcess(void);

/* 对时状态：0=等待，1=成功，2=时间无效，3=回读不匹配，5/6=写入/读回失败。 */
#define UART_RTC_SYNC_WAITING         0
#define UART_RTC_SYNC_OK              1
#define UART_RTC_SYNC_INVALID_TIME    2
#define UART_RTC_SYNC_VERIFY_FAILED   3
#define UART_RTC_SYNC_WRITE_FAILED    5
#define UART_RTC_SYNC_READBACK_FAILED 6
extern volatile u8 Uarts_RtcSyncLastStatus;


#endif
