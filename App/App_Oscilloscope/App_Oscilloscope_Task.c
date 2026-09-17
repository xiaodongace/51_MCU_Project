#include "UART.h"

#include "NVIC.h"

#include "Switch.h"

#include "Oscilloscope.h"

#include "I2C.h"

#include "I2C_OLED.h"
#define Oscilloscope_tast 1

void GPIO_config(void) {

    GPIO_InitTypeDef    GPIO_InitStructure;     //结构定义

    GPIO_InitStructure.Pin  = GPIO_Pin_0 | GPIO_Pin_1;      //指定要初始化的IO,

    GPIO_InitStructure.Mode = GPIO_PullUp;  //指定IO的输入或输出方式,GPIO_PullUp,GPIO_HighZ,GPIO_OUT_OD,GPIO_OUT_PP

    GPIO_Inilize(GPIO_P3, &GPIO_InitStructure);//初始化

}

void UART_config(void) {

    // >>> 记得添加 NVIC.c, UART.c, UART_Isr.c <<<

    COMx_InitDefine     COMx_InitStructure;                 //结构定义

    COMx_InitStructure.UART_Mode      = UART_8bit_BRTx; //模式, UART_ShiftRight,UART_8bit_BRTx,UART_9bit,UART_9bit_BRTx

    COMx_InitStructure.UART_BRT_Use   = BRT_Timer1;         //选择波特率发生器, BRT_Timer1, BRT_Timer2 (注意: 串口2固定使用BRT_Timer2)

    COMx_InitStructure.UART_BaudRate  = 115200ul;           //波特率, 一般 110 ~ 115200

    COMx_InitStructure.UART_RxEnable  = ENABLE;             //接收允许,   ENABLE或DISABLE

    COMx_InitStructure.BaudRateDouble = DISABLE;            //波特率加倍, ENABLE或DISABLE

    UART_Configuration(UART1, &COMx_InitStructure);     //初始化串口1 UART1,UART2,UART3,UART4

    NVIC_UART1_Init(ENABLE,Priority_1);     //中断使能, ENABLE/DISABLE; 优先级(低到高) Priority_0,Priority_1,Priority_2,Priority_3

    UART1_SW(UART1_SW_P30_P31);     // 引脚选择, UART1_SW_P30_P31,UART1_SW_P36_P37,UART1_SW_P16_P17,UART1_SW_P43_P44

}

void	I2C_config(void)
{
    I2C_InitTypeDef		I2C_InitStructure;

    I2C_InitStructure.I2C_Mode      = I2C_Mode_Master;	//主从选择   I2C_Mode_Master, I2C_Mode_Slave
    I2C_InitStructure.I2C_Enable    = ENABLE;			//I2C功能使能,   ENABLE, DISABLE
    I2C_InitStructure.I2C_MS_WDTA   = DISABLE;			//主机使能自动发送,  ENABLE, DISABLE
    I2C_InitStructure.I2C_Speed     = 13;				//总线速度=Fosc/2/(Speed*2+4),      0~63
    // 400k, 24M => 13
    I2C_Init(&I2C_InitStructure);
    NVIC_I2C_Init(I2C_Mode_Master,DISABLE,Priority_0);	//主从模式, I2C_Mode_Master, I2C_Mode_Slave; 中断使能, ENABLE/DISABLE; 优先级(低到高) Priority_0,Priority_1,Priority_2,Priority_3

    I2C_SW(I2C_P33_P32);					//I2C_P14_P15,I2C_P24_P25,I2C_P33_P32
}

u8 clear_screen = 1;

void test_task4() _task_ App_Oscilloscope_Task_Id {
    u8 duty_percent=0;

    float duty;

    Oscilloscope_init();

    GPIO_config();

    UART_config();

    I2C_config();
    while(1){

        clear_screen=1;
      
        duty=Motro_pwm_duty(&duty_percent);

        printf("duty=%.2f duty_percent=%d",duty,(int)duty_percent);
 
      
        os_create_task(4);
     }

}

void show_Oscillscope(){
    char str[32]=" ";
    sprintf(str,"duty=%.2f",duty);
    I2C_OLED_ShowString(0, 0,str, 16);
}
void test_task5() _task_ Oscilloscope_show{
    I2C_OLED_Init();
    I2C_OLED_ColorTurn(0);//0正常显示，1 反色显示
    I2C_OLED_DisplayTurn(0);//0正常显示 1 屏幕翻转显示
    I2C_OLED_Clear();  // 初始清屏
  
    while(1){
        if(clear_screen) {
            I2C_OLED_Clear();
            clear_screen=0;
            I2C_OLED_ShowString(0, 0,"Oscillscope", 16);
        }
        show_Oscillscope();
        os_wait1(K_SIG);
    }
}
