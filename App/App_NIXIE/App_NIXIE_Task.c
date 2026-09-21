#include "NIXIE.h"

void text_task() _task_ App_Nixie_Task_Id {
    u8 page = 0;

    u16 last_1s;
    u16 last_2s;
    u16 now;

    last_1s = Timers_GetSystemMs();
    last_2s = last_1s;

    /* 初始显示日期 */
    Nixie_SetDigits(date_page);
    while (1) {
        now = Timers_GetSystemMs();

        /* 每隔 1 秒更新时间 */
        if((u16)(now - last_1s) >= 1000)
        {
            last_1s += 1000;

            Clock_Update();
            Update_Time_Page();

            /* 如果当前显示的是时间页面，就同步显示缓冲区 */
            if(page == 1)
            {
                Nixie_SetDigits(time_page);
            }
        }

        if(page == 0)
        {
            /* 日期页面显示 1 秒 */
            if((u16)(now - last_2s) >= 1000)
            {
                last_2s += 1000;

                page = 1;
                Nixie_SetDigits(time_page);
            }
        }
        else
        {
            /* 时间页面显示 5 秒 */
            if((u16)(now - last_2s) >= 5000)
            {
                last_2s += 5000;

                page = 0;
                Nixie_SetDigits(date_page);
            }
        }

        /* RTX51 任务延时 */
        os_wait2(K_TMO, 1);
    }
}