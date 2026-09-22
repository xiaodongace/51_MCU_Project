/*
 * App_Public.c - App 层公共全局对象的唯一定义处
 *
 * 说明：所有跨模块共享的变量都在这里定义，其它文件只用 extern 声明（见 App_Public.h）。
 *       这样避免"多处定义"或"只声明未定义"（现有工程 main.c 的 ledger 里就有这类问题）。
 */
#include "App_Public.h"

/* 1ms 系统时钟。Timer3 中断里自增，其它模块只读。
 * volatile：中断会改它，编译器不许把它缓存进寄存器。 */
volatile u32 g_sysTick = 0;

/* 当前时间，由 TASK_LOGIC 每秒刷新 */
Clock_t g_clock;

/* 掉电保存的设置 */
Settings_t g_settings;

/* 8 组闹钟 */
AlarmItem_t g_alarms[ALARM_MAX];

/* 响铃状态：0=没响；非 0 = 正在响的闹钟下标 + 1 */
u8 g_ringingIdx = 0;

/* 传感器最新值 */
s16 g_tempX10    = 0;       /* NTC 温度 x10 */
s16 g_dhtTempX10 = 0;       /* DHT11 温度 x10 */
u8  g_humi       = 0;       /* DHT11 湿度 */
u8  g_humiValid  = 0;       /* DHT11 最近一次读数是否有效 */

/* I2C 访问请求标志（定义说明见 App_Public.h） */
volatile bit g_reqClockRefresh = 0;
volatile bit g_reqClockSet     = 0;
volatile bit g_reqClockRecover = 0;
volatile bit g_reqAlarmApply   = 0;
volatile bit g_reqRtcIrq       = 0;

/* 【2026-09-22 清理】此处原来的 g_timeEntry / g_timeEntryCnt 已删除（理由见 App_Public.h）。 */

/*
 * 读取系统时钟（毫秒）。
 * 读 u32 变量时可能刚好被中断打断，导致读到"高 16 位已进位、低 16 位还没进位"的中间值。
 * 处理办法：连读两次，直到两次一致为止——这是读多字节共享变量的标准做法。
 */
u32 SysTick_Get(void)
{
    u32 a;
    u32 b;

    do
    {
        a = g_sysTick;
        b = g_sysTick;
    } while (a != b);

    return a;
}
