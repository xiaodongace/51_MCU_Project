/*
 * App_Clock.h - 时间服务（PCF8563 之上的薄封装）
 *
 * 职责：
 *   1) 初始化时钟芯片、每秒读一次时间
 *   2) 星期不依赖芯片内部的星期计数器 —— 用年月日在软件里算（Sakamoto 算法），
 *      这样即使用户没校准芯片的星期，显示和"周几响"也是对的
 *   3) 校时（只有在用户主动校时或第一次上电时才写芯片，
 *      因为芯片靠板上纽扣电池走时，每次开机重设会把时间冲掉 —— 《04》B 角色第 2 条）
 */
#ifndef __APP_CLOCK_H
#define __APP_CLOCK_H

#include "App_Public.h"

/* 初始化时钟芯片并读一次时间 */
void Clock_Init(void);

/* 读一次时间到 g_clock。返回 0 表示成功，负数为错误码 */
s8 Clock_Refresh(void);

/* 校时：把 t 写进芯片（星期由年月日算出，t 的 week 字段会被忽略/覆盖） */
void Clock_Set(Clock_t *t);

/* 时间是否已经读成功过 */
u8 Clock_IsValid(void);

/*
 * 时钟没读起来时的自救：每 5 秒尝试重设一次，最多 10 次。
 * 由 TASK_LOGIC 每秒调用。解决"USB 刚插上时芯片未稳定导致时间静止、
 * 必须按重启按钮才恢复"的问题。
 */
void Clock_TryRecover(void);

/* 真正执行者（由 TASK_RENDER 调用，不要在别处调用） */
void Clock_SetNow(Clock_t *t);
void Clock_TryRecoverNow(void);

/* 由年月日算星期。返回 0=周日, 1=周一, ... 6=周六 */
u8 Clock_WeekdayOf(u16 year, u8 month, u8 day);

/* 当前星期对应到本工程的 DAY_xxx 掩码（单天） */
u8 Clock_TodayMask(void);

/* 把 DAY_xxx 掩码里的某一位转成 PCF8563 的星期值（0=周日..6=周六）。
 * 传入的位序号 days 必须是"单天"掩码，否则返回 DAY_INVALID */
u8 Clock_MaskToWeek(u8 mask);

/* 某年某月的天数（用于日期加减与合法性检查） */
u8 Clock_DaysInMonth(u16 year, u8 month);

/* 把 time 往后推 1 秒（用于番茄钟、贪睡这类"相对时间"推算） */
void Clock_AddSeconds(Clock_t *t, u16 sec);

/* 把 24 小时制时分转成"当天第几分钟" */
u16 Clock_MinutesOfDay(u8 hour, u8 minute);

#endif
