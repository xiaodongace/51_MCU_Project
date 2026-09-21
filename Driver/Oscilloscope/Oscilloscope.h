#ifndef _OSCILLOSCOPE_H_
#define _OSCILLOSCOPE_H_

#include "App_Public.h"

#include "Uarts.h"

extern u8 clear_screen;
extern u8 duty_percent;

#define PERIOD (MAIN_Fosc / 1000)   // PWM???????????? = MAIN_Fosc / PERIOD = 1kHz

void Oscilloscope_init(void);

void Motor_pwm_duty(u8* duty_percent);

void Clear_screen(u8 is_go_clear);

void show_Oscilloscope(void);
#endif
