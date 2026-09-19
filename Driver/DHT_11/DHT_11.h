#ifndef __DTH11_H__
#define __DTH11_H__

#include "App_Public.h"
#include "I2C_OLED/I2C_OLED.h"


#define DHT P46                                                                              

// 等待电平变换
#define delay_1us()  NOP18()

#define wait_level_change(level, min, max, desc)                                                               \
    do{                                                                                                        \
        cnt = 0; /*确保开始是0*/                                                                                \
        while(DHT == level){                                                                                   \
            /*每循环一次,代表过去了1us,通过cnt记录时间*/                                                          \
            delay_1us();                                                                                       \
            cnt++;                                                                                             \
        };                                                                                                     \
                                                                                                               \
        /*不符合目标范围, 及时短路返回, 避免代码嵌套*/                                                            \
        if(cnt < min || cnt > max) {                                                                           \
            printf("err: 时间[%dus], 不满足 %s[%dus, %dus]\n", cnt, desc, (int)min, (int)max, (int)__LINE__);   \
            return -2;                                                                                         \
        }                                                                                                      \
    }while(0)
    
    

void DHT11_Init();

int8 DHT11_get_info(float* p_humidity, float* p_temperature);

#endif