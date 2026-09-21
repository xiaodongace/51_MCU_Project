#include "Pwm.h"
#include "Oscilloscope.h"
#include "I2C_OLED.h"

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

u8 clear_screen = 1;
u8 duty_percent = 0;

void Clear_screen(u8 is_go_clear){
    if(is_go_clear==1)
        clear_screen=1;
    os_send_signal(5);
}

void show_Oscilloscope(void) {
    char str[24];
    sprintf(str, "duty=%d%%",(int)duty_percent);
    I2C_OLED_ShowString(0, 2, str, 16);
}