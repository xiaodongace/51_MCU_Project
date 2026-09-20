#include "App_Public.h"
#include "Key.h"
#include "Buzzer.h"
#include "NIXIE.h"
#include "LED.h"
#include "DHT_11.h"
#include "NTC.h"
#include "Uarts.h"

void Sys_Init(void) {
	EA = 0;			// 关闭全局中断 初始化结束后统一开启
	EAXSFR();		/* 扩展寄存器访问使能 */

	/* 外设初始化 */
    Timers_Init();			// 定时器
    Key_Init();				// 独立按键
    Buzzer_Init();			// 蜂鸣器
    LED_Init();				// LED灯
    Nixie_init();			// 数码管
    DHT11_Init();			// DHT11
    NTC_init();				// NTC
	Oscilloscope_init();	// 电机
	
    
    Uarts_Init(UART_USE_1);	// 串口

	EA = 1;
	printf("=====Sys_Init=====\r\n");
}

// 这里函数名可随意, 建议不要使用start, 会和I2C.h里的Start冲突
void Main_Start() _task_ 0 {
	Sys_Init();
	// 创建任务 1
	// os_create_task(1);
	// 结束任务 0
    // os_create_task(SPI_OLED_Task_ID);
    // os_create_task(I2C_OLED_Task_ID);
    os_create_task(App_Oscilloscope_Task_Id);

	// 创建任务 7 -> 蜂鸣器 + 小灯
	os_create_task(App_Buzzer_Task_Id);


	// os_create_task(App_Nixie_Task_Id);
	// 温湿度—I2C_OLED
	// os_create_task(App_DHT11_Task_Id);
	
    os_delete_task(0);
}