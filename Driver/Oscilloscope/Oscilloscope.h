#ifndef _OSCILLOSCOPE_H_
#define _OSCILLOSCOPE_H_

#include "App_Public.h"
#include "Uarts.h"

#define PERIOD (MAIN_Fosc / 1000)   // PWM???????????? = MAIN_Fosc / PERIOD = 1kHz

void Oscilloscope_init(void);

void Motor_pwm_duty(u8* duty_percent);

#endif
