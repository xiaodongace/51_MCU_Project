/*
 * Servo.h - 舵机（主屏任务清单里的"云台"）
 *
 * 真值来源：
 *   《02-技术方案》58 行：`| 舵机 | P2.5 | 硬件 PWM 输出 |`
 *   《02-技术方案》82 行：`舵机在 A 组，独占 50Hz，不受影响——查过 A 组没有其他 PWM 用户。`
 *   Lib/STC8H_PWM.h:67 ：`PWM3_USE_P24P25()`  => PWM3P = P2.4、PWM3N = P2.5
 *   参考代码 `day05/Code/04_舵机_PWM驱动_文件封装/Servo.c`（角度->脉宽映射公式）
 *
 * 【为什么不用参考工程的 PWM7 / P0.2】
 *   参考工程用 PWM7，它在 **PWMB 组**，与蜂鸣器(PWM5)、马达(PWM6)
 *   共用同一个周期寄存器 PWMB_ARR；而蜂鸣器每次换音都要重写它。
 *   本工程页面切换就有一声 40ms 提示音（Music_Beep），
 *   所以"舵机和蜂鸣器不同时使用"这个前提**不成立** ——
 *   一进云台页按一下键，蜂鸣器一响，PWMB 的周期就变成 kHz 级，舵机的 50Hz 当场丢失。
 *   PWM3 属 **PWMA 组**，ARR/PSCR 是两组独立寄存器，50Hz 和 1kHz 可以共存。
 *
 * 【为什么只使能 N 通道】
 *   P2.4（PWM3P）是超声波的 TRIG，不能让给 PWM。
 *   Lib/STC8H_PWM.c 的 PWM_Configuration() 里 P/N 是**各自独立判断**的
 *   （`if (EnoSelect & ENO3P) ...` / `if (EnoSelect & ENO3N) ...`），
 *   所以只填 ENO3N 就能只输出 N 通道，P2.4 保持普通 IO 不动。
 *   副作用：N 通道相位与 P 相反，对舵机只是 0°/180° 方向互换。
 *
 * 【安全】上电**不**初始化；只有进"云台"页才 Servo_Init()，
 *         离开时 Servo_Off() 关掉输出 —— 免得舵机一直有信号在那儿抖。
 */
#ifndef __SERVO_H
#define __SERVO_H

#include "Config.h"

/* 进"云台"页时调一次（可重复调用，幂等） */
/* 【上电安全电平】必须在 sys_init() 最前面调用，和马达 P0.1、蜂鸣器 P0.0 同一批。
 * 作用：把 P2.5 先按到低电平，避免"开机到第一次进云台"这段时间里
 *       舵机信号线一直是高、被舵机当成超长脉宽而甩到极限位置。 */
void Servo_SafeLevel(void);

void Servo_Init(void);

/* 离开"云台"页时调：关掉 PWM 输出，舵机不再受力 */
void Servo_Off(void);

/* 设置角度，参数 0~180（超出会被夹住） */
void Servo_SetAngle(u8 angle);

/* 当前角度 */
u8 Servo_GetAngle(void);

#endif
