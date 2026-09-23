/*
 * App_Alarm.c - 闹钟（ALARM_MAX 组，当前 3 组）的调度与响铃
 *
 * 调度把"周几 + 时分"统一折算成**周一 00:00 起的分钟序号**（week-of-minute），
 * 这样比较"谁先响"就只是一次整数比较，不需要处理跨月、跨年、闰年。
 *
 *   wom(星期i, 时h, 分m) = i * 1440 + h * 60 + m
 *   i: 0=周一 … 6=周日
 *
 * "下一次"的算法：对每一组、每一个被选中的星期，
 *   算出它在本周内的 wom，如果已经过去就加一周（10080 分钟），取全场最小。
 */
#include "App_Alarm.h"

#include "PCF8563.h"
#include "App_Clock.h"
#include "App_Storage.h"
#include "App_Music.h"
#include "Motor.h"
#include "LED.h"
#include "I2C_Lock.h"

#define MINUTES_PER_WEEK    (7U * 1440U)    /* 10080 */

/*========================================================================
 *                              内部状态
 *========================================================================*/

/* 每一组"最后一次响过的分钟序号"，用来去重（两层触发都看它）
 * 存的是 wom（0..10079），所以"同一分钟只响一次"天然成立，跨天也自动复位 */
static u16 s_lastRingWom[ALARM_MAX];

/* 正在响的组（下标）；IDX_NONE 表示没在响 */
static u8 s_ringIdx = IDX_NONE;

/* 响铃已持续秒数（用于自动静音） */
static u16 s_ringSec = 0;

/* 贪睡：剩余秒数 + 贪睡的是哪一组 */
static u16 s_snoozeSec = 0;
static u8  s_snoozeIdx = IDX_NONE;

/* 硬件闹钟每次触发的计数（调试用，串口打印可以看到"门铃"真的响了） */
static u8 s_hwAlarmHit = 0;

/* 上一次设置的日出亮度，避免每秒都写 IO */
static u8 s_lastSunrise = 0;

/*========================================================================
 *                          PCF8563 驱动回调
 *
 * PCF8563_int_call() 在清掉闹钟标志后调用这两个函数。
 * 它们必须存在，否则链接会报 UNRESOLVED（PCF8563.h 里是 extern 声明）。
 *========================================================================*/

void PCF8563_on_alarm(void)
{
    /* 这里已经处在任务上下文（由 Alarm_OnRtcIrq 调过来的），不在中断里，
     * 所以printf 是安全的。但为了不拖慢调度，只累加一个计数。 */
    s_hwAlarmHit++;
}

void PCF8563_on_timer(void)
{
    /* 本工程不使用 PCF8563 的内部定时器（讲义原话：HZ1 那档实测不稳定，
     * 而我们需要的高精度计时由 Timer3 的 1ms 系统时钟负责）。留空实现。 */
}

/*========================================================================
 *                            时间折算小工具
 *========================================================================*/

/* g_clock.week 是 0=周日..6=周六；本函数转成 0=周一..6=周日 */
static u8 monday_index(void)
{
    if (g_clock.week > 6)
    {
        return 0;
    }

    return (u8)((g_clock.week + 6) % 7);
}

static u16 now_wom(void)
{
    return (u16)((u16)monday_index() * 1440U +
                 (u16)g_clock.hour * 60U +
                 (u16)g_clock.minute);
}

/* 某一组在"本周/下周"里、对应第 dayBit 个星期（0=周一）的那一次是第几分钟 */
static u16 wom_of(u8 dayIdx, u8 hour, u8 minute, u8 fromMonIdx)
{
    u16 wom;

    wom = (u16)(((u16)((dayIdx + 7U - fromMonIdx) % 7U) * 1440U) +
                (u16)hour * 60U + (u16)minute);

    if (wom < now_wom())
    {
        wom = (u16)(wom + MINUTES_PER_WEEK);
    }

    return wom;
}

/*
 * 找出"时间上最近的、启用的"那一组。
 * 返回 1 并填 outIdx / outDayBit（该组的哪个星期位）/ outWom；没有则返回 0。
 */
static u8 find_next(u8 *outIdx, u8 *outDayBit, u16 *outWom)
{
    u8  i;
    u8  k;
    u8  found = 0;
    u16 best = 0xFFFFU;
    u8  from = monday_index();

    for (i = 0; i < ALARM_MAX; i++)
    {
        if (!g_alarms[i].enable)
        {
            continue;
        }
        if (g_alarms[i].days == DAY_NONE)
        {
            continue;
        }
        if ((g_alarms[i].days & (u8)(~DAY_ALL)) != 0)
        {
            continue;       /* 脏数据，跳过 */
        }
        if (g_alarms[i].hour > 23 || g_alarms[i].minute > 59)
        {
            continue;
        }

        for (k = 0; k < 7; k++)
        {
            u16 w;

            if (((g_alarms[i].days >> k) & 1) == 0)
            {
                continue;
            }

            w = wom_of(k, g_alarms[i].hour, g_alarms[i].minute, from);

            if (w < best)
            {
                best      = w;
                *outIdx   = i;
                *outDayBit = k;
                found     = 1;
            }
        }
    }

    if (found)
    {
        *outWom = best;
    }

    return found;
}

/*========================================================================
 *                          写入硬件闹钟
 *========================================================================*/

/*
 * 【方案 B】Alarm_ApplyHardware() 改为"只登记请求"，真正的 I2C 写由 TASK_RENDER 执行。
 * 这样所有 I2C 访问都在同一个任务里串行，不再需要总线锁 —— 见 App_Public.h 的说明。
 */
void Alarm_ApplyHardware(void)
{
    g_reqAlarmApply = 1;
}

void Alarm_ApplyHardwareNow(void)
{
    u8  idx = IDX_NONE;
    u8  dayBit = 0;
    u16 wom = 0;
    Alarm_t a;

    /* 写硬件闹钟是 2 个 I2C 事务（写闹钟寄存器 + 改控制寄存器），
     * 中途不能被副屏刷新插进来，所以整段拿总线锁。 */
    I2C_Lock();

    if (!find_next(&idx, &dayBit, &wom))
    {
        /* 没有待响闹钟：把硬件闹钟全部禁用 */
        a.minute = -1;
        a.hour   = -1;
        a.day    = -1;
        a.week   = -1;
        PCF8563_set_alarm(a);
        PCF8563_enable_alarm(DISABLE);
        I2C_Unlock();
        return;
    }

    a.minute = (s8)g_alarms[idx].minute;
    a.hour   = (s8)g_alarms[idx].hour;
    a.day    = -1;      /* 不用"日"匹配，统一用"周" / 或全禁 */

    if (g_alarms[idx].days == DAY_ALL)
    {
        /* 每天都要响：周字段禁用，芯片每天同一时刻触发 */
        a.week = -1;
    }
    else
    {
        /* 只在指定的星期响：填那一天（0=周日..6=周六 是芯片的约定） */
        a.week = (s8)Clock_MaskToWeek((u8)(1U << dayBit));
    }

    PCF8563_set_alarm(a);
    PCF8563_enable_alarm(ENABLE);

    I2C_Unlock();
}

/*========================================================================
 *                              中断处理
 *========================================================================*/

void Alarm_OnRtcIrq(void)
{
    /* 中断里只置了标志，这里是任务上下文，可以安全地走 I2C。
     * PCF8563_int_call() 会：读 CS2 -> 判断 AF/AIE -> 调 PCF8563_on_alarm() -> 写回清标志。
     * 必须清标志，否则 P3.7 会一直保持低电平（《04》B 角色注意事项）。
     * 这 2 个 I2C 事务同样要和副屏刷新互斥。 */
    /* 【方案 B】现在只有 TASK_RENDER 会调本函数，全工程 I2C 串行，不再需要显式加锁。
     * （I2C_Lock 保留但永不争用，作为将来改动的安全网。） */
    PCF8563_int_call();
}

/*========================================================================
 *                              起闹 / 停止
 *========================================================================*/

void Alarm_Start(u8 idx)
{
    u8 song;
    u8 vol;

    if (idx >= ALARM_MAX)
    {
        return;
    }

    s_ringIdx = idx;
    s_ringSec = 0;
    g_ringingIdx = (u8)(idx + 1);       /* 非 0 表示正在响 */

    /* 曲目：组里指定了就用组里的，0 表示跟随全局设置 */
    song = g_alarms[idx].song;
    if (song == 0)
    {
        song = (u8)(g_settings.song + 1);
    }

    vol = g_settings.volume;

    if (g_settings.alert_mode == ALERT_VIBRATE)
    {
        /* 静音闹钟：不发声，用马达震（《01》2.3 的"怕吵到室友"场景） */
        Music_Stop();
        /* 【第 7 点】按用户设置的"震动强度"震，而不是写死的 50% */
        Motor_On(g_settings.vibrate);
    }
    else
    {
        Motor_Off();
        Music_Play(song, vol);
    }

}

void Alarm_Stop(void)
{
    Music_Stop();
    Motor_Off();

    Led_Breath(0);          /* 小灯熄灭 */

    s_ringIdx    = IDX_NONE;
    s_ringSec    = 0;
    s_snoozeSec  = 0;
    s_snoozeIdx  = IDX_NONE;
    s_lastSunrise = 0;
    g_ringingIdx = 0;

    /* 响完这一组，重新挑下一组写进硬件闹钟 */
    Alarm_ApplyHardware();
}

void Alarm_Snooze(void)
{
    u8 min;

    Music_Stop();
    Motor_Off();
    Led_Breath(0);

    s_snoozeIdx = s_ringIdx;
    min = g_settings.snooze_min;
    if (min == 0)
    {
        min = SNOOZE_MINUTES;
    }
    s_snoozeSec = (u16)min * 60U;

    s_ringIdx    = IDX_NONE;
    s_ringSec    = 0;
    s_lastSunrise = 0;
    g_ringingIdx = 0;

}

u8 Alarm_IsRinging(void)
{
    return (s_ringIdx != IDX_NONE) ? 1 : 0;
}

u8 Alarm_RingingIndex(void)
{
    return s_ringIdx;
}

u16 Alarm_SnoozeRemainSec(void)
{
    return s_snoozeSec;
}

/*========================================================================
 *                            每秒处理
 *========================================================================*/

u8 Alarm_Tick1s(void)
{
    u8  i;
    u16 w;
    u8  today;
    u8  started = 0;

    /* ---------- 1) 贪睡倒计时 ---------- */
    if (s_snoozeSec > 0)
    {
        s_snoozeSec--;
        if (s_snoozeSec == 0 && s_snoozeIdx != IDX_NONE)
        {
            u8 idx = s_snoozeIdx;
            s_snoozeIdx = IDX_NONE;
            Alarm_Start(idx);
            /* 贪睡响的这一次也记上分钟戳，避免软件比对立刻又触发一次 */
            s_lastRingWom[idx] = now_wom();
            return 1;
        }
    }

    /* ---------- 2) 响铃超时自动静音 ---------- */
    if (s_ringIdx != IDX_NONE)
    {
        s_ringSec++;
        if (s_ringSec >= ALARM_AUTO_STOP_SEC)
        {
            Alarm_Stop();
        }
        /* 正在响的这一秒不再做兜底比对，避免叠音 */
        return 0;
    }

    /* ---------- 3) 软件兜底比对：时:分 命中且今天该响 ---------- */
    if (!Clock_IsValid())
    {
        return 0;
    }

    today = Clock_TodayMask();
    w     = now_wom();

    for (i = 0; i < ALARM_MAX; i++)
    {
        if (!g_alarms[i].enable)
        {
            continue;
        }
        if ((g_alarms[i].days & today) == 0)
        {
            continue;
        }
        if (g_alarms[i].hour != g_clock.hour)
        {
            continue;
        }
        if (g_alarms[i].minute != g_clock.minute)
        {
            continue;
        }
        if (s_lastRingWom[i] == w)
        {
            continue;       /* 这一分钟已经响过了 */
        }

        s_lastRingWom[i] = w;
        Alarm_Start(i);
        started = 1;
        break;              /* 一次只起一组，避免叠音 */
    }

    return started;
}

/*========================================================================
 *                            日出唤醒
 *========================================================================*/

void Alarm_TickSunrise(void)
{
    s32 left;
    u8  level;

    /* 正在响铃 / 贪睡中，小灯交给响铃逻辑处理，不在这里抢 */
    if (s_ringIdx != IDX_NONE || s_snoozeSec > 0)
    {
        return;
    }

    if (!g_settings.sunrise_en)
    {
        if (s_lastSunrise != 0)
        {
            Led_Breath(0);
            s_lastSunrise = 0;
        }
        return;
    }

    left = Alarm_SecondsToNext();

    if (left < 0 || left > (s32)ALARM_SUNRISE_SEC)
    {
        if (s_lastSunrise != 0)
        {
            Led_Breath(0);
            s_lastSunrise = 0;
        }
        return;
    }

    /* 600 秒 -> 0，映射到亮度 255 -> 0（越接近响铃越亮）。
     * 纯整数：level = 255 * left / 600 的补数 */
    {
        u16 remain = (u16)left;
        u16 bright;

        bright = (u16)(255U - (255U * remain) / ALARM_SUNRISE_SEC);

        if (bright > 255U)
        {
            bright = 255U;
        }

        /* 每 100ms 调一级就够（《04》G 角色第 4 条），
         * 这里每秒调用一次，所以只有当亮度真的变了才写 IO */
        level = (u8)bright;

        if (level != s_lastSunrise)
        {
            Led_Breath(level);
            s_lastSunrise = level;
        }
    }
}

s32 Alarm_SecondsToNext(void)
{
    u8  idx = IDX_NONE;
    u8  dayBit = 0;
    u16 wom = 0;
    u16 nw;
    u16 delta;

    if (!find_next(&idx, &dayBit, &wom))
    {
        return -1;
    }

    nw = now_wom();

    if (wom >= nw)
    {
        delta = (u16)(wom - nw);
    }
    else
    {
        delta = (u16)(wom + MINUTES_PER_WEEK - nw);
    }

    return (s32)((u32)delta * 60UL);
}

/*========================================================================
 *                            编辑接口
 *========================================================================*/

u8 Alarm_Set(u8 idx, const AlarmItem_t *a)
{
    if (idx >= ALARM_MAX || a == NULL)
    {
        return 0;
    }

    g_alarms[idx] = *a;

    if (g_alarms[idx].hour > 23)
    {
        g_alarms[idx].hour = 0;
    }
    if (g_alarms[idx].minute > 59)
    {
        g_alarms[idx].minute = 0;
    }
    if (g_alarms[idx].days == DAY_NONE || (g_alarms[idx].days & (u8)(~DAY_ALL)) != 0)
    {
        g_alarms[idx].days = DAY_ALL;
    }

    /* 改完立刻存盘（《04》H 角色：设置页改完要立刻存，不然掉电白设），并重算硬件闹钟 */
    Storage_SaveAll();
    Alarm_ApplyHardware();

    return 1;
}

u8 Alarm_Delete(u8 idx)
{
    if (idx >= ALARM_MAX)
    {
        return 0;
    }

    g_alarms[idx].enable = 0;
    g_alarms[idx].days   = DAY_WORKDAY;
    g_alarms[idx].hour   = 7;
    g_alarms[idx].minute = 0;
    g_alarms[idx].song   = 0;

    Storage_SaveAll();
    Alarm_ApplyHardware();

    return 1;
}

u8 Alarm_Toggle(u8 idx)
{
    if (idx >= ALARM_MAX)
    {
        return 0;
    }

    g_alarms[idx].enable = (u8)(!g_alarms[idx].enable);

    Storage_SaveAll();
    Alarm_ApplyHardware();

    return 1;
}

u8 Alarm_Add(const AlarmItem_t *a)
{
    u8 i;

    for (i = 0; i < ALARM_MAX; i++)
    {
        if (!g_alarms[i].enable)
        {
            Alarm_Set(i, a);
            return i;
        }
    }

    return IDX_NONE;        /* ALARM_MAX 组都满了 */
}

/*========================================================================
 *                             初始化
 *========================================================================*/

void Alarm_Init(void)
{
    u8 i;

    for (i = 0; i < ALARM_MAX; i++)
    {
        s_lastRingWom[i] = 0xFFFFU;     /* 一个不可能的分钟序号，表示"还没响过" */
    }

    s_ringIdx     = IDX_NONE;
    s_ringSec     = 0;
    s_snoozeSec   = 0;
    s_snoozeIdx   = IDX_NONE;
    s_hwAlarmHit  = 0;
    s_lastSunrise = 0;
    g_ringingIdx  = 0;

    /* 接口与 IO 模式已经在 sys_init() 里配好了，这里不重复 Led_Init()——
     * 那会把上电进度指示的 8 颗灯清掉，也会把总开关关掉。
     * 只保证总开关是开的（日出唤醒的 8 颗灯要能被看见）。 */
    Led_Power(1);
    Led_Breath(0);

    Alarm_ApplyHardware();

}
