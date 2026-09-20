#include "App_Public.h"
#include "Key.h"
#include "Buzzer.h"
#include "NIXIE.h"

#include "Uarts.h"
#include "LED.h"

void sys_init(void) {
	EA = 0;			// Configure peripherals before enabling interrupts.
	EAXSFR();		/* À©Õ¹¼Ä´æÆ÷·ÃÎÊÊ¹ÄÜ */

	/* ÍâÉè³õÊ¼»¯ */
	Timers_Init();
  	Key_Init();
	Buzzer_Init();
	LED_Init();
	Nixie_init();
	
    
    Uarts_Init(UART_USE_1);

	EA = 1;
	printf("==sys_init==\r\n");
}

// ï¿½ï¿½ï¿½ïº¯ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½, ï¿½ï¿½ï¿½é²»ÒªÊ¹ï¿½ï¿½start, ï¿½ï¿½ï¿½I2C.hï¿½ï¿½ï¿½Startï¿½ï¿½Í»
void main_start() _task_ App_Main_Task_Id {
	sys_init();
	// ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ 1
	// os_create_task(1);
	// ½áÊøÈÎÎñ 0
    os_create_task(App_Keys_Task_Id);
    os_create_task(SPI_OLED_Task_ID);
    os_create_task(I2C_OLED_Task_ID);

	// ´´½¨ÈÎÎñ 7 -> ·äÃùÆ÷ + Ğ¡µÆ
	// os_create_task(App_Buzzer_Task_Id);
	
		
	os_create_task(App_Nixie_Task_Id);
	
    os_delete_task(0);
}