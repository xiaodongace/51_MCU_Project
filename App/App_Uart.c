/*
 * App_Uart.c - 上位机协议解析与执行
 *
 * 字节来源：Lib/UART_Isr.c 的 UART1 接收中断会把字节写进 RX1_Buffer，
 *           并把个数记在 COM1.RX_Cnt 里。本模块不去改中断，只做消费。
 */
#include "App_Uart.h"

#include "UART.h"
#include "App_Clock.h"
#include "App_Alarm.h"
#include "App_Storage.h"
#include "App_Sensor.h"
#include "App_Music.h"

/*------------------------------------------------------------------------
 * 解析状态机的状态
 *------------------------------------------------------------------------*/
#define ST_HEAD1    0
#define ST_HEAD2    1
#define ST_LEN      2
#define ST_BODY     3
#define ST_SUM      4

/* 一帧最长：8 组闹钟 40 字节 + CMD + LEN + SUM + 帧头 = 45，留点余量 */
#define RX_FRAME_MAX    48

/* 发送缓冲。放 xdata：C51 的栈在片内 RAM 上很紧，几十字节别放局部变量 */
static u8 xdata s_txBuf[56];

static u8  s_state   = ST_HEAD1;
static u8  s_len     = 0;
static u8  s_pos     = 0;
static u8  s_sum     = 0;
static u8 xdata s_body[RX_FRAME_MAX];

static u16 s_frames  = 0;
static u16 s_errors  = 0;

/*========================================================================
 *                              发送
 *========================================================================*/

static void uart_send_byte(u8 b)
{
    /* Lib 里 UART_QUEUE_MODE = 0，是阻塞发送。
     * 单字节 87us，一帧最长 45 字节约 4ms，远小于 RTX51 的 25ms 时间片，
     * 所以不会把别的任务饿死。 */
    TX1_write2buff(b);
}

/* 注意：形参名不能叫 data —— data 是 C51 的存储类型关键字，
 * 用了会直接报语法错误（v3.1 的 DHT11.c 里也专门提醒过这一点）。 */
static void send_frame(u8 cmd, const u8 *pdat, u8 len)
{
    u8 i;
    u8 sum;

    s_txBuf[0] = 0xAA;
    s_txBuf[1] = 0x55;
    s_txBuf[2] = (u8)(len + 1);         /* LEN = CMD + DATA */
    s_txBuf[3] = cmd;

    sum = s_txBuf[2];
    sum = (u8)(sum + cmd);

    for (i = 0; i < len; i++)
    {
        s_txBuf[4 + i] = pdat[i];
        sum = (u8)(sum + pdat[i]);
    }

    s_txBuf[4 + len] = sum;

    for (i = 0; i < (u8)(len + 5); i++)
    {
        uart_send_byte(s_txBuf[i]);
    }
}

static void send_ack(u8 cmd, u8 status)
{
    u8 d[1];

    d[0] = status;
    send_frame(cmd, d, 1);
}

/*========================================================================
 *                              命令执行
 *========================================================================*/

static void cmd_execute(u8 cmd, const u8 *d, u8 len)
{
    u8 out[44];

    switch (cmd)
    {
    /* ---------- 读时间 ---------- */
    case UC_GET_TIME:
        if (!Clock_IsValid())
        {
            send_ack(cmd, UC_FAIL);
            return;
        }
        out[0] = (u8)(g_clock.year >> 8);
        out[1] = (u8)(g_clock.year & 0xFF);
        out[2] = g_clock.month;
        out[3] = g_clock.day;
        out[4] = g_clock.week;          /* 0=周日..6=周六 */
        out[5] = g_clock.hour;
        out[6] = g_clock.minute;
        out[7] = g_clock.second;
        send_frame(cmd, out, 8);
        break;

    /* ---------- 校时 ---------- */
    case UC_SET_TIME:
        if (len < 7)
        {
            send_ack(cmd, UC_FAIL);
            return;
        }
        g_clock.year   = (u16)(((u16)d[0] << 8) | d[1]);
        g_clock.month  = d[2];
        g_clock.day    = d[3];
        g_clock.hour   = d[4];
        g_clock.minute = d[5];
        g_clock.second = d[6];
        Clock_Set(&g_clock);            /* 写芯片 + 立刻回读 */
        Alarm_ApplyHardware();          /* 时间变了，硬件闹钟要重算 */
        send_ack(cmd, Clock_IsValid() ? UC_OK : UC_FAIL);
        break;

    /* ---------- 读设置 ---------- */
    case UC_GET_SETTINGS:
        out[0] = g_settings.volume;
        out[1] = g_settings.alert_mode;
        out[2] = g_settings.song;
        out[3] = g_settings.pomodoro_work;
        out[4] = g_settings.pomodoro_rest;
        out[5] = g_settings.screen_on;
        out[6] = g_settings.snooze_min;
        out[7] = g_settings.sunrise_en;
        send_frame(cmd, out, 8);
        break;

    /* ---------- 写设置 ---------- */
    case UC_SET_SETTINGS:
        if (len < 8)
        {
            send_ack(cmd, UC_FAIL);
            return;
        }
        g_settings.volume        = (d[0] > VOLUME_MAX) ? VOLUME_MAX : d[0];
        g_settings.alert_mode    = (d[1] > ALERT_VIBRATE) ? ALERT_RING : d[1];
        g_settings.song          = (d[2] >= SONG_COUNT) ? 0 : d[2];
        g_settings.pomodoro_work = (d[3] == 0 || d[3] > 99) ? 25 : d[3];
        g_settings.pomodoro_rest = (d[4] == 0 || d[4] > 99) ? 5 : d[4];
        g_settings.screen_on     = (d[5] != 0);
        g_settings.snooze_min    = (d[6] == 0 || d[6] > 60) ? SNOOZE_MINUTES : d[6];
        g_settings.sunrise_en    = (d[7] != 0);
        Storage_SaveAll();
        send_ack(cmd, UC_OK);
        break;

    /* ---------- 读 8 组闹钟 ---------- */
    case UC_GET_ALARMS:
        {
            u8 i;
            for (i = 0; i < ALARM_MAX; i++)
            {
                out[i * 5 + 0] = g_alarms[i].enable;
                out[i * 5 + 1] = g_alarms[i].hour;
                out[i * 5 + 2] = g_alarms[i].minute;
                out[i * 5 + 3] = g_alarms[i].days;
                out[i * 5 + 4] = g_alarms[i].song;
            }
            send_frame(cmd, out, (u8)(ALARM_MAX * 5));
        }
        break;

    /* ---------- 设一组闹钟 ---------- */
    case UC_SET_ALARM:
        if (len < 6 || d[0] >= ALARM_MAX)
        {
            send_ack(cmd, UC_FAIL);
            return;
        }
        {
            AlarmItem_t a;
            a.enable = (d[1] != 0);
            a.hour   = d[2];
            a.minute = d[3];
            a.days   = d[4];
            a.song   = (d[5] > SONG_COUNT) ? 0 : d[5];
            Alarm_Set(d[0], &a);        /* 内部会存盘 + 重算硬件闹钟 */
        }
        send_ack(cmd, UC_OK);
        break;

    /* ---------- 删一组闹钟 ---------- */
    case UC_DEL_ALARM:
        if (len < 1 || d[0] >= ALARM_MAX)
        {
            send_ack(cmd, UC_FAIL);
            return;
        }
        Alarm_Delete(d[0]);
        send_ack(cmd, UC_OK);
        break;

    /* ---------- 读温湿度 ---------- */
    case UC_GET_ENV:
        out[0] = (u8)(g_tempX10 >> 8);
        out[1] = (u8)(g_tempX10 & 0xFF);
        out[2] = g_humi;
        out[3] = Sensor_HumiValid();
        send_frame(cmd, out, 4);
        break;

    /* ---------- 读记录条数 ---------- */
    case UC_GET_LOGCOUNT:
        out[0] = (u8)(Storage_LogCount() >> 8);
        out[1] = (u8)(Storage_LogCount() & 0xFF);
        send_frame(cmd, out, 2);
        break;

    /* ---------- 读第 n 条记录 ---------- */
    case UC_GET_LOG:
        if (len < 2)
        {
            send_ack(cmd, UC_FAIL);
            return;
        }
        {
            u16 idx;
            s8  t;
            u8  h;

            idx = (u16)(((u16)d[0] << 8) | d[1]);

            if (Storage_ReadLog(idx, &t, &h))
            {
                out[0] = (u8)t;
                out[1] = h;
                send_frame(cmd, out, 2);
            }
            else
            {
                send_ack(cmd, UC_FAIL);
            }
        }
        break;

    /* ---------- 读版本号 ---------- */
    case UC_GET_VERSION:
        out[0] = APP_VERSION_MAJOR;
        out[1] = APP_VERSION_MINOR;
        out[2] = 0;
        send_frame(cmd, out, 3);
        break;

    default:
        send_ack(cmd, UC_FAIL);
        break;
    }
}

/*========================================================================
 *                              解析状态机
 *========================================================================*/

static void feed_byte(u8 b)
{
    switch (s_state)
    {
    case ST_HEAD1:
        if (b == 0xAA)
        {
            s_state = ST_HEAD2;
        }
        break;

    case ST_HEAD2:
        if (b == 0x55)
        {
            s_state = ST_LEN;
        }
        else if (b == 0xAA)
        {
            /* 仍然是帧头，继续等第二个字节 */
            s_state = ST_HEAD2;
        }
        else
        {
            s_state = ST_HEAD1;
        }
        break;

    case ST_LEN:
        if (b == 0 || b > RX_FRAME_MAX)
        {
            s_errors++;
            s_state = ST_HEAD1;
            break;
        }
        s_len   = b;
        s_pos   = 0;
        s_sum   = b;
        s_state = ST_BODY;
        break;

    case ST_BODY:
        s_body[s_pos] = b;
        s_pos++;
        s_sum = (u8)(s_sum + b);

        if (s_pos >= s_len)
        {
            s_state = ST_SUM;
        }
        break;

    case ST_SUM:
        if (b == s_sum)
        {
            u8 cmd = s_body[0];
            u8 dlen = (u8)(s_len - 1);

            s_frames++;
            cmd_execute(cmd, &s_body[1], dlen);
        }
        else
        {
            s_errors++;
        }
        s_state = ST_HEAD1;
        break;

    default:
        s_state = ST_HEAD1;
        break;
    }
}

void Uart_Init(void)
{
    s_state = ST_HEAD1;
    s_len   = 0;
    s_pos   = 0;
    s_sum   = 0;
    s_frames = 0;
    s_errors = 0;
}

/*
 * 消费 Lib 接收缓冲里的字节。
 *
 * 关于并发：接收中断可能在本次循环期间继续往 RX1_Buffer 里追加。
 * 先快照 RX_Cnt，处理快照范围内的字节，最后把已处理的字节"搬走"而不是粗暴清零，
 * 这样本次循环期间新到的字节不会被丢掉。
 */
void Uart_Poll(void)
{
    u8 n;
    u8 i;
    u8 left;

    n = (u8)COM1.RX_Cnt;

    if (n == 0)
    {
        return;
    }

    /* 最多处理 32 个字节，避免一次占用任务太久 */
    if (n > 32)
    {
        n = 32;
    }

    for (i = 0; i < n; i++)
    {
        feed_byte(RX1_Buffer[i]);
    }

    /* 把剩下的字节往前搬，RX_Cnt 同步减少 */
    left = (u8)((u8)COM1.RX_Cnt - n);
    for (i = 0; i < left; i++)
    {
        RX1_Buffer[i] = RX1_Buffer[n + i];
    }
    COM1.RX_Cnt = left;
}

u16 Uart_FrameCount(void)
{
    return s_frames;
}

u16 Uart_ErrorCount(void)
{
    return s_errors;
}
