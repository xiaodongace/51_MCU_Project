/*
 * App_Storage.c - 片内 EEPROM 读写
 *
 * 底层直接调用厂家给的 Lib/EEPROM.c（与现有工程一致，不自己重写 IAP 触发序列）：
 *   void  EEPROM_read_n(u16 EE_address, u8 *DataAddress, u16 number);
 *   void  EEPROM_write_n(u16 EE_address, u8 *DataAddress, u16 number);
 *   void  EEPROM_SectorErase(u16 EE_address);
 *
 * 0 号区布局（512 字节内）：
 *   偏移 0    : Settings_t
 *   偏移 20   : AlarmItem_t[8]（每组 5 字节，共 40 字节）
 *   其余      : 保留
 *
 * 记录区（2..7 号区）：
 *   顺序填充，每 2 字节一条；擦除态是 0xFF，用"湿度 == 0xFF"判定空位。
 *   写满 1536 条之后回到 2 号区重新擦除从头写（更旧的记录被覆盖）。
 */
#include "App_Storage.h"

#include "EEPROM.h"

#define OFF_SETTINGS    0
#define OFF_ALARMS      20

/* 先擦页、再写一页的完整内容。EEPROM_write_n 内部会逐字节触发 IAP。 */
static u8 ee_write_page(u16 addr, u8 *buf, u16 len)
{
    EEPROM_SectorErase(addr);
    EEPROM_write_n(addr, buf, len);
    return ST_OK;
}

/* 只写、不擦（用于往已擦除区追加记录） */
static u8 ee_write(u16 addr, u8 *buf, u16 len)
{
    EEPROM_write_n(addr, buf, len);
    return ST_OK;
}

static u8 ee_read(u16 addr, u8 *buf, u16 len)
{
    EEPROM_read_n(addr, buf, len);
    return ST_OK;
}

/*========================================================================
 *                              默认值
 *========================================================================*/

void Storage_Default(void)
{
    u8 i;

    g_settings.magic       = SETTINGS_MAGIC;
    g_settings.version     = SETTINGS_VERSION;
    /* 【2026-09-22 用户要求】出厂默认音量改为 **1**、震动强度 25% */
    g_settings.volume      = 1;
    g_settings.vibrate     = 25;    /* 25% 震动强度 */
    g_settings.alert_mode  = ALERT_RING;
    g_settings.song        = 0;
    g_settings.pomodoro_work = 25;
    g_settings.pomodoro_rest = 5;
    g_settings.screen_on   = 1;
    g_settings.snooze_min  = SNOOZE_MINUTES;
    g_settings.sunrise_en  = 1;
    g_settings.logCount    = 0;

    for (i = 0; i < 3; i++)
    {
        g_settings.reserved[i] = 0;
    }

    for (i = 0; i < ALARM_MAX; i++)
    {
        g_alarms[i].enable = 0;
        g_alarms[i].hour   = 7;
        g_alarms[i].minute = 0;
        g_alarms[i].days   = DAY_WORKDAY;
        g_alarms[i].song   = 0;
    }

    /* 【2026-09-21 用户要求】"最开始都关闭" —— 出厂默认**全部**关闭（现在 3 组），
     * 由用户在「1 闹钟」里按 KEY2 逐组打开。
     * （原来是把第 1 组默认打开，方便一上电就能看到东西。） */
}

/*========================================================================
 *                          0 号区：设置 + 闹钟
 *========================================================================*/

/*
 * 0 号区的镜像缓冲。
 * 放 xdata：Settings_t + 8 x AlarmItem_t 约 60 字节，
 * 本项目用 Compact 内存模型，默认变量进 pdata（256 字节），
 * 大一点的结构显式放 xdata 更稳妥。
 */
static u8 xdata s_page0[64];

u8 Storage_SaveAll(void)
{
    u8 i;

    /* 1) 先在内存里把整页内容拼好（《04》B 角色：先在内存备好完整数据再擦） */
    s_page0[0] = (u8)(g_settings.magic & 0xFF);
    s_page0[1] = (u8)(g_settings.magic >> 8);
    s_page0[2] = g_settings.version;
    s_page0[3] = g_settings.volume;
    s_page0[4] = g_settings.alert_mode;
    s_page0[5] = g_settings.song;
    s_page0[6] = g_settings.pomodoro_work;
    s_page0[7] = g_settings.pomodoro_rest;
    s_page0[8] = g_settings.screen_on;
    s_page0[9] = g_settings.snooze_min;
    s_page0[10] = g_settings.sunrise_en;
    s_page0[11] = (u8)(g_settings.logCount & 0xFF);
    s_page0[12] = (u8)(g_settings.logCount >> 8);
    s_page0[13] = g_settings.vibrate;              /* 第 7 点：震动强度 */

    for (i = 14; i < OFF_ALARMS; i++)
    {
        s_page0[i] = 0;
    }

    for (i = 0; i < ALARM_MAX; i++)
    {
        s_page0[OFF_ALARMS + i * 5 + 0] = g_alarms[i].enable;
        s_page0[OFF_ALARMS + i * 5 + 1] = g_alarms[i].hour;
        s_page0[OFF_ALARMS + i * 5 + 2] = g_alarms[i].minute;
        s_page0[OFF_ALARMS + i * 5 + 3] = g_alarms[i].days;
        s_page0[OFF_ALARMS + i * 5 + 4] = g_alarms[i].song;
    }

    /* 2) 一次擦除 + 一次写回。只写这 60 字节，不必写满 512。 */
    ee_write_page(EE_SETTINGS, s_page0, OFF_ALARMS + ALARM_MAX * 5);

    return ST_OK;
}

u8 Storage_Load(void)
{
    u8 i;
    u8 ok;

    ee_read(EE_SETTINGS, s_page0, OFF_ALARMS + ALARM_MAX * 5);

    g_settings.magic         = (u16)s_page0[0] | ((u16)s_page0[1] << 8);
    g_settings.version       = s_page0[2];
    g_settings.volume        = s_page0[3];
    g_settings.alert_mode    = s_page0[4];
    g_settings.song          = s_page0[5];
    g_settings.pomodoro_work = s_page0[6];
    g_settings.pomodoro_rest = s_page0[7];
    g_settings.screen_on     = s_page0[8];
    g_settings.snooze_min    = s_page0[9];
    g_settings.sunrise_en    = s_page0[10];
    g_settings.logCount      = (u16)s_page0[11] | ((u16)s_page0[12] << 8);
    g_settings.vibrate       = s_page0[13];

    for (i = 0; i < 4; i++)
    {
        g_settings.reserved[i] = 0;
    }

    /* 魔数或版本不对 -> 当作第一次上电，全部填默认值 */
    ok = 1;
    if (g_settings.magic != SETTINGS_MAGIC)
    {
        ok = 0;
    }
    if (g_settings.version != SETTINGS_VERSION)
    {
        ok = 0;
    }
    if (g_settings.volume > VOLUME_MAX)
    {
        ok = 0;
    }
    if (g_settings.vibrate > 100)
    {
        ok = 0;
    }
    if (g_settings.alert_mode > ALERT_VIBRATE)
    {
        ok = 0;
    }
    if (g_settings.pomodoro_work == 0 || g_settings.pomodoro_work > 99)
    {
        ok = 0;
    }
    if (g_settings.logCount > EE_LOG_CAPACITY)
    {
        ok = 0;
    }

    if (!ok)
    {
        Storage_Default();
        return ST_ERR_MAGIC;
    }

    for (i = 0; i < ALARM_MAX; i++)
    {
        g_alarms[i].enable = s_page0[OFF_ALARMS + i * 5 + 0];
        g_alarms[i].hour   = s_page0[OFF_ALARMS + i * 5 + 1];
        g_alarms[i].minute = s_page0[OFF_ALARMS + i * 5 + 2];
        g_alarms[i].days   = s_page0[OFF_ALARMS + i * 5 + 3];
        g_alarms[i].song   = s_page0[OFF_ALARMS + i * 5 + 4];

        /* 逐项兜底：任何一个越界都当这一组无效，避免脏数据把显示搞乱 */
        if (g_alarms[i].enable > 1 ||
            g_alarms[i].hour > 23 ||
            g_alarms[i].minute > 59 ||
            g_alarms[i].days == DAY_NONE ||
            (g_alarms[i].days & (u8)~DAY_ALL) != 0)
        {
            g_alarms[i].enable = 0;
            g_alarms[i].days   = DAY_WORKDAY;
        }
    }

    return ST_OK;
}

/*========================================================================
 *                          2..7 号区：温湿度记录
 *========================================================================*/

u16 Storage_LogCount(void)
{
    return g_settings.logCount;
}

/* 写入地址 = 记录区起点 + 条序号 * 2。条序号按容量取模，满了就从头覆盖。 */
static u16 log_addr(u16 seq)
{
    u16 s;

    s = (u16)(seq % EE_LOG_CAPACITY);
    return (u16)(EE_LOG_BASE + s * 2U);
}

u8 Storage_AppendLog(s8 temp, u8 humi)
{
    u8  buf[2];
    u16 addr;
    u16 seq;
    u16 offsetInPage;
    u16 pageAddr;

    if (humi > 100)
    {
        humi = 100;
    }

    seq  = g_settings.logCount;
    addr = log_addr(seq);

    offsetInPage = (u16)((addr - EE_LOG_BASE) % EE_PAGE_SIZE);
    pageAddr     = (u16)(addr - offsetInPage);

    /* 擦除规则：只在"正好要写某一页的第 0 条"时擦这一页。
     * 这样每 256 条才擦一次（约 42 小时一次），寿命完全不是问题；
     * 页内续写不擦，正好利用了"擦除态就是 0xFF，可以直接按字节写"这一点。 */
    if (offsetInPage == 0)
    {
        EEPROM_SectorErase(pageAddr);
    }

    /* 温度整体加 100 再存：s8 的 -1 编码是 0xFF，会和"空位标记"撞车。
     * -55..125 变成 45..225，永远不会出现 0xFF。读的时候减回去。 */
    buf[0] = (u8)((s16)temp + 100);
    buf[1] = humi;

    ee_write(addr, buf, 2);

    g_settings.logCount++;

    /* 每 32 条回写一次 0 号区，把写指针存下来。
     * 掉电最多丢 32 条的指针，下次上电从旧位置继续写会覆盖最近几条记录，
     * 但不会破坏更早的数据，也不会写坏结构。 */
    if ((g_settings.logCount % 32U) == 0U)
    {
        Storage_SaveAll();
    }

    return ST_OK;
}

u8 Storage_ReadLog(u16 idx, s8 *temp, u8 *humi)
{
    u8  buf[2];
    u16 addr;

    if (idx >= EE_LOG_CAPACITY)
    {
        return 0;
    }

    addr = log_addr(idx);
    ee_read(addr, buf, 2);

    if (buf[1] == 0xFF)
    {
        return 0;       /* 空位 */
    }

    *temp = (s8)((s16)buf[0] - 100);
    *humi = buf[1];

    return 1;
}
