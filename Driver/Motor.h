/*
 * Motor.h - 震动马达（P0.1，PWMB / PWM6）
 *
 * 真值来源：
 *   v3.1 App/App_Motor.c：PWM6 + PWM_Configuration(PWMB,...) + PWM6_SW(PWM6_SW_P01)
 *   《02-技术方案》3.2：蜂鸣器与马达同属 PWMB 组，共用频率
 *   《AI项目生成规范》示例表：马达 P0.1，P01 = 0 不工作（高电平震）
 *
 * 关键安全点：上电复位后 P0.1 是准双向口且输出寄存器为 1 = 高电平 = 马达震。
 *            必须由 Motor_SafeLevel() 在 sys_init() 最前面把它按到低电平。
 */
#ifndef __MOTOR_H
#define __MOTOR_H

#include "Config.h"
#include "STC8H_PWM.h"

sbit MOTOR = P0 ^ 1;

/*
 * 马达总开关。
 * 0 = Motor_On() 不驱动马达（只打日志）。
 *
 * 【2026-09-19 加此开关的原因】
 * 真机现象：项目代码连按按键多次 → I2C 屏花屏（全竖线）+ **蜂鸣器位置发出"嗡嗡嗡"声**，
 * 而蜂鸣器本身是全局静音的（BUZZER_ENABLE 0）—— 那个声音只可能是**震动马达在转**。
 * 震动马达是百毫安级负载，会把 3.3V 拉低，OLED 模块欠压后就花屏/黑屏，
 * **而且重启救不回来（模块内部锁死），只有拔插 USB 断电才恢复** —— 现象完全吻合。
 * 对照：demo30 狂按按键 90 秒完全正常，因为它的马达只由电位器主动驱动，不会自己启动。
 * 先用这个开关切掉最大的电流源做验证。
 */
#define MOTOR_ENABLE    1    /* 【第 7 点】用户要求开启马达 */

/* 马达单独工作时的频率：1 kHz（照抄 v3.1 App_Motor.c 的 PREQ = 1000） */
#define MOTOR_PREQ      1000
#define MOTOR_PERIOD    (MAIN_Fosc / MOTOR_PREQ)

/* 上电安全电平：只写 IO，可在 EAXSFR() 之前调用 */
void Motor_SafeLevel(void);

/* 初始化：配推挽 + 配 PWM6（占空比 0）+ 切到 P0.1 + 关 PWMB 中断 */
void Motor_Init(void);

/*
 * 让马达持续震动。
 *   level —— 震动强度 0..100（第 7 点新增）。
 *            占空比 = MOTOR_PERIOD * level / 100，所以 **50 就是原来的行为**
 *            （原来写死 PERIOD/2 = 50%）。level = 0 表示不振。
 *
 * 【第 7 点补漏】原来这里没有强度参数、占空比写死 50%，
 * 于是设置页里的"震动强度"改了也**没有任何效果** —— 设了等于没设。
 * 现在强度由调用方（App 层）把 g_settings.vibrate 传进来，
 * 驱动层只做机制、不做策略。
 *
 * 副作用：会先停掉蜂鸣器——两者同属 PWMB，共用周期寄存器，
 *        不先停蜂鸣器就会出现"马达按音符频率震"。见《02》3.2。
 */
void Motor_On(u8 level);

/* 停止震动（关闭 PWM6 输出使能，引脚回到低电平） */
void Motor_Off(void);

/*
 * 震动 ms 毫秒后**自动停**（非阻塞）。
 *
 * 用户第 7 点要求：按 KEY2 确认后"马达或蜂鸣器给出**持续 100ms** 的反馈"。
 * 蜂鸣器那边有 Music_Beep(ms) 这个现成的非阻塞接口，马达这边没有，
 * 所以在这里补一个同款：
 *   Motor_Vibrate(100, g_settings.vibrate);   // 震 100ms，强度取设置值
 *
 * 实现是"数拍子"而不是记时间戳 —— 这样驱动层不必依赖 App 层的 SysTick。
 * 代价是**必须由 5ms 周期的 Motor_Tick() 来喂**（见 Motor_Tick 的说明）。
 */
void Motor_Vibrate(u16 ms, u8 level);

/*
 * 震动计时的心跳。**必须在 5ms 周期的任务里调用**
 * （本工程挂在 TASK_MUSIC 的 while 循环里，和 Music_Tick 并排）。
 * 这个 5ms 是 Motor_Vibrate 换算拍数的基准，改了 TASK_MUSIC 的周期
 * 就要同时改这里，否则"100ms 反馈"会不准。
 */
void Motor_Tick(void);

#endif
