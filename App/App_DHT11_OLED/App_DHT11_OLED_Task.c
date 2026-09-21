#include "App_Public.h"
#include "I2C_OLED.h"
#include "DHT_11.h"
#include "ADC.h"
#include "NTC.h"
#include <stdio.h>
void App_DHT11_OLED_Task(void)_task_ App_DHT11_Task_Id {
    u8 x = 0;
    u16 adc_value;

    float humidity, temperature, vol, ntc;
    int8 rst; // rst -> result  
    char humBuf[32], tempBuf[32], volBuf[32], ntcBuf[32];
    
    
    I2C_OLED_Init();
	I2C_OLED_ColorTurn(0);
    I2C_OLED_DisplayTurn(0);
    
    while(1) {
        rst = DHT11_get_info(&humidity, &temperature);

        // 电压
        adc_value = Get_ADCResult(ADC_CH13);
        vol = adc_value * 2.5 / 4096;
        // 计算热敏电阻温度
        ntc = NTC_get_temperature();
        // printf("vol: %.2fV, ntc: %.2f\n", vol, ntc);
        if(rst == SUCCESS) {
            // printf("湿度：%.2f%%, 温度：%.2f\n", humidity, temperature);
            sprintf(humBuf, "%.2f%%", humidity);
            sprintf(tempBuf, "%.2f", temperature);
            sprintf(volBuf, "%.2fV", vol);
            sprintf(ntcBuf, "%.2f", ntc);
            // 显示湿度
            x = 0;
            x = I2C_OLED_ShowChinese(x,0,0,16);
            x = I2C_OLED_ShowChinese(x,0,1,16);
            x = I2C_OLED_ShowChar(x,0,':',16);
            x = I2C_OLED_ShowString(x, 0, humBuf, 16);
            // 显示温度
            x = 0;
            x = I2C_OLED_ShowChinese(x,2,2,16);
            x = I2C_OLED_ShowChinese(x,2,3,16);
            x = I2C_OLED_ShowChar(x,2,':',16);
            x = I2C_OLED_ShowString(x, 2, tempBuf, 16);
            x = I2C_OLED_ShowChinese(x,2,4,16);
            // 显示电压
            x = 0;
            x = I2C_OLED_ShowChinese(x,4,5,16);
            x = I2C_OLED_ShowChinese(x,4,6,16);
            x = I2C_OLED_ShowChar(x,4,':',16);
            x = I2C_OLED_ShowString(x, 4, volBuf, 16);
            // 显示热敏电阻
            x = 0;
            x = I2C_OLED_ShowChinese(x,6,9,16);
            x = I2C_OLED_ShowChinese(x,6,10,16);
            x = I2C_OLED_ShowChar(x,6,':',16);
            x = I2C_OLED_ShowString(x, 6, ntcBuf, 16);
            x = I2C_OLED_ShowChinese(x,6,11,16);
        } else {
            x = 0;
            x = I2C_OLED_ShowChinese(x,0,0,16);
            x = I2C_OLED_ShowChinese(x,0,1,16);
        }
        
        os_wait2(K_TMO, 200);
    }
}