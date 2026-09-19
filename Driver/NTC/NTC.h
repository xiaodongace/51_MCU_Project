#ifndef __NTC_H__
#define __NTC_H__

#include "App_Public.h"

#define NTC_CHANNEL      ADC_CH12

extern void NTC_init();

extern int NTC_get_temperature();


#endif