#ifndef _OSCILLOSCOPE_H_
#define _OSCILLOSCOPE_H_

#include "App_Public.h"
#include "STC8H_PWM.h"
#include "Uarts.h"

#define PERIOD (MAIN_Fosc / 1000)   // PWM周期计数，频率 = MAIN_Fosc / PERIOD = 1kHz

extern void Oscilloscope_init(void);

extern PWMx_Duty duty;

extern u16 Motor_pwm_duty(u8* duty_percent);

#endif
