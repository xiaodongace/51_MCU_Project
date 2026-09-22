#ifndef __APP_PUBLIC_H
#define __APP_PUBLIC_H

// 公共头文件
#include "Config.h"
#include "GPIO.h"
#include "Timers.h"
#include "App_Menu.h"
#include "I2C_OLED.h"
#include "DHT_11.h"
#include "SPI_OLED.h"
#include "NTC.h"
#include "ADC.h"

// 这里可以放置一些公共的宏定义、类型定义、函数声明等
#define App_Main_Task_Id            0
#define App_Menu_Task_Id            1
#define App_Nixie_Task_Id           2
#define App_DHT11_Task_Id           3
#define App_Oscilloscope_Task_Id    4
#define I2C_OLED_Task_ID            5
#define SPI_OLED_Task_ID            6
#define App_Buzzer_Task_Id          7  
#define App_Keys_Task_Id            8
#define App_Storage_RTC_Test_Id     9


//============App_Start========================开机动画
void Animation_Expand();
void Animation_Contract(void);
//=============================================开机动画

#endif
