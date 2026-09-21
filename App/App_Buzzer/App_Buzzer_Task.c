#include "App_Menu.h"
#include "Buzzer.h"
#include "LED.h"
#include "NIXIE.h"

/* Buzzer任务函数
 * 1、按键由App_Menu_Task统一扫描和分发，本任务不再读取按键
 * 2、只有current_page等于PAGE_BUZZER时才运行蜂鸣器功能
 * 3、播放时LED随机闪烁，暂停或退出时全灭
 * 4、保留原有走马灯数码管功能
*/


void App_Buzzer_Task() _task_ App_Buzzer_Task_Id {
    u8 led_tick = 0;    // LED 刷新计数(每 15 轮 ≈ 150ms 换一组)

    while (1)
    {
        /* 只有菜单进入蜂鸣器功能后才执行原有逻辑 */
        if (current_page == PAGE_BUZZER) {
            /* 保留原有数码管走马灯功能 */
            Nixie_Run();

            /* 非阻塞推进音乐，只在拍子结束时切换音符 */
            Buzzer_Tick();

            /* 每15轮刷新一次LED效果 */
            if (++led_tick >= 15) {
                /* 清零LED刷新计数 */
                led_tick = 0;

                /* 播放时随机点亮2至6个LED */
                if (Buzzer_IsPlaying())
                    LED_Random();
                else
                    /* 暂停或停止时关闭全部LED */
                    LED_AllOff();
            }
        }
        else {
            /* 非蜂鸣器页面不累计LED刷新时间 */
            led_tick = 0;
        }

        /* 使用固定节拍让出CPU，任务不会忙等待 */
        os_wait2(K_TMO, BUZZER_LOOP_TICKS);
    }
}
