#ifndef __PWM_DRIVER_H
#define __PWM_DRIVER_H

#include "STC8H_PWM.h"
#include "Switch.h"
#include "NVIC.h"

/*
 * PWM 通用初始化配置结构体。
 *
 * Channel：
 *     PWM1 ~ PWM8，表示具体的 PWM 输出通道。
 *
 * Route：
 *     PWM 通道的引脚复用编号，填写对应 PWMx_SW_xxx 宏的值。
 *     例如 PWM5_SW_P00、PWM6_SW_P01。
 *
 * Mode：
 *     PWM 工作模式，例如 CCMRn_PWM_MODE1。
 *
 * OutputSelect：
 *     PWM 输出使能选择，例如 ENO5P、ENO6P。
 *
 * Period：
 *     PWM 自动重装载寄存器的值，通常等于实际周期计数值减 1。
 *
 * Duty：
 *     PWM 比较值，范围为 0 ~ Period。
 *
 * DeadTime：
 *     PWM 死区时间配置。普通单路 PWM 通常填写 0。
 *
 * CounterEnable：
 *     PWM 计数器是否启动，填写 ENABLE 或 DISABLE。
 *
 * MainOutputEnable：
 *     PWM 主输出是否使能，填写 ENABLE 或 DISABLE。
 *
 * InterruptState：
 *     PWM 中断使能配置。没有使用 PWM 中断时填写 DISABLE。
 *
 * Priority：
 *     PWM 中断优先级，填写 Priority_0 ~ Priority_3。
 */
typedef struct
{
    u8  Channel;
    u8  Route;
    u8  Mode;
    u8  OutputSelect;

    u16 Period;
    u16 Duty;

    u8  DeadTime;
    u8  CounterEnable;
    u8  MainOutputEnable;

    u8  InterruptState;
    u8  Priority;
} Pwm_InitTypeDef;

/*
 * 函数功能：初始化一个 PWM 通道及其所属的公共计数器。
 *
 * 参数：
 *     config - PWM 初始化配置结构体指针。
 *
 * 返回值：
 *     SUCCESS - 初始化成功。
 *     FAIL    - 配置指针、通道、路由或参数范围错误。
 *
 * 说明：
 *     PWM1 ~ PWM4 使用 PWMA，PWM5 ~ PWM8 使用 PWMB。
 *     该函数会先初始化具体通道，再初始化对应的公共计数器，
 *     最后设置引脚复用和 PWM 中断配置。
 */
u8 Pwm_Init(Pwm_InitTypeDef *config);

/*
 * 函数功能：修改指定 PWM 通道的占空比。
 *
 * 参数：
 *     channel - PWM1 ~ PWM8。
 *     duty    - 占空比比较值，不能大于当前公共计数器的 Period。
 *
 * 返回值：
 *     SUCCESS - 设置成功。
 *     FAIL    - PWM 通道编号无效。
 */
u8 Pwm_SetDuty(u8 channel, u16 duty);

/*
 * 函数功能：按照频率修改 PWMA 或 PWMB 的公共周期。
 *
 * 参数：
 *     timer     - PWMA 或 PWMB。
 *     frequency - 目标频率，单位 Hz。
 *
 * 返回值：
 *     SUCCESS - 设置成功。
 *     FAIL    - 定时器无效、频率为 0 或计算出的周期超出 16 位范围。
 *
 * 注意：
 *     同一个定时器下的多个 PWM 通道共用周期。
 *     例如 PWM5 和 PWM6 都属于 PWMB，修改 PWMB 频率会同时影响它们。
 */
u8 Pwm_SetFrequency(u8 timer, u32 frequency);

/*
 * 函数功能：打开指定 PWM 通道的比较输出。
 *
 * 参数：
 *     channel - PWM1 ~ PWM8。
 *
 * 返回值：
 *     SUCCESS - 打开成功。
 *     FAIL    - PWM 通道编号无效。
 */
u8 Pwm_Enable(u8 channel);

/*
 * 函数功能：关闭指定 PWM 通道的比较输出。
 *
 * 参数：
 *     channel - PWM1 ~ PWM8。
 *
 * 返回值：
 *     SUCCESS - 关闭成功。
 *     FAIL    - PWM 通道编号无效。
 */
u8 Pwm_Disable(u8 channel);

#endif
