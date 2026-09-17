#include "Oscilloscope.h"


void test_task1() _task_ App_Oscilloscope_Task_Id {
    u8 duty_percent=0;

    float duty;

    Oscilloscope_init();

    Uarts_Init(UART_USE_1);

    while(1){

        duty=Motro_pwm_duty(&duty_percent);

         printf("duty=%.2f duty_percent=%d",duty,(int)duty_percent);
 
         os_wait2(K_TMO, 1);

     }

}

//void test_task2() _task_ Oscilloscope_show(){
//    SPI_OLED_Init();
//    SPI_OLED_ColorTurn(0);//0正常显示，1 反色显示
//    SPI_OLED_DisplayTurn(0);//0正常显示 1 屏幕翻转显示
//    SPI_OLED_Clear();  // 初始清屏
//  
//    while(1){
//        if(clear_screen) {
//            OLED_Clear();
//            clear_screen=0;
//            OLED_ShowString(0, 0,menu_items[count].title, 16);
//        }
//    }
//}
