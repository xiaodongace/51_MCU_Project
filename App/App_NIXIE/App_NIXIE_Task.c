#include "NIXIE.h"
#include "Alarmclock.h"
#include "App_Menu.h"

/* RTC读取间隔，以及两个页面的显示时长。 */
#define RTC_READ_INTERVAL_MS  1000U
#define DATE_SHOW_MS          1000U
#define TIME_SHOW_MS          5000U

/* 22是当前数码管驱动中的空白显示编码。 */
static u8 blank_page[8] = {22, 22, 22, 22, 22, 22, 22, 22};

/* 放在任务栈外，减少RTX51任务栈占用。 */
static Clock_t xdata rtc_now;

/* 从RTC读取时间，并生成日期页、时间页。成功返回1。 */
static u8 Nixie_LoadRtcPages(void)
{
    /* 读取的是PCF8563当前时间，不是上次串口发送的时间。 */
    if (PCF8563_GetTime(&rtc_now) == 0)
    {
        return 0;
    }

    /* 防止无效数据被当作数码管显示编码使用。 */
    if (rtc_now.year > 99 ||
        rtc_now.month < 1 || rtc_now.month > 12 ||
        rtc_now.day < 1 || rtc_now.day > 31 ||
        rtc_now.hour > 23 ||
        rtc_now.minute > 59 ||
        rtc_now.second > 59)
    {
        return 0;
    }

    /* 日期页：20YY.MM.DD；10～19表示带小数点的数字。 */
    date_page[0] = 2;
    date_page[1] = 0;
    date_page[2] = rtc_now.year / 10;
    date_page[3] = 10 + rtc_now.year % 10;
    date_page[4] = rtc_now.month / 10;
    date_page[5] = 10 + rtc_now.month % 10;
    date_page[6] = rtc_now.day / 10;
    date_page[7] = 10 + rtc_now.day % 10;

    /* 时间页沿用工程当前的“时-分-秒”分隔符，21表示横杠。 */
    time_page[0] = rtc_now.hour / 10;
    time_page[1] = rtc_now.hour % 10;
    time_page[2] = 21;
    time_page[3] = rtc_now.minute / 10;
    time_page[4] = rtc_now.minute % 10;
    time_page[5] = 21;
    time_page[6] = rtc_now.second / 10;
    time_page[7] = rtc_now.second % 10;

    return 1;
}

void text_task() _task_ App_Nixie_Task_Id
{
    u8 page = 0;             /* 0：日期页；1：时间页。 */
    u8 rtc_valid = 0;        /* 尚未成功读取RTC时不显示旧的固定时间。 */
    u16 last_read_ms;
    u16 last_page_ms;
    u16 now;

    now = Timers_GetSystemMs();
    last_read_ms = (u16)(now - RTC_READ_INTERVAL_MS); /* 首轮立即尝试读取。 */
    last_page_ms = now;
    Nixie_SetDigits(blank_page);

    while (1)
    {
        now = Timers_GetSystemMs();

        if ((u16)(now - last_read_ms) >= RTC_READ_INTERVAL_MS)
        {
            last_read_ms = now;

            /*
             * DHT11页面使用同一条I2C总线；此处暂时跳过RTC读取，
             * 显示上次读到的时间。正式并发使用仍需I2C互斥。
             */
            if (current_page != PAGE_DHT11 && Nixie_LoadRtcPages())
            {
                if (rtc_valid == 0)
                {
                    /* 首次读取成功，从日期页开始显示。 */
                    rtc_valid = 1;
                    page = 0;
                    last_page_ms = now;
                }

                /* 当前显示哪一页，就刷新哪一页的数据。 */
                Nixie_SetDigits(page == 0 ? date_page : time_page);
            }
        }

        if (rtc_valid && page == 0 &&
            (u16)(now - last_page_ms) >= DATE_SHOW_MS)
        {
            /* 日期显示满1秒后切换到时间。 */
            page = 1;
            last_page_ms = now;
            Nixie_SetDigits(time_page);
        }
        else if (rtc_valid && page == 1 &&
                 (u16)(now - last_page_ms) >= TIME_SHOW_MS)
        {
            /* 时间显示满5秒后切换回日期。 */
            page = 0;
            last_page_ms = now;
            Nixie_SetDigits(date_page);
        }

        /* 让出CPU；不要在定时器中断里调用RTC的I2C读取函数。 */
        os_wait2(K_TMO, 1);
    }
}