/*
 * App_System.h - 系统初始化
 *
 * 说明：五个任务的入口函数不需要对外声明。
 *  RTX51 Tiny 用 os_create_task(任务号) 创建任务，任务函数本身的符号
 *  由 _task_ 定义生成，main.c 只传任务号，不做函数调用，
 *  所以这里不放任务原型（也就不必纠结 _task_ 属性写不写进原型里）。
 */
#ifndef __APP_SYSTEM_H
#define __APP_SYSTEM_H

#include "App_Public.h"

/*
 * 系统初始化。由 TASK_MAIN 调用，顺序严格按下面来：
 *   0) 先把"默认电平危险"的引脚按到安全电平（马达 / 蜂鸣器 / 小灯总开关）
 *   1) EAXSFR() 扩展寄存器访问使能
 *   2) 各外设配置（UART / I2C / ADC / PWM）
 *   3) 最后 EA = 1 开全局中断
 */
void sys_init(void);

#endif
