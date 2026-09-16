#ifndef _OSCILLOSCOPE_H_

#define _OSCILLOSCOPE_H_

#include "STC8H_PWM.h"

#include "NVIC.h"

#include "GPIO.h"

#include "Switch.h"

#define MOTOR P01

extern void Oscilloscope_init();

extern PWMx_Duty duty;

extern float Motro_pwm_duty(u8* duty_percent);

#endif
