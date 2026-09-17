#ifndef __APP_LED_H__
#define __APP_LED_H__

#include "App_Public.h"

// LED 引脚定义
#define LED_SW	    P45
#define LED1		P27
#define LED2		P26
#define LED3		P15
#define LED4		P14
#define LED5		P23
#define LED6		P22
#define LED7		P21
#define LED8		P20

void LED_Init(void);
// 全部熄灭
void LED_AllOff(void);

// 随机点亮 2-6 个 LED 灯
void LED_Random(void);

#endif
