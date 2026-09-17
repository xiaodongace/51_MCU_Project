#ifndef __APP_PUBLIC_H
#define __APP_PUBLIC_H

// 公共头文件
#include "Config.h"
#include "GPIO.h"
#include "Timers.h"

// 这里可以放置一些公共的宏定义、类型定义、函数声明等
#define App_Main_Task_Id            0
#define App_RTC_Task_Id             1
#define App_Nixie_Task_Id           2
#define App_DHT11_Task_Id           3
#define App_Oscilloscope_Task_Id    4
#define Oscilloscope_show           5
#define App_Buzzer_Task_Id          7       

#endif
