#include "App_Public.h"
#include "Key.h"
#include "Buzzer.h"
#include "LED.h"

void sys_init(void) {
	EA = 1;			// 使能全局中断
	EAXSFR();		/* 扩展寄存器访问使能 */

	Timers_Init();
  Key_Init();
	Buzzer_Init();
	LED_Init();
    
    printf("==sys_init==\n");
}

// 这里函数名可随意, 建议不要使用start, 会和I2C.h里的Start冲突
void main_start() _task_ App_Main_Task_Id {
	sys_init();
	// 创建任务 1
	// os_create_task(1);
	// 结束任务 0
    os_create_task(I2C_OLED_Task_ID );
    os_create_task(App_Oscilloscope_Task_Id);

	// 创建任务 7 -> 蜂鸣器 + 小灯
    os_create_task(App_Buzzer_Task_Id);
	
    os_delete_task(0);
}