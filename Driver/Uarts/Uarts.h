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


#endif
