#include "Uarts.h"

/*
    通用的串口初始化函数
    需要初始化某个串口使用宏进行逻辑或操作( | )
    例如初始化串口1和串口2: 
        Uarts_Init(UART_USE_1 | UART_USE_2);
*/
void Uarts_Init(unsigned char uartMask) {
    COMx_InitDefine COMx_InitStructure;

    /* 四路串口通用参数 */
    COMx_InitStructure.UART_Mode       = UART_8bit_BRTx;
    COMx_InitStructure.UART_BaudRate   = 115200ul;
    COMx_InitStructure.UART_RxEnable   = ENABLE;
    COMx_InitStructure.BaudRateDouble  = DISABLE;

    if (uartMask & UART_USE_1)
    {
        UART1_SW(UART1_SW_P30_P31);
        /* ========== Uart1引脚初始化 ========== */
        P3_MODE_IO_PU(GPIO_Pin_0 | GPIO_Pin_1);

        COMx_InitStructure.UART_BRT_Use = BRT_Timer1;
        UART_Configuration(UART1, &COMx_InitStructure);
        NVIC_UART1_Init(ENABLE, Priority_1);
    }

    if (uartMask & UART_USE_2)
    {
        UART2_SW(UART2_SW_P10_P11);
        /* ========== Uart2引脚初始化 ========== */
        P1_MODE_IO_PU(GPIO_Pin_0 | GPIO_Pin_1);

        COMx_InitStructure.UART_BRT_Use = BRT_Timer2;
        UART_Configuration(UART2, &COMx_InitStructure);
        NVIC_UART2_Init(ENABLE, Priority_1);
    }

    if (uartMask & UART_USE_3)
    {
        UART3_SW(UART3_SW_P00_P01);
        /* ========== Uart3引脚初始化 ========== */
        P0_MODE_IO_PU(GPIO_Pin_0 | GPIO_Pin_1);

        COMx_InitStructure.UART_BRT_Use = BRT_Timer3;
        UART_Configuration(UART3, &COMx_InitStructure);
        NVIC_UART3_Init(ENABLE, Priority_1);
    }

    if (uartMask & UART_USE_4)
    {
        UART4_SW(UART4_SW_P02_P03);
        /* ========== Uart4引脚初始化 ========== */
        P0_MODE_IO_PU(GPIO_Pin_2 | GPIO_Pin_3);

        COMx_InitStructure.UART_BRT_Use = BRT_Timer4;
        UART_Configuration(UART4, &COMx_InitStructure);
        NVIC_UART4_Init(ENABLE, Priority_1);   
    }
}

