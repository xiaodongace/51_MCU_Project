#include "Uarts.h"
#include "Alarmclock.h"

/* Cube-ISP RTC帧头为01 0B，完整帧长为13字节。 */
#define RTC_ISP_HEADER_0       0x01
#define RTC_ISP_FRAME_LENGTH   13

/* 保存从通用UART1接收缓冲中提取的RTC帧。 */
static u8 xdata g_rtcIspFrame[RTC_ISP_FRAME_LENGTH];
static Clock_t xdata g_rtcIspClock;
static Clock_t xdata g_rtcVerifyClock;
static u8 g_rtcIspPosition = 0;
static u8 g_rtcRxScanPosition = 0;
static bit g_rtcIspFrameReady = 0;
static bit g_rtcSyncAttempted = 0;

/* 保留最近一次对时结果，供应用任务查询。 */
volatile u8 Uarts_RtcSyncLastStatus = UART_RTC_SYNC_WAITING;

/* 从UART1接收缓冲区读取一个字节时短暂保护缓冲索引。 */
static u8 Uarts_RtcReadRxByte(u8 position)
{
    bit oldEA;
    u8 dat;

    oldEA = EA;
    EA = 0;
    dat = RX1_Buffer[position];
    EA = oldEA;

    return dat;
}

/* 将新收到的字节累积为以01 0B开头的13字节对时帧。 */
static void Uarts_RtcConsumeByte(u8 dat)
{
    if (g_rtcIspFrameReady) return;

    if (g_rtcIspPosition == 0)
    {
        if (dat == RTC_ISP_HEADER_0)
        {
            g_rtcIspFrame[0] = dat;
            g_rtcIspPosition = 1;
        }
        return;
    }

    if (g_rtcIspPosition == 1)
    {
        if (dat == RTC_ISP_FRAME_LENGTH - 2)
        {
            g_rtcIspFrame[1] = dat;
            g_rtcIspPosition = 2;
        }
        else if (dat == RTC_ISP_HEADER_0)
        {
            g_rtcIspFrame[0] = dat;
            g_rtcIspPosition = 1;
        }
        else
        {
            g_rtcIspPosition = 0;
        }
        return;
    }

    g_rtcIspFrame[g_rtcIspPosition++] = dat;

    if (g_rtcIspPosition >= RTC_ISP_FRAME_LENGTH)
    {
        g_rtcIspPosition = 0;
        g_rtcIspFrameReady = 1;
    }
}

/* 计算2000至2099年指定月份的天数。 */
static u8 Uarts_RtcDaysInMonth(u8 year, u8 month)
{
    switch (month)
    {
        case 1:
        case 3:
        case 5:
        case 7:
        case 8:
        case 10:
        case 12:
            return 31;

        case 4:
        case 6:
        case 9:
        case 11:
            return 30;

        case 2:
            return ((year % 4) == 0) ? 29 : 28;

        default:
            return 0;
    }
}

/* 检查Cube-ISP提供的日期和时间字段是否在有效范围内。 */
static u8 Uarts_RtcTimeIsValid(const Clock_t *clock)
{
    u8 maxDay;

    if (clock->year > 99) return 0;
    if (clock->month < 1 || clock->month > 12) return 0;
    if (clock->hour > 23) return 0;
    if (clock->minute > 59) return 0;
    if (clock->second > 59) return 0;

    maxDay = Uarts_RtcDaysInMonth(clock->year, clock->month);
    if (clock->day < 1 || clock->day > maxDay) return 0;

    return 1;
}

/* 允许读回时间恰好跨过一秒边界。 */
static u8 Uarts_RtcMatchesSetTime(const Clock_t *setTime, const Clock_t *readTime)
{
    Clock_t expected;

    expected.year = setTime->year;
    expected.month = setTime->month;
    expected.day = setTime->day;
    expected.hour = setTime->hour;
    expected.minute = setTime->minute;
    expected.second = setTime->second;

    if (expected.second < 59)
    {
        expected.second++;
    }
    else
    {
        expected.second = 0;
        if (expected.minute < 59)
        {
            expected.minute++;
        }
        else
        {
            expected.minute = 0;
            if (expected.hour < 23)
            {
                expected.hour++;
            }
            else
            {
                expected.hour = 0;
                if (expected.day < Uarts_RtcDaysInMonth(expected.year, expected.month))
                {
                    expected.day++;
                }
                else
                {
                    expected.day = 1;
                    if (expected.month < 12)
                    {
                        expected.month++;
                    }
                    else
                    {
                        expected.month = 1;
                        expected.year = (expected.year + 1) % 100;
                    }
                }
            }
        }
    }

    if (readTime->year == setTime->year && readTime->month == setTime->month &&
        readTime->day == setTime->day && readTime->hour == setTime->hour &&
        readTime->minute == setTime->minute && readTime->second == setTime->second)
        return 1;

    if (readTime->year == expected.year && readTime->month == expected.month &&
        readTime->day == expected.day && readTime->hour == expected.hour &&
        readTime->minute == expected.minute && readTime->second == expected.second)
        return 1;

    return 0;
}

/* 提取新字节、写入RTC并回读验证，不向串口发送调试文本。 */
u8 Uarts_RtcSyncProcess(void)
{
    u8 rxPosition;
    u8 dat;

    /* 本次复位已经处理过一帧有效时间，不再重复读写RTC。 */
    if (g_rtcSyncAttempted) return Uarts_RtcSyncLastStatus;

    /* 原UART驱动缓冲区写满后会归零；在此兼容其回绕方式。 */
    /* 接收计数在UART1中断里更新，强制每次从内存读取其最新值。 */
    rxPosition = *((volatile u8 *)&COM1.RX_Cnt);
    if (rxPosition < g_rtcRxScanPosition)
    {
        while (g_rtcRxScanPosition < COM_RX1_Lenth)
        {
            dat = Uarts_RtcReadRxByte(g_rtcRxScanPosition++);
            Uarts_RtcConsumeByte(dat);
        }
        g_rtcRxScanPosition = 0;
    }

    while (g_rtcRxScanPosition < rxPosition)
    {
        dat = Uarts_RtcReadRxByte(g_rtcRxScanPosition++);
        Uarts_RtcConsumeByte(dat);
    }

    if (!g_rtcIspFrameReady) return UART_RTC_SYNC_WAITING;

    /* 完整帧保存在静态XDATA缓冲区，减少RTX51任务栈占用。 */
    g_rtcIspFrameReady = 0;

    if (g_rtcIspFrame[0] != RTC_ISP_HEADER_0 ||
        g_rtcIspFrame[1] != RTC_ISP_FRAME_LENGTH - 2)
    {
        Uarts_RtcSyncLastStatus = UART_RTC_SYNC_INVALID_TIME;
        return Uarts_RtcSyncLastStatus;
    }

    /* Cube-ISP RTC字段为普通数值，不是BCD码。 */
    g_rtcIspClock.year = g_rtcIspFrame[3];
    g_rtcIspClock.month = g_rtcIspFrame[4];
    g_rtcIspClock.day = g_rtcIspFrame[5];
    g_rtcIspClock.weekday = (g_rtcIspFrame[6] <= 6) ? g_rtcIspFrame[6] : 0;
    g_rtcIspClock.hour = g_rtcIspFrame[7];
    g_rtcIspClock.minute = g_rtcIspFrame[8];
    g_rtcIspClock.second = g_rtcIspFrame[9];

    if (!Uarts_RtcTimeIsValid(&g_rtcIspClock))
    {
        Uarts_RtcSyncLastStatus = UART_RTC_SYNC_INVALID_TIME;
        return Uarts_RtcSyncLastStatus;
    }

    /* 第一帧有效时间只尝试一次；失败状态保留到下一次复位。 */
    g_rtcSyncAttempted = 1;

    /* 写入目标时间，再读回RTC确认实际保存的时间。 */
    if (!PCF8563_SetTime(&g_rtcIspClock))
    {
        Uarts_RtcSyncLastStatus = UART_RTC_SYNC_WRITE_FAILED;
        return Uarts_RtcSyncLastStatus;
    }
    if (!PCF8563_GetTime(&g_rtcVerifyClock))
    {
        Uarts_RtcSyncLastStatus = UART_RTC_SYNC_READBACK_FAILED;
        return Uarts_RtcSyncLastStatus;
    }

    if (!Uarts_RtcMatchesSetTime(&g_rtcIspClock, &g_rtcVerifyClock))
    {
        Uarts_RtcSyncLastStatus = UART_RTC_SYNC_VERIFY_FAILED;
        return Uarts_RtcSyncLastStatus;
    }

    Uarts_RtcSyncLastStatus = UART_RTC_SYNC_OK;
    return Uarts_RtcSyncLastStatus;
}

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
        /* 每次初始化UART1时复位RTC帧解析状态。 */
        g_rtcIspPosition = 0;
        g_rtcRxScanPosition = 0;
        g_rtcIspFrameReady = 0;
        g_rtcSyncAttempted = 0;
        Uarts_RtcSyncLastStatus = UART_RTC_SYNC_WAITING;

        UART1_SW(UART1_SW_P30_P31);
        /* ========== Uart1引脚初始化 ========== */
        P3_MODE_IO_PU(GPIO_Pin_0 | GPIO_Pin_1);
        /* 准双向口写1释放RX引脚，并保持TX空闲电平为高。 */
        P30 = 1;
        P31 = 1;

        /* Timer0由RTX51使用、Timer3负责数码管；UART1改用空闲的Timer2。 */
        COMx_InitStructure.UART_BRT_Use = BRT_Timer2;
        /* 底层函数负责清空UART1通用收发缓冲区。 */
        UART_Configuration(UART1, &COMx_InitStructure);

        /* 24MHz/115200的Timer2重装值为65536-52，即0xFFCC。 */
        /* 先停表再写计数/重装寄存器，避免运行中只改到隐藏重装寄存器。 */
        AUXR &= (u8)~(S1BRT | T2_CT | T2x12 | T2R);
        TM2PS = 0;
        T2H = 0xFF;
        T2L = 0xCC;
        SCON = 0x50;
        /* UART1选择Timer2，Timer2使用内部1T时钟并开始计数。 */
        AUXR |= S1BRT | T2x12 | T2R;

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
