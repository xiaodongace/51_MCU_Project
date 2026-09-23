/*
 * App_Alarm.h - 闹钟（ALARM_MAX 组，当前 3 组）：数据、调度、响铃
 *
 * 这是全项目唯一的"真算法"（《04-M1分工规格》B 角色点名）。
 *
 * 难点：PCF8563 硬件只有**一套**闹钟寄存器，而我们要用 ALARM_MAX 组，
 *       而且每组还要支持"周几响"（例如只在工作日响）。
 *       更麻烦的是：芯片的"周"寄存器只能填**一个**星期值，写不进掩码。
 *
 * 本工程用两层配合解决：
 *
 *   第一层（硬件）：每次改动或响铃结束后，从 ALARM_MAX 组里挑出"时间上最近、还没响的那一组"，
 *                   把它的 (分, 时) 写进芯片；
 *                   · 若这组是"每天响"  -> 周字段禁用，芯片每天同一时刻触发
 *                   · 若这组是"指定周几" -> 周字段填那一天，芯片只在那天触发
 *                   这样"到点叫醒程序"这条链路（P3.7 + INT3 + 下降沿）是真实在用的，
 *                   不是摆设。
 *
 *   第二层（软件）：TASK_LOGIC 每秒把当前 时:分 和每一组比一遍，命中且今天该响、
 *                   这一分钟还没响过 -> 补触发。
 *                   这一层是真正的兜底：即使硬件闹钟因为星期计数、温漂、
 *                   纽扣电池掉电等原因漏触发，也不会漏响。
 *
 *   两层都靠"同一条记录过的分钟戳" g_lastRingWom 去重，所以不会重复响。
 */
#ifndef __APP_ALARM_H
#define __APP_ALARM_H

#include "App_Public.h"

/* 响铃后无人理会，自动静音（秒）。只是防止无限响，界面仍停在响铃页。 */
#define ALARM_AUTO_STOP_SEC     120

/* 日出唤醒：响铃前这么多秒开始渐亮（《01》要求提前 10 分钟） */
#define ALARM_SUNRISE_SEC       600

/* 初始化：清状态、算出下一次、写硬件闹钟 */
void Alarm_Init(void);

/*
 * 把"时间上最近的、启用的那一组"写进 PCF8563 的硬件闹钟寄存器。
 * 改动闹钟、校时、响铃结束之后都要调用一次。
 */
void Alarm_ApplyHardware(void);

/* 真正执行者（由 TASK_RENDER 调用，不要在别处调用） */
void Alarm_ApplyHardwareNow(void);

/*
 * 处理 PCF8563 的中断标志。
 * 由 TASK_LOGIC 在检测到 g_rtcIrqFlag 后调用 —— 中断里只置了标志，
 * I2C 清标志的动作放在任务里做，因为 I2C 总线要和副屏共用。
 */
void Alarm_OnRtcIrq(void);

/*
 * 每秒调用一次。返回 1 表示这一秒起闹了（界面需要切到响铃页）。
 * 里面同时处理：软件兜底比对、贪睡倒计时、响铃超时自动静音。
 */
u8 Alarm_Tick1s(void);

/* 起闹（一般不用直接调，由 Tick1s 触发） */
void Alarm_Start(u8 idx);

/* 用户按"停止" */
void Alarm_Stop(void);

/* 用户按"贪睡"：静音 s_settings.snooze_min 分钟后再响 */
void Alarm_Snooze(void);

u8 Alarm_IsRinging(void);
u8 Alarm_RingingIndex(void);        /* 正在响的闹钟下标；无效返回 IDX_NONE */

/* 距离下一次响铃还有多少秒；-1 表示当前没有任何待响闹钟 */
s32 Alarm_SecondsToNext(void);

/* 贪睡剩余秒数，0 表示没有在贪睡 */
u16 Alarm_SnoozeRemainSec(void);

/*
 * 日出唤醒：每秒调用一次。根据"距下一次响铃还有多久"决定 8 颗小灯的亮度。
 * 与响铃无关的时刻会把灯全灭（写 IO 立即生效，不依赖任务刷新）。
 */
void Alarm_TickSunrise(void);

/* ---------------- 编辑接口：改完自动存盘 + 重算硬件闹钟 ---------------- */

u8 Alarm_Set(u8 idx, const AlarmItem_t *a);    /* 覆盖第 idx 组 */
u8 Alarm_Delete(u8 idx);                       /* 关掉并复位第 idx 组 */
u8 Alarm_Toggle(u8 idx);                       /* 开关取反 */
u8 Alarm_Add(const AlarmItem_t *a);            /* 找一个空位放进去，返回下标；满了返回 IDX_NONE */

/* 用到 PCF8563 驱动声明的两个回调（PCF8563_int_call 里会调） */
void PCF8563_on_alarm(void);
void PCF8563_on_timer(void);

#endif
