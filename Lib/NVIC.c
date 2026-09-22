/*---------------------------------------------------------------------*/
/* --- 本项目裁剪说明（2026-09-22）------------------------------------*/
/*                                                                     */
/* 本文件来自 STC 官方库，原始版本 33 个 NVIC 中断初始化函数。          */
/* C51 是**模块级链接**：只要 NVIC.c 在工程里，里面所有函数都会占 Flash，*/
/* 哪怕一个都没被调用。所以这里只保留本工程真正用到的 7 个：            */
/*                                                                     */
/*   NVIC_PWM_Init        —— 舵机云台 PWM（PWMB 通道）                  */
/*   NVIC_Timer2_Init     —— 数码管 1kHz 动态扫描                       */
/*   NVIC_Timer3_Init     —— 系统 1ms 节拍                              */
/*   NVIC_I2C_Init        —— 副屏（I2C OLED）+ PCF8563                  */
/*   NVIC_UART1_Init      —— 上位机通信（P3.0/P3.1 经 CH340）           */
/*   NVIC_INT3_Init       —— PCF8563 闹钟中断                           */
/*   NVIC_ADC_Init        —— NTC 热敏电阻测温                           */
/*                                                                     */
/* 其余 26 个（Timer0/1/4、INT0/1/2/4、CMP、UART2/3/4、SPI、RTC、       */
/* DMA x12、LCM x2）全工程零引用，定义与声明都已删除，省约 1.3KB Flash。*/
/* 以后需要时，从 STC 官方库对应版本拷回函数体即可（函数名/签名未改）。 */
/*---------------------------------------------------------------------*/

/*---------------------------------------------------------------------*/
/* --- STC MCU Limited ------------------------------------------------*/
/* --- STC 1T Series MCU Demo Programme -------------------------------*/
/* --- Mobile: (86)13922805190 ----------------------------------------*/
/* --- Fax: 86-0513-55012956,55012947,55012969 ------------------------*/
/* --- Tel: 86-0513-55012928,55012929,55012966 ------------------------*/
/* --- Web: www.STCAI.com ---------------------------------------------*/
/* --- BBS: www.STCAIMCU.com  -----------------------------------------*/
/* --- QQ:  800003751 -------------------------------------------------*/
/* 如果要在程序中使用此代码,请在程序中注明使用了STC的资料及程序            */
/*---------------------------------------------------------------------*/

#include	"NVIC.h"



//========================================================================
// 函数: NVIC_Timer2_Init
// 描述: Timer2嵌套向量中断控制器初始化.
// 参数: State:    中断使能状态, ENABLE/DISABLE.
// 参数: Priority: 中断优先级, NULL.
// 返回: 执行结果 SUCCESS/FAIL.
// 版本: V1.0, 2020-09-29
//========================================================================
u8 NVIC_Timer2_Init(u8 State, u8 Priority)
{
	if(State > ENABLE) return FAIL;
	if(Priority > Priority_3) return FAIL;
	Timer2_Interrupt(State);
	Priority = NULL;
	return SUCCESS;
}

//========================================================================
// 函数: NVIC_Timer3_Init
// 描述: Timer3嵌套向量中断控制器初始化.
// 参数: State:    中断使能状态, ENABLE/DISABLE.
// 参数: Priority: 中断优先级, NULL.
// 返回: 执行结果 SUCCESS/FAIL.
// 版本: V1.0, 2020-09-29
//========================================================================
u8 NVIC_Timer3_Init(u8 State, u8 Priority)
{
	if(State > ENABLE) return FAIL;
	if(Priority > Priority_3) return FAIL;
	Timer3_Interrupt(State);
	Priority = NULL;
	return SUCCESS;
}





//========================================================================
// 函数: NVIC_INT3_Init
// 描述: INT3嵌套向量中断控制器初始化.
// 参数: State:    中断使能状态, ENABLE/DISABLE.
// 参数: Priority: 中断优先级, NULL.
// 返回: 执行结果 SUCCESS/FAIL.
// 版本: V1.0, 2020-09-29
//========================================================================
u8 NVIC_INT3_Init(u8 State, u8 Priority)
{
	if(State > ENABLE) return FAIL;
	INT3_Interrupt(State);
	Priority = NULL;
	return SUCCESS;
}


//========================================================================
// 函数: NVIC_ADC_Init
// 描述: ADC嵌套向量中断控制器初始化.
// 参数: State:    中断使能状态, ENABLE/DISABLE.
// 参数: Priority: 中断优先级, Priority_0,Priority_1,Priority_2,Priority_3.
// 返回: 执行结果 SUCCESS/FAIL.
// 版本: V1.0, 2020-09-29
//========================================================================
u8 NVIC_ADC_Init(u8 State, u8 Priority)
{
	if(State > ENABLE) return FAIL;
	if(Priority > Priority_3) return FAIL;
	ADC_Interrupt(State);
	ADC_Priority(Priority);
	return SUCCESS;
}


//========================================================================
// 函数: NVIC_I2C_Init
// 描述: I2C嵌套向量中断控制器初始化.
// 参数: Mode:     模式, I2C_Mode_Master/I2C_Mode_Slave.
// 参数: State:    中断使能状态, I2C_Mode_Master: ENABLE/DISABLE.
//                              I2C_Mode_Slave: I2C_ESTAI/I2C_ERXI/I2C_ETXI/I2C_ESTOI/DISABLE.
// 参数: Priority: 中断优先级, Priority_0,Priority_1,Priority_2,Priority_3.
// 返回: 执行结果 SUCCESS/FAIL.
// 版本: V1.0, 2020-09-29
//========================================================================
u8 NVIC_I2C_Init(u8 Mode, u8 State, u8 Priority)
{
	if(Mode > I2C_Mode_Master) return FAIL;
	if(Priority > Priority_3) return FAIL;
	if(Mode == I2C_Mode_Master)
	{
		I2C_Master_Inturrupt(State);
	}
	else if(Mode == I2C_Mode_Slave)
	{
		I2CSLCR = (I2CSLCR & ~0x78) | State;
	}
	CMP_Priority(Priority);
	return SUCCESS;
}

//========================================================================
// 函数: NVIC_UART1_Init
// 描述: UART1嵌套向量中断控制器初始化.
// 参数: State:    中断使能状态, ENABLE/DISABLE.
// 参数: Priority: 中断优先级, Priority_0,Priority_1,Priority_2,Priority_3.
// 返回: 执行结果 SUCCESS/FAIL.
// 版本: V1.0, 2020-09-29
//========================================================================
u8 NVIC_UART1_Init(u8 State, u8 Priority)
{
	if(State > ENABLE) return FAIL;
	if(Priority > Priority_3) return FAIL;
	UART1_Interrupt(State);
	UART1_Priority(Priority);
	return SUCCESS;
}





//========================================================================
// 函数: NVIC_PWM_Init
// 描述: PWM嵌套向量中断控制器初始化.
// 参数: Channel:  通道, PWMA/PWMB.
// 参数: State:    中断使能状态, PWM_BIE/PWM_TIE/PWM_COMIE/PWM_CC8IE~PWM_CC1IE/PWM_UIE/DISABLE.
// 参数: Priority: 中断优先级, Priority_0,Priority_1,Priority_2,Priority_3.
// 返回: 执行结果 SUCCESS/FAIL.
// 版本: V1.0, 2020-09-29
//========================================================================
u8 NVIC_PWM_Init(u8 Channel, u8 State, u8 Priority)
{
	if(Channel > PWMB) return FAIL;
	if(Priority > Priority_3) return FAIL;
	switch(Channel)
	{
		case PWMA:
			PWMA_IER = State;
			PWMA_Priority(Priority);
		break;

		case PWMB:
			PWMB_IER = State;
			PWMB_Priority(Priority);
		break;

		default:
			PWMB_IER = State;
			Priority = NULL;
		break;
	}
	return SUCCESS;
}














