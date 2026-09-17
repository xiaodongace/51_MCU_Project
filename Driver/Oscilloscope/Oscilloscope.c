#include "Oscilloscope.h"

static void Oscilloscope_GPIO_config(void) {
    GPIO_InitTypeDef    GPIO_InitStructure;

    /* 初始化P01为推挽 */
    GPIO_InitStructure.Pin  = GPIO_Pin_1;
    GPIO_InitStructure.Mode = GPIO_OUT_PP;
    GPIO_Inilize(GPIO_P0, &GPIO_InitStructure);

    /* 初始化P30 P31为准双向 */
    GPIO_InitStructure.Pin  = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStructure.Mode = GPIO_PullUp;
    GPIO_Inilize(GPIO_P3, &GPIO_InitStructure);
}

PWMx_Duty duty;

static void PWM_config(void)
{
    PWMx_InitDefine     PWMx_InitStructure;

    // 配置PWM6
    PWMx_InitStructure.PWM_Mode         = CCMRn_PWM_MODE1;
    PWMx_InitStructure.PWM_Duty         = duty.PWM6_Duty;
    PWMx_InitStructure.PWM_EnoSelect    = ENO6P;
    PWM_Configuration(PWM6, &PWMx_InitStructure);

    // 配置PWMB
    PWMx_InitStructure.PWM_Period   = PERIOD - 1;
    PWMx_InitStructure.PWM_DeadTime = 0;
    PWMx_InitStructure.PWM_MainOutEnable = ENABLE;
    PWMx_InitStructure.PWM_CEN_Enable    = ENABLE;
    PWM_Configuration(PWMB, &PWMx_InitStructure);

    // 切换PWM通道
    PWM6_SW(PWM6_SW_P01);

    // 初始化PWMB的中断
    NVIC_PWM_Init(PWMB, DISABLE, Priority_0);
}

void Oscilloscope_init(void){
    EA = 1;
    EAXSFR();
    Oscilloscope_GPIO_config();
    PWM_config();
}

u16 Motor_pwm_duty(u8* duty_percent){
    // 占空比计算公式：duty范围 0 ~ (PERIOD-1)，避免100%时溢出
    duty.PWM6_Duty = (PERIOD - 1) * (*duty_percent) / 100;

    UpdatePwm(PWM6, &duty);

    (*duty_percent) += 10;
    if(*duty_percent > 100){
        *duty_percent = 0;
    }

    return duty.PWM6_Duty;
}
