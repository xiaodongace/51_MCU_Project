#include "App.h"
#include "SPI_OLED.h"
#include "App_Public.h"

typedef struct {
    char name[16];
    char title[16];
} menu_item_t;

menu_item_t menu_items[] = {
    {"8个LED闪烁", "LED"},
    {"热敏电阻", "NTC-TMP"},
    {"电位器+马达", "POT+MOTOR"},
    {"RTC时钟", "RTC"},
    {"数码管", "Digit"},
    {"温湿度", "Temp+Humid"},
    {"键盘蜂鸣器", "Keyboard-Beep"},
}; 

u8 clear_screen = 1;

void test_task6() _task_ SPI_OLED_Task_ID {
    char arr[32]="a";
    int8 i;
    SPI_OLED_Init();
    SPI_OLED_ColorTurn(0);//0正常显示，1 反色显示
    SPI_OLED_DisplayTurn(0);//0正常显示 1 屏幕翻转显示
    SPI_OLED_Clear();  // 初始清屏
    while(1) {
        SPI_OLED_Clear();  // 初始清屏
        for(i=0; i<4; i++) {
            u8 index=(count+i)%7;
            sprintf(arr,"%c %d. %s",(i==0?'>':' '),(int)index,menu_items[index].name);
            SPI_OLED_Display_GB2312_string(0,i*2, arr);
        }
        os_wait1(K_SIG);
    }
}

void Clear_screen(u8 is_go_clear){
    if(is_go_clear==1)
        clear_screen=1;
    os_send_signal(5);
}

void test_task5() _task_ I2C_OLED_Task_ID  {
    os_wait2(K_TMO, 200);
    I2C_OLED_Init();
    I2C_OLED_ColorTurn(0);
    I2C_OLED_DisplayTurn(0);
    I2C_OLED_Clear();

    while(1){
        if(clear_screen) {
            I2C_OLED_Clear();
            clear_screen = 0;
            I2C_OLED_ShowString(0, 0, "Oscilloscope", 16);
        }
        show_Oscilloscope();
        os_wait1(K_SIG);
    }
}