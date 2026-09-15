#ifndef __TIMERS_H
#define __TIMERS_H

#include "App_Public.h"
#include "Timer.h"
#include "NVIC.h"

/* 初始化应用层定时器模块 */
void Timers_Init();


/* 获取系统毫秒数 */
u16 Timers_GetSystemMs();

#endif
