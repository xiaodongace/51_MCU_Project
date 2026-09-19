/*
 * I2C_Lock.h - I2C 总线互斥（"走廊通行权"）
 *
 * 背景（《02-技术方案》3.3）：副屏 OLED 和时钟芯片 PCF8563 共用 P3.2/P3.3 一对线。
 * 两个不同的任务都会去访问这条总线：
 *   TASK_RENDER 刷副屏；TASK_LOGIC 读时间 / 清闹钟标志。
 * 如果两边同时说话就乱了。
 *
 * 做法：谁用谁举手 —— 访问前 I2C_Lock()，用完 I2C_Unlock()。
 *   · 上锁用"读标志 + 置标志 + 关中断"三步，保证测试与置位之间不被打断；
 *   · 拿不到锁的一方不硬等，而是 os_wait2 让出 CPU 稍后重试（不阻塞别的任务）；
 *   · 持锁期间不做长时间操作：本工程把副屏刷新按"行"粒度做，
 *     单行约 5.7ms，短于 RTX51 的 25ms 时间片，所以不会被中途换出去。
 */
#ifndef __I2C_LOCK_H
#define __I2C_LOCK_H

#include "Config.h"

/* 申请总线。拿不到就让出 CPU 重试，直到拿到为止 */
void I2C_Lock(void);

/*
 * 尝试申请一次，不等待。
 * 返回 1 = 拿到；0 = 别人正在用（调用方自己决定稍后再试）。
 */
u8 I2C_TryLock(void);

/* 释放总线 */
void I2C_Unlock(void);

/* 当前是否被占用（调试用） */
u8 I2C_IsLocked(void);

#endif
