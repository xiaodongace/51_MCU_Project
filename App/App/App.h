#ifndef _APP_H_
#define _APP_H_

#include "config.h"
#include <stdio.h>
#include "GPIO.h"
#include "I2C_OLED.h"
#include "SPI_OLED.h"


extern int8 count;

//oled_clear_screen
extern u8 clear_screen;
//extern u8 is_go_clear;
//App
extern void App_Init();
//OLED
extern void Clear_screen(u8 is_go_clear);
//Oscilloscope
extern void show_Oscilloscope();
#endif
