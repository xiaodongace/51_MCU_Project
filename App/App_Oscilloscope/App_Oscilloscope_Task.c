#include "UART.h"
#include "NVIC.h"
#include "Switch.h"
#include "Oscilloscope.h"
#include "I2C.h"
#include "I2C_OLED.h"
#include "App_Public.h"


void App_Oscilloscope_Task() _task_ App_Oscilloscope_Task_Id {

    
    while(1){
        Clear_screen(0);
      
        Motor_pwm_duty(&duty_percent);
      
        os_wait2(K_TMO, 200);
    }
}



void delete_Oscilloscope(void){
    //关闭小马达；
    //将屏幕占空比改为0；
}


