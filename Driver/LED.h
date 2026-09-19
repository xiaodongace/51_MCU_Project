/*
 * LED.h - 8 颗小灯 + 总开关
 *
 * 真值来源：v3.1 App/App_LED.c（真机跑通的自检固件）
 *   总开关 LED_SW = P4.5，低电平导通（LED_SW = 0 表示打开）
 *   灯   LED1..LED8 = P2.7 P2.6 P1.5 P1.4 P2.3 P2.2 P2.1 P2.0，低电平点亮
 *   全部输出推挽
 */
#ifndef __LED_H
#define __LED_H

#include "Config.h"

sbit LED_SW = P4 ^ 5;
sbit LED1   = P2 ^ 7;
sbit LED2   = P2 ^ 6;
sbit LED3   = P1 ^ 5;
sbit LED4   = P1 ^ 4;
sbit LED5   = P2 ^ 3;
sbit LED6   = P2 ^ 2;
sbit LED7   = P2 ^ 1;
sbit LED8   = P2 ^ 0;

#define LED_COUNT   8

/* 上电安全电平：总开关关掉（P45=1 表示关闭），8 颗灯全灭。
 * 必须在 sys_init() 最前面调用，否则上电瞬间灯是随机状态。 */
void Led_SafeLevel(void);

/* 初始化：配推挽 + 关总开关 + 全灭 */
void Led_Init(void);

/* 全部熄灭 */
void Led_AllOff(void);

/* 全部点亮 */
void Led_AllOn(void);

/* 打开/关闭总开关（用于统一调亮度/省电） */
void Led_Power(u8 on);

/*
 * 立即点亮/熄灭第 idx 颗灯（idx = 0..7）。
 * 与 Led_SetMask 的区别：本函数直接写 IO 口，不经过任何任务刷新，
 * 因此可以用作"上电到哪一步"的诊断手段（规范第四节要求诊断输出不依赖被诊断对象）。
 */
void Led_SetSingle(u8 idx, u8 on);

/*
 * 按掩码点亮：mask bit0..bit7 分别对应 LED1..LED8，1 = 亮。
 * 立即写 IO 口，不排队。
 */
void Led_SetMask(u8 mask);

/*
 * 日出唤醒：level 0..255 表示亮度等级。
 *
 * 注意（本工程的设计取舍）：8 颗灯全部是普通 GPIO，没有独立 PWM 通道
 * （LED 引脚的 PWM 备选通道属于 PWMA/PWMB，已被舵机/蜂鸣器/马达占用）。
 * 所以"越来越亮"用"一颗接着一颗点亮"来实现——这是视觉上可行的近似。
 */
void Led_Breath(u8 level);

/* 查询当前点亮的颗数（供调试/显示用） */
u8 Led_GetCount(void);

#endif
