#include "UART.h"
#include "NVIC.h"
#include "Switch.h"
#include "Oscilloscope.h"
#include "I2C.h"
#include "I2C_OLED.h"
#include "App.h"
#include "App_Public.h"
u8 clear_screen = 1;

void test_task4() _task_ App_Oscilloscope_Task_Id {
    u8 duty_percent = 0;
    u16 duty_now;
    Oscilloscope_init();
    while(1){
        clear_screen = 1;

        duty_now = Motor_pwm_duty(&duty_percent);

        printf("duty=%u duty_percent=%d\r\n", duty_now, duty_percent);
    }
}

void show_Oscilloscope(void) {
    char str[24];
    sprintf(str, "duty=%u/%u", (unsigned)duty.PWM6_Duty, (unsigned)(PERIOD - 1));
    I2C_OLED_ShowString(0, 0, str, 16);
}


