#include "App_Public.h"
#include "Key.h"
#include "Buzzer.h"
#include "LED.h"

void sys_init(void) {
	EA = 1;			// ʹ��ȫ���ж�
	EAXSFR();		/* ��չ�Ĵ�������ʹ�� */

	Timers_Init();
  Key_Init();
	Buzzer_Init();
	LED_Init();
    
    printf("==sys_init==\n");
}

// ���ﺯ����������, ���鲻Ҫʹ��start, ���I2C.h���Start��ͻ
void main_start() _task_ App_Main_Task_Id {
	sys_init();
	// �������� 1
	// os_create_task(1);
	// �������� 0
    os_create_task(SPI_OLED_Task_ID);
    // 注意: 本工程没有 _task_ 5 的定义, 创建它会取到空的任务入口(0x0000)导致复位
    os_create_task(App_Oscilloscope_Task_Id);

	// �������� 7 -> ������ + С��
    os_create_task(App_Buzzer_Task_Id);
	
    os_delete_task(0);
}