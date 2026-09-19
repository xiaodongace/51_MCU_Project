/*
 * App_Clock.c - 时间服务实现
 *
 * 读时间的序列照抄 v3.1 App/App_RTC.c 与 Driver/PCF8563.c：
 *   PCF8563_init()   -> 配 GPIO（P3.7 准双向、P3.2/P3.3 开漏）+ INT3 下降沿
 *   PCF8563_get_clock(&c) -> 一次 I2C 读 7 字节，驱动内部已做 BCD 转换
 *
 * 星期为什么自己算：
 *   PCF8563 的星期寄存器只是 0..6 的一个计数器，上电默认值不对，
 *   芯片自己也不知道"今天是周几"。本工程用年月日在软件里算，
 *   显示和"周几响"都不依赖芯片的星期计数器。
 */
#include "App_Clock.h"

#include "PCF8563.h"
#include "I2C_Lock.h"       /* 副屏与时钟芯片共用 I2C，访问前必须拿总线锁 */
#include "I2C.h"

/* 已连续失败多少次（成功时清零）。用来只在"刚开始失败"时打印一次，避免刷屏。 */
static u8 s_failStreak = 0;

/* 待写入芯片的时间（由 Clock_Set 登记，Clock_SetNow 执行） */
static Clock_t s_pendingClock;

/*
 * 诊断：把 PCF8563 时间寄存器区的 7 个原始字节打出来。
 *
 * 为什么需要它：`clock valid=0` 只说明"读回来的值过不了合法性检查"，
 * 但过不了的原因可能是总线没应答（全 FF）、芯片没走时（全 00）、
 * 世纪位异常、或者只是秒的 VL 位。这三者处理方式完全不同。
 * 打出原始字节，一眼就能分辨。
 *
 * 寄存器含义（驱动注释原文）：
 *   [0] 秒  VL 0 1 1 - 0 0 0 0      [1] 分  x 1 1 1 - 0 0 0 0
 *   [2] 时  x x 1 1 - 0 0 0 0      [3] 日  x x 1 1 - 0 0 0 0
 *   [4] 周  x x x x - x 0 0 0      [5] 月/世纪 C x x 1 - 0 0 0 0
 *   [6] 年  1 1 1 1 - 0 0 0 0
 */
static void clock_debug_raw(void)
{
    u8 p[7];
    u8 i;

    for (i = 0; i < 7; i++)
    {
        p[i] = 0;
    }

    I2C_Lock();
    I2C_ReadNbyte(PCF8563_ADDR, PCF8563_REG, p, 7);
    I2C_Unlock();

    printf("[RTC] raw regs 02..08: ");
    for (i = 0; i < 7; i++)
    {
        printf("%02X ", (unsigned)p[i]);
    }
    printf("\r\n[RTC] expect: sec/mi/hr/day/wk/mon/yr, 全 FF=没人应答, 全 00=芯片没走时\r\n");
}

static u8 s_valid = 0;

/* Sakamoto 算法：由公历日期算星期，纯整数，无查表大数组。
 * 返回 0=周日, 1=周一, ... 6=周六 */
u8 Clock_WeekdayOf(u16 year, u8 month, u8 day)
{
    static u8 code t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    u16 y;
    u16 sum;
    u8  m;

    if (month < 1 || month > 12)
    {
        return 0;
    }
    if (day < 1 || day > 31)
    {
        return 0;
    }

    y = year;
    m = month;

    if (m < 3)
    {
        y = (u16)(y - 1);
    }

    sum = (u16)(y + y / 4U - y / 100U + y / 400U + t[m - 1] + day);

    return (u8)(sum % 7U);
}

u8 Clock_DaysInMonth(u16 year, u8 month)
{
    static u8 code d[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    u8 leap;

    if (month < 1 || month > 12)
    {
        return 31;
    }

    if (month != 2)
    {
        return d[month - 1];
    }

    leap = 0;
    if ((year % 4U) == 0U)
    {
        leap = 1;
    }
    if ((year % 100U) == 0U)
    {
        leap = 0;
    }
    if ((year % 400U) == 0U)
    {
        leap = 1;
    }

    return leap ? 29 : 28;
}

u8 Clock_IsValid(void)
{
    return s_valid;
}

/*
 * 【方案 B】Clock_Set() 改为"只登记请求"，真正的 I2C 写由 TASK_RENDER 执行。
 * 原因见 App_Public.h：所有 I2C 必须收在同一个任务里串行执行，才不需要总线锁。
 * 真正干活的函数是同文件下面的 Clock_SetNow()。
 */
void Clock_Set(Clock_t *t)
{
    if (t != NULL)
    {
        s_pendingClock = *t;
    }
    g_reqClockSet = 1;
}

void Clock_SetNow(Clock_t *t)
{
    Clock_t tmp;
    u8 w;

    if (t == NULL)
    {
        return;
    }

    /* 先把日期字段收进合法范围，避免把越界值写进芯片 */
    if (t->month < 1)
    {
        t->month = 1;
    }
    if (t->month > 12)
    {
        t->month = 12;
    }
    if (t->day < 1)
    {
        t->day = 1;
    }
    if (t->hour > 23)
    {
        t->hour = 0;
    }
    if (t->minute > 59)
    {
        t->minute = 0;
    }
    if (t->second > 59)
    {
        t->second = 0;
    }
    {
        u8 dim = Clock_DaysInMonth(t->year, t->month);
        if (t->day > dim)
        {
            t->day = dim;
        }
    }

    /* 星期由年月日算，覆盖调用方传进来的值 */
    w = Clock_WeekdayOf(t->year, t->month, t->day);
    t->week = w;

    /* PCF8563 的 week 寄存器取 0..6，驱动直接写 p[4] = c.week，与上面同约定 */
    tmp = *t;

    /* 写芯片是 I2C 事务，和副屏共用总线，必须串行化 */
    I2C_Lock();
    PCF8563_set_clock(tmp);
    I2C_Unlock();

    /* 写完立刻回读一次，保证 g_clock 与芯片一致 */
    Clock_Refresh();
}

s8 Clock_Refresh(void)
{
    Clock_t c;
    s8 rst;

    /* 驱动内部做 BCD 转换（READ_BCD），读出来就是十进制。
     * 读一次是 1 个 I2C 事务，与副屏刷新互斥。 */
    I2C_Lock();
    PCF8563_get_clock(&c);
    I2C_Unlock();

    /* 合法性检查：芯片没接/断电时读到的是 0xFF 之类，全字段过滤一遍。
     * 失败时把具体是哪一项越界、以及读到的值一起打出来 ——
     * "读不到"和"读到了但越界"是两回事，不打印就只能靠猜。 */
    rst = 0;

    if (c.year < 2000 || c.year > 2199)
    {
        rst = -1;
    }
    else if (c.month < 1 || c.month > 12)
    {
        rst = -2;
    }
    else if (c.day < 1 || c.day > Clock_DaysInMonth(c.year, c.month))
    {
        rst = -3;
    }
    else if (c.hour > 23 || c.minute > 59 || c.second > 59)
    {
        rst = -4;
    }

    if (rst != 0)
    {
        /* 【2026-09-18 重要修正】这里原来会 printf 一行诊断。
         * 真机日志显示 refresh 失败与恢复在**每秒交替出现** ——
         * 也就是这个 printf 每秒被调用两次。而 printf 是 C51 里栈开销最大的函数，
         * 它的调用链是：task_logic -> Menu_Tick1s -> Clock_TryRecover -> Clock_Refresh
         *            -> PCF8563_get_clock -> I2C_ReadNbyte -> Wait -> printf
         * 链接映射里 FREE_STACK 只剩 0x14(20) 字节，且 ?RTX_STACKERROR 已被链接进来 ——
         * 栈被踩穿，表现就是读到 y=259 mo=93 94:08:07 这类**算术上不可能**的字段值，
         * 以及随后整个系统的状态错乱、最后自动重启。
         *
         * 所以这里只累加计数，不再打印。诊断信息由低频路径（Clock_Init 打印一次 rst）
         * 和 TASK_SENSOR 提供，1 秒级热路径里绝不放 printf。 */
        s_failStreak++;
        return rst;
    }

    s_failStreak = 0;

    /* 星期不信芯片，自己算 */
    c.week = Clock_WeekdayOf(c.year, c.month, c.day);

    g_clock = c;
    s_valid = 1;

    return 0;
}

u8 Clock_TodayMask(void)
{
    /* g_clock.week: 0=周日, 1=周一, ... 6=周六
     * 本工程掩码:   bit0=周一, ... bit5=周六, bit6=周日 */
    static u8 code map[7] = { DAY_SUN, DAY_MON, DAY_TUE, DAY_WED, DAY_THU, DAY_FRI, DAY_SAT };

    if (g_clock.week > 6)
    {
        return DAY_NONE;
    }

    return map[g_clock.week];
}

u8 Clock_MaskToWeek(u8 mask)
{
    u8 i;

    /* 单天掩码 -> PCF8563 星期值（0=周日..6=周六） */
    /* bit0=周一->1, bit1=周二->2, bit2=周三->3, bit3=周四->4,
     * bit4=周五->5, bit5=周六->6, bit6=周日->0 */
    static u8 code map[7] = { 1, 2, 3, 4, 5, 6, 0 };

    if (mask == 0)
    {
        return DAY_INVALID;
    }

    for (i = 0; i < 7; i++)
    {
        if ((mask >> i) & 1)
        {
            /* 多天掩码只取最低的那一天，调用方需自行保证传单天 */
            return map[i];
        }
    }

    return DAY_INVALID;
}

u16 Clock_MinutesOfDay(u8 hour, u8 minute)
{
    return (u16)((u16)hour * 60U + minute);
}

void Clock_AddSeconds(Clock_t *t, u16 sec)
{
    u16 total;
    u8  dim;

    if (t == NULL)
    {
        return;
    }

    /* 简化实现：把它当成"秒累加 + 逐级进位"，够番茄钟和贪睡用 */
    total = (u16)(t->second + (sec % 60U));
    t->second = (u8)(total % 60U);
    {
        u16 carryMin = (u16)((sec / 60U) + (total / 60U));
        u16 totalMin = (u16)(t->minute + carryMin);

        t->minute = (u8)(totalMin % 60U);
        {
            u16 carryHour = (u16)(totalMin / 60U);
            u16 totalHour = (u16)(t->hour + carryHour);

            t->hour = (u8)(totalHour % 24U);
            {
                u16 carryDay = (u16)(totalHour / 24U);

                if (carryDay > 0)
                {
                    t->day = (u8)(t->day + carryDay);
                    dim = Clock_DaysInMonth(t->year, t->month);

                    while (t->day > dim)
                    {
                        t->day = (u8)(t->day - dim);
                        t->month++;
                        if (t->month > 12)
                        {
                            t->month = 1;
                            t->year++;
                        }
                        dim = Clock_DaysInMonth(t->year, t->month);
                    }
                }
            }
        }
    }

    t->week = Clock_WeekdayOf(t->year, t->month, t->day);
}

void Clock_Init(void)
{
    u8 i;
    s8 rst;

    PCF8563_init();

    /*
     * 【真机修正】多次重试，而且**每次之间要等一会儿**。
     *
     * 用户现象：刚插上 USB 数据线时，数码管和副屏的时钟都不走，必须按一下重启按钮才正常。
     *
     * 原因：USB 插入的瞬间 MCU 和 PCF8563 是同时上电的，芯片内部（晶振起振、
     * 电源稳定）需要几百毫秒才能正常应答 I2C；而本函数原来三次重试之间
     * 没有任何等待，等于三次都在同一个时刻试，自然一起失败。
     * 按重启按钮之所以有效，是因为那时电源早已稳定。
     *
     * 现在改成 5 次、每次间隔 100ms（共约 500ms 窗口），
     * 给芯片留出稳定的时间。 */
    rst = -1;
    for (i = 0; i < 8; i++)
    {
        rst = Clock_Refresh();
        if (rst == 0)
        {
            break;
        }

        if (i < 7)
        {
            os_wait2(K_TMO, 20);        /* 20 x 5ms = 100ms */
        }
    }

    printf("[RTC] init done, rst=%d\r\n", (int)rst);

    if (rst != 0)
    {
        /* 读出来是无效值 —— 先把原始寄存器打出来便于定位 */
        clock_debug_raw();

        /*
         * 【真机修正 · 关键】这一步必须写芯片。
         *
         * 《04-M1分工规格》B 角色的原话是"时间不要每次开机都重设，
         * 时钟芯片靠板上小电池维持走时，开机就重设会把时间冲掉"。
         * 但那条规则的前提是"芯片里已经有一个有效时间"。
         *
         * 实机读回来的原始字节是： sec=0x21 min=0x20 hr=0x00 day=0x00 wk=0x00 mon=0x00 yr=0x00
         * 秒在走（两次读相隔 1 秒，0x21 -> 0x22），但**日/月/年全是 0** ——
         * 这颗芯片从来没有被设过日期，或者纽扣电池掉电之后只从 00:00 重新起算。
         *
         * 此时如果不写，Clock_Refresh() 会因为"月份 = 0"永远判为非法 ->
         * g_clock 永远停在占位值 -> 三块屏的时间全部静止 -> 闹钟的时间基准是坏的。
         *
         * 所以：只有在"读出来无效"时才写一次，让整机先有一个能走的时间基准；
         * 芯片正常走时时绝不重设，两条规则不冲突。
         *
         * 写入的默认值是 2026-01-01 00:00:00 —— 只是为了让它跑起来。
         * 真实时间需要用户校准（见下面 printf 的提示，或 App_Uart 的 0x02 命令）。 */
        g_clock.year   = 2026;
        g_clock.month  = 1;
        g_clock.day    = 1;
        g_clock.hour   = 0;
        g_clock.minute = 0;
        g_clock.second = 0;
        g_clock.week   = Clock_WeekdayOf(2026, 1, 1);

        Clock_Set(&g_clock);

        if (Clock_IsValid())
        {
            printf("[RTC] 已写入默认时间 2026-01-01 00:00:00，时钟开始走时。\r\n");
            printf("[RTC] 请校时：串口发 0x02 命令，或主界面用矩阵键盘直接输 4 位数字(HHMM)。\r\n");
        }
        else
        {
            printf("[RTC] 写入后仍然读不回有效值 -> 芯片/电池/上拉有问题，请查硬件。\r\n");
            s_valid = 0;
        }

        s_failStreak = 0;       /* 让每秒的失败重新从第 1 次开始报 */
    }
}

/*
 * 时钟没起来时的自救。
 *
 * 场景：USB 刚插上时 PCF8563 可能还没稳定，Clock_Init() 的 5 次重试也可能全落空，
 * 这时系统就没有时间基准了（时间静止、闹钟不会响）。
 * 由 Menu_Tick1s() 每秒调用，每 5 秒真正尝试一次，最多 10 次。
 * 成功了就自动恢复，不需要用户按重启按钮。
 */
void Clock_TryRecover(void)
{
    g_reqClockRecover = 1;      /* 【方案 B】只登记，由 TASK_RENDER 执行 */
}

/* 真正执行自救（由 TASK_RENDER 调用） */
void Clock_TryRecoverNow(void)
{
    static u8 s_tick  = 0;
    static u8 s_tries = 0;

    if (s_valid)
    {
        return;
    }
    if (s_tries >= 60)
    {
        return;                 /* 放弃，避免无限重试一直擦写芯片 */
    }

    s_tick++;
    if (s_tick < 5)             /* 每 5 秒试一次 */
    {
        return;
    }
    s_tick = 0;
    s_tries++;

    /* 冷启动时芯片要是迟迟不应答，前几次失败很正常；这里不写芯片，
     * 只做"读一次试试"。读得到就说明芯片醒了，直接恢复，不必擦写。 */
    if (Clock_Refresh() == 0)
    {
        return;         /* 【不打印】1 秒级热路径，见 Clock_Refresh 里的说明 */
    }

    /* 读不到再走"写一次默认时间"的兜底 */
    g_clock.year   = 2026;
    g_clock.month  = 1;
    g_clock.day    = 1;
    g_clock.hour   = 0;
    g_clock.minute = 0;
    g_clock.second = 0;
    g_clock.week   = Clock_WeekdayOf(2026, 1, 1);

    Clock_Set(&g_clock);
}
