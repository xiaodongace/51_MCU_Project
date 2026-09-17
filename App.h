#ifndef _APP_H_
#define _APP_H_

#include "config.h"
#include <stdio.h>
#include "GPIO.h"
#include "I2C_OLED.h"
#include "SPI_OLED.h"
#include "STC8H_PWM.h"

#define PERIOD (MAIN_Fosc / 1000)

extern int8 count;
extern int8 temp;

extern u8 clear_screen;

extern void App_Init();
//PWM
extern PWMx_Duty dutyB;
//OLED
extern void Clear_screen(u8 is_go_clear_screen);
//Oscilloscope
extern void show_Oscilloscope();
#endif