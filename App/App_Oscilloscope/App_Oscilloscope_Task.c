#include "UART.h"
#include "NVIC.h"
#include "Switch.h"
#include "Oscilloscope.h"
#include "I2C.h"
#include "I2C_OLED.h"
#include "App.h"
#include "App_Public.h"

u8 duty_percent=0;
void test_task4() _task_ App_Oscilloscope_Task_Id {

    Oscilloscope_init();
    while(1){
        Clear_screen(0);
      
        Motor_pwm_duty(&duty_percent);
      
        os_wait2(K_TMO, 200);
    }
}

void show_Oscilloscope(void) {
    char str[24];
    sprintf(str, "duty=%d%%",(int)duty_percent);
    I2C_OLED_ShowString(0, 2, str, 16);
}


