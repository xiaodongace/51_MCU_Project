/*
 * App_Sensor.c - 采集任务实现
 *
 * 时序开销说明（《05-M1集成验证》第四节专门分析过这一项）：
 *   DHT11_Read() 全程约 25ms，其中"关中断捂耳朵"约 4.6ms。
 *   影响有两处，都可接受：
 *     · 系统时钟少走 4 格 / 30 秒 = 0.013%，计时误差可忽略
 *     · 数码管暂停刷 4ms，一位本来停留 1ms，肉眼看不出闪
 *
 * 失败处理（《04》F 角色第 4 条）：
 *   读失败就沿用上一次的有效值；连续失败 5 次，把 g_humiValid 置 0，
 *   由界面显示 "--"（显示的事交给 App_Display，本模块不碰屏幕）。
 */
#include "App_Sensor.h"

#include "DHT11.h"
#include "NTC.h"
#include "App_Storage.h"

static u8 s_failCnt   = 0;
static u16 s_logTimer = 0;

void Sensor_Init(void)
{
    DHT11_Init();
    NTC_Init();

    s_failCnt  = 0;
    s_logTimer = 0;

    g_humiValid  = 0;
    g_humi       = 0;
    g_tempX10    = 0;
    g_dhtTempX10 = 0;
}

u8 Sensor_ReadOnce(void)
{
    s16 t = 0;
    u8  h = 0;
    u8  rst;

    /* NTC 温度：查表法，整数，几乎瞬时 */
    g_tempX10 = NTC_GetTempX10();

    /* DHT11 温湿度：单总线，会关中断约 4.6ms */
    rst = DHT11_Read(&t, &h);

    if (rst == 0)
    {
        s_failCnt    = 0;
        g_dhtTempX10 = t;
        g_humi       = h;
        g_humiValid  = 1;
        return 1;
    }

    /* 读失败：沿用上一次的有效值 */
    if (s_failCnt < 255)
    {
        s_failCnt++;
    }

    if (s_failCnt >= HUMI_FAIL_LIMIT)
    {
        g_humiValid = 0;        /* 界面据此显示 "--" */
    }

    return 0;
}

u8 Sensor_HumiValid(void)
{
    return g_humiValid;
}

u8 Sensor_Tick1s(void)
{
    u8 wrote = 0;

    Sensor_ReadOnce();

    s_logTimer++;
    if (s_logTimer >= SENSOR_LOG_INTERVAL)
    {
        s_logTimer = 0;

        /* 记录只存整数摄氏度（表查出来本来就是整数），湿度用整数部分 */
        Storage_AppendLog((s8)(g_tempX10 / 10), g_humiValid ? g_humi : 0);


        wrote = 1;
    }

    return wrote;
}

/*========================================================================
 *                       TASK_SENSOR：每秒一拍
 *========================================================================*/

void task_sensor(void) _task_ TASK_SENSOR
{
    /* 上电先等一会儿：屏幕初始化要做 I2C/SPI 通信，
     * 头一秒就跑 DHT11 会关中断 4.6ms，容易把屏幕初始化打断（v3.1 App_RTC.c 也这么处理）。 */
    os_wait2(K_TMO, 200);       /* 200 x 5ms = 1s */

    Sensor_Init();

    while (1)
    {
        Sensor_Tick1s();

        os_wait2(K_TMO, 200);   /* 200 x 5ms = 1s */
    }
}
