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

//oled_clear_screen
extern u8 clear_screen;
//extern u8 is_go_clear;
//App
extern void App_Init();
//PWM
extern PWMx_Duty dutyB;
//OLED
extern void Clear_screen(u8 is_go_clear);
//Oscilloscope
extern void show_Oscilloscope();
#endif