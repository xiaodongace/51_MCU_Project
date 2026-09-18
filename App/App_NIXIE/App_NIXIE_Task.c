#include "NIXIE.h"

void text_task() _task_ App_Nixie_Task_Id {
    u8 hour = 14;
    u8 minute = 59;
    u8 second = 50;
    u16 last_ms;
    u16 now;

    last_ms = Timers_GetSystemMs();

    while (1) {
        now = Timers_GetSystemMs();

        if ((u16)(now - last_ms) >= 1000) {
            last_ms += 1000;

            second++;

            if (second >= 60) {
                second = 0;
                minute++;

                if (minute >= 60) {
                    minute = 0;
                    hour++;

                    if (hour >= 24) {
                        hour = 0;
                    }
                }
            }

            Nixie_SetNumber(
                (u32)hour * 10000UL +
                (u32)minute * 100UL +
                second);
        }

        os_wait2(K_TMO, 1);
    }
}