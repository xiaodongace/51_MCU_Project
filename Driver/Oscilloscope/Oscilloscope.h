#ifndef _OSCILLOSCOPE_H_
#define _OSCILLOSCOPE_H_

#include "App_Public.h"
#include "STC8H_PWM.h"
#include "Uarts.h"

#define PERIOD (MAIN_Fosc / 1000)  

extern void Oscilloscope_init(void);

extern PWMx_Duty duty;

extern void Motor_pwm_duty(u8* duty_percent);

#endif
