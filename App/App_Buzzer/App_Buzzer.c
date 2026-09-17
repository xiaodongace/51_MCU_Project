//#include "Buzzer.h"
//#include "Key.h"
//#include "LED.h"

///* Buzzer任务函数
// * 1、扫描独立按键，根据按键1状态 播放 | 暂停 音符
// *     - 按键1松开时 通过 Buzzer_IsPlaying() 查询当前播放状态后取反
// * 2、根据按键2状态：三首音乐，按键2松开后切换下一首音乐
// * 3、根据按键3状态：按键3松开后音乐音量--，音量范围0-10
// * 4、根据按键4状态：按键4松开后音乐音量++，音量范围0-10
// * 5、播放时 LED 随机闪烁(2-6个),暂停时全灭
// * 6、数码管上面显示当前播放的音标
// * 
// * 6不着急做
//*/


//void Task_Buzzer() _task_ App_Buzzer_Task_Id {
//    u8 led_tick = 0;    // LED 刷新计数(每 15 轮 ≈ 150ms 换一组)

//    while (1)
//    {
//        Key_Scan();
//        if(Key_GetPressEvent(0) != 0) {
//            printf("111");

//            Buzzer_Play_Pause(!Buzzer_IsPlaying());
//        }
//        if(Key_GetPressEvent(1) != 0) {
//            printf("222: %d\n", (int)Song);

//            Buzzer_NextSong();
//        }
//        if(Key_GetPressEvent(2) != 0) {
//            Volume--;
//            if(Volume < 0) Volume = 0;
//            Buzzer_Refresh();
//            printf("333: %d\n", (int)Volume);
//        }
//        if(Key_GetPressEvent(3) != 0) {
//            Volume++;
//            if(Volume > 10) Volume = 10;
//            Buzzer_Refresh();
//            printf("444: %d\n", (int)Volume);
//        }

//        Buzzer_Tick();                  // 非阻塞推进:拍子到才换音

//        // 任务5:播放时 LED 随机闪烁,暂停/停止时全灭
//        if (++led_tick >= 15) {
//            led_tick = 0;
//            if (Buzzer_IsPlaying())
//                LED_Random();           // 随机点亮 2-6 个
//            else
//                LED_AllOff();           // 全灭
//        }

//        os_wait2(K_TMO, BUZZER_LOOP_TICKS);
//    }
//    
//}