#ifndef __DTH11_H__
#define __DTH11_H__

#include "App_Public.h"

/* DHT11单总线连接到P4.6 */
#define DHT P46

/* 约1微秒的总线时序延时 */
#define DHT11_DELAY_1US() NOP18()

/* 初始化DHT11单总线GPIO */
void DHT11_Init(void);

/* 读取湿度和温度，成功返回SUCCESS */
int8 DHT11_get_info(
    float *p_humidity,
    float *p_temperature);

#endif
