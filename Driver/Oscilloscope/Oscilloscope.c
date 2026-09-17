#include "Oscilloscope.h"

static void Oscilloscope_GPIO_config(void) {
    GPIO_InitTypeDef    GPIO_InitStructure;     //结构定义

    /* 初始化P01为推挽 */
    GPIO_InitStructure.Pin  = GPIO_Pin_1;       //指定要初始化的IO,
    GPIO_InitStructure.Mode = GPIO_OUT_PP;  //指定IO的输入或输出方式,GPIO_PullUp,GPIO_HighZ,GPIO_OUT_OD,GPIO_OUT_PP
    GPIO_Inilize(GPIO_P0, &GPIO_InitStructure);//初始化

    /* 初始化P30 P31为准双向 */
    GPIO_InitStructure.Pin  = GPIO_Pin_0 | GPIO_Pin_1; 
    GPIO_InitStructure.Mode = GPIO_PullUp;
    GPIO_Inilize(GPIO_P3, &GPIO_InitStructure);
}

#define PERIOD (MAIN_Fosc / 1000)

PWMx_Duty dutyB;

void    PWM_config(void)

{

    PWMx_InitDefine     PWMx_InitStructure;

    

//  // 配置PWM5

//  PWMx_InitStructure.PWM_Mode         = CCMRn_PWM_MODE1;  //模式,     CCMRn_FREEZE,CCMRn_MATCH_VALID,CCMRn_MATCH_INVALID,CCMRn_ROLLOVER,CCMRn_FORCE_INVALID,CCMRn_FORCE_VALID,CCMRn_PWM_MODE1,CCMRn_PWM_MODE2

//  PWMx_InitStructure.PWM_Duty         = dutyB.PWM5_Duty;  //PWM占空比时间, 0~Period

//  PWMx_InitStructure.PWM_EnoSelect    = ENO5P;            //输出通道选择, ENO1P,ENO1N,ENO2P,ENO2N,ENO3P,ENO3N,ENO4P,ENO4N / ENO5P,ENO6P,ENO7P,ENO8P

//  PWM_Configuration(PWM5, &PWMx_InitStructure);           //初始化PWM,  PWMA,PWMB

    // 配置PWM6

    PWMx_InitStructure.PWM_Mode         = CCMRn_PWM_MODE1;  //模式,     CCMRn_FREEZE,CCMRn_MATCH_VALID,CCMRn_MATCH_INVALID,CCMRn_ROLLOVER,CCMRn_FORCE_INVALID,CCMRn_FORCE_VALID,CCMRn_PWM_MODE1,CCMRn_PWM_MODE2

    PWMx_InitStructure.PWM_Duty         = dutyB.PWM6_Duty;  //PWM占空比时间, 0~Period

    PWMx_InitStructure.PWM_EnoSelect    = ENO6P;            //输出通道选择, ENO1P,ENO1N,ENO2P,ENO2N,ENO3P,ENO3N,ENO4P,ENO4N / ENO5P,ENO6P,ENO7P,ENO8P

    PWM_Configuration(PWM6, &PWMx_InitStructure);           //初始化PWM,  PWMA,PWMB

//  // 配置PWM7

//  PWMx_InitStructure.PWM_Mode         = CCMRn_PWM_MODE1;  //模式,     CCMRn_FREEZE,CCMRn_MATCH_VALID,CCMRn_MATCH_INVALID,CCMRn_ROLLOVER,CCMRn_FORCE_INVALID,CCMRn_FORCE_VALID,CCMRn_PWM_MODE1,CCMRn_PWM_MODE2

//  PWMx_InitStructure.PWM_Duty         = dutyB.PWM7_Duty;  //PWM占空比时间, 0~Period

//  PWMx_InitStructure.PWM_EnoSelect    = ENO7P;            //输出通道选择, ENO1P,ENO1N,ENO2P,ENO2N,ENO3P,ENO3N,ENO4P,ENO4N / ENO5P,ENO6P,ENO7P,ENO8P

//  PWM_Configuration(PWM7, &PWMx_InitStructure);           //初始化PWM,  PWMA,PWMB

//  // 配置PWM8

//  PWMx_InitStructure.PWM_Mode         = CCMRn_PWM_MODE1;  //模式,     CCMRn_FREEZE,CCMRn_MATCH_VALID,CCMRn_MATCH_INVALID,CCMRn_ROLLOVER,CCMRn_FORCE_INVALID,CCMRn_FORCE_VALID,CCMRn_PWM_MODE1,CCMRn_PWM_MODE2

//  PWMx_InitStructure.PWM_Duty         = dutyB.PWM8_Duty;  //PWM占空比时间, 0~Period

//  PWMx_InitStructure.PWM_EnoSelect    = ENO8P;            //输出通道选择, ENO1P,ENO1N,ENO2P,ENO2N,ENO3P,ENO3N,ENO4P,ENO4N / ENO5P,ENO6P,ENO7P,ENO8P

//  PWM_Configuration(PWM8, &PWMx_InitStructure);           //初始化PWM,  PWMA,PWMB

    // 配置PWMB

    PWMx_InitStructure.PWM_Period   = PERIOD - 1;           //周期时间,   0~65535

    PWMx_InitStructure.PWM_DeadTime = 0;                    //死区发生器设置, 0~255

    PWMx_InitStructure.PWM_MainOutEnable= ENABLE;           //主输出使能, ENABLE,DISABLE

    PWMx_InitStructure.PWM_CEN_Enable   = ENABLE;           //使能计数器, ENABLE,DISABLE

    PWM_Configuration(PWMB, &PWMx_InitStructure);           //初始化PWM通用寄存器,  PWMA,PWMB

    // 切换PWM通道

//  PWM5_SW(PWM5_SW_P20);                   //PWM5_SW_P20,PWM5_SW_P17,PWM5_SW_P00,PWM5_SW_P74

    PWM6_SW(PWM6_SW_P01);                   //PWM6_SW_P21,PWM6_SW_P54,PWM6_SW_P01,PWM6_SW_P75

//  PWM7_SW(PWM7_SW_P22);                   //PWM7_SW_P22,PWM7_SW_P33,PWM7_SW_P02,PWM7_SW_P76

//  PWM8_SW(PWM8_SW_P23);                   //PWM8_SW_P23,PWM8_SW_P34,PWM8_SW_P03,PWM8_SW_P77

    // 初始化PWMB的中断

    NVIC_PWM_Init(PWMB,DISABLE,Priority_0);

}

void Oscilloscope_init(){
    EA = 1;
    EAXSFR();

    Oscilloscope_GPIO_config();
    PWM_config();

    MOTOR = 0;
}

PWMx_Duty duty;

float Motro_pwm_duty(u8* duty_percent){

    // 设置占空比

    duty.PWM6_Duty = PERIOD * (*duty_percent) / 100;

    UpdatePwm(PWM6, &duty);

        

    // 修改占空比 0 -> 100

    (*duty_percent)+=10;

    if(*duty_percent > 100){

      *duty_percent = 0;

    }

    return duty.PWM6_Duty;

}
