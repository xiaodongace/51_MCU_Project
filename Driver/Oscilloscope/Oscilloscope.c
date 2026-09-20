#include "Pwm.h"
#include "Oscilloscope.h"

static void Oscilloscope_GPIO_config(void) {
    GPIO_InitTypeDef    GPIO_InitStructure;

    /* ?????P01????? */
    GPIO_InitStructure.Pin  = GPIO_Pin_1;
    GPIO_InitStructure.Mode = GPIO_OUT_PP;
    GPIO_Inilize(GPIO_P0, &GPIO_InitStructure);

    /* ?????P30 P31????? */
    GPIO_InitStructure.Pin  = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStructure.Mode = GPIO_PullUp;
    GPIO_Inilize(GPIO_P3, &GPIO_InitStructure);
}


static void Oscilloscope_PWM_Init(void)
{
    Pwm_InitTypeDef pwmConfig;

    pwmConfig.Channel          = PWM6;
    pwmConfig.Route            = PWM6_SW_P01;
    pwmConfig.Mode             = CCMRn_PWM_MODE1;
    pwmConfig.OutputSelect     = ENO6P;
    pwmConfig.Period           = PERIOD - 1;
    pwmConfig.Duty             = 0;
    pwmConfig.DeadTime         = 0;
    pwmConfig.CounterEnable    = ENABLE;
    pwmConfig.MainOutputEnable = ENABLE;
    pwmConfig.InterruptState   = DISABLE;
    pwmConfig.Priority         = Priority_0;

    Pwm_Init(&pwmConfig);
}

void Oscilloscope_init(void)
{
    Oscilloscope_GPIO_config();
    Oscilloscope_PWM_Init();
}

void Motor_pwm_duty(u8* duty_percent)
{
    u16 dutyValue;

    dutyValue = (u16)((u32)(PERIOD - 1) * (*duty_percent) / 100UL);
    Pwm_SetDuty(PWM6, dutyValue);

    (*duty_percent) += 10;
    if (*duty_percent > 30)
    {
        *duty_percent = 0;
    }
}
