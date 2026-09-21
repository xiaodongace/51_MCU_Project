#include "App_Public.h"
#include "Key.h"
#include "Buzzer.h"
#include "NIXIE.h"
#include "LED.h"
#include "DHT_11.h"
#include "NTC.h"
#include "Uarts.h"
#include "Oscilloscope.h"
#include "Storage.h"
#include "Alarmclock.h"
#include "Delay.h"

/* 测试宏 (TEST_ 前缀避开 Config.h 的 FAIL=-1 冲突) */
#define TEST_PASS()   printf("  PASS\r\n")
#define TEST_FAIL()   printf("  FAIL\r\n")

void Sys_Init(void) {
	EA = 0;			// 关闭全局中断 初始化结束后统一开启
	EAXSFR();		/* 扩展寄存器访问使能 */

	/* 外设初始化 */
    Timers_Init();			// 定时器
    Key_Init();				// 独立按键
    Buzzer_Init();			// 蜂鸣器
    LED_Init();				// LED灯
    Nixie_init();			// 数码管
    DHT11_Init();			// DHT11
    NTC_init();				// NTC
	Oscilloscope_init();	// 电机
    PCF8563_Init();      // RTC 时钟
    
    Uarts_Init(UART_USE_1);	// 串口

	EA = 1;
	printf("=====Sys_Init=====\r\n");
}

// 这里函数名可随意, 建议不要使用start, 会和I2C.h里的Start冲突
void Main_Start() _task_ App_Main_Task_Id {
	/* 统一初始化入口 */ 
	Sys_Init();
	
	/* 创建任务入口 */
    // os_create_task(SPI_OLED_Task_ID);		// SPI显示
    // os_create_task(I2C_OLED_Task_ID);		// I2C显示

    // os_create_task(App_Oscilloscope_Task_Id);	// 电动马达
	// os_create_task(App_Buzzer_Task_Id);			// 蜂鸣器 + 小灯
	os_create_task(App_Storage_RTC_Test_Id);      // B 角色测试任务 (Storage + RTC)

	
	// os_create_task(App_Nixie_Task_Id);		// 数码管显示
	// os_create_task(App_DHT11_Task_Id);		// 温湿度—I2C_OLED
	
	/* 销毁任务 */
    os_delete_task(0);
}


/*---------------------------------------------------------------------*/
/* App_Storage_RTC_Test.c - Storage + RTC 综合测试任务 (角色 B)          */
/*                                                                     */
/* 测试清单:                                                           */
/*   [1] Storage_LoadSettings  — 首次上电应 FAIL                       */
/*   [2] Storage_SaveSettings  — 存一套默认值                          */
/*   [3] Storage_LoadSettings  — 再读应 SUCCESS, magic/checksum 对     */
/*   [4] Alarm_LoadAll         — 读 8 组 (默认 enabled=0)              */
/*   [5] Alarm_Save            — 改 idx=0 为 enabled=1, 7:30, song=2  */
/*   [6] Alarm_LoadAll         — 再读, 验证 idx=0 新值                 */
/*   [7] Log_Append            — 连续追加 5 条温湿度                    */
/*   [8] PCF8563_SetTime/GetTime — 写 → 读验证 BCD 转换                */
/*   [9] PCF8563_SetAlarm      — 写 "下一组" 闹钟寄存器                */
/*                                                                     */
/* 运行: printf 通过 UART1 (P3.0/P3.1, 115200) 输出 PASS/FAIL          */
/*---------------------------------------------------------------------*/



/* 存一份 Settings 到 EEPROM 并打印结果
 * 仅在【首次上电】时调用 (magic 不存在时) */
static void test_storage_first_boot(void) {
    Settings_t s;
    u8 ret;

    /* [1] 首次读应失败 (EEPROM 全 0xFF) */
    printf("\r\n=== [Storage] 首次上电检测 ===\r\n");
    ret = Storage_LoadSettings(&s);
    printf("  [1] Storage_LoadSettings (首次): ret=%d", (int)ret);
    if (ret == FAIL) TEST_FAIL(); else TEST_PASS();

    /* [2] 填默认值 + 存 */
    printf("\r\n=== [Storage] 写入默认 Settings ===\r\n");
    Storage_GetDefault(&s);
    s.volume        = 7;       /* 改一个值便于之后验证 */
    s.pomodoro_work  = 30;
    s.alarms[0].enabled = 1;   /* 把 idx=0 先开了 */
    s.alarms[0].hour    = 6;
    s.alarms[0].minute  = 45;
    s.alarms[0].song_id = 1;

    ret = Storage_SaveSettings(&s);
    printf("  [2] Storage_SaveSettings: ret=%d", (int)ret);
    if (ret == SUCCESS) TEST_PASS(); else TEST_FAIL();
}

static void test_storage_load_verify(void) {
    Settings_t s;
    u8 ret;

    /* [3] 再读, 验证 magic/checksum + 字段值 */
    printf("\r\n=== [Storage] 读回验证 ===\r\n");
    ret = Storage_LoadSettings(&s);
    printf("  [3] Storage_LoadSettings (读回): ret=%d", (int)ret);
    if (ret != SUCCESS) { TEST_FAIL(); return; } else TEST_PASS();

    printf("      magic=0x%02X (期望 0x%02X)", (int)s.magic, (int)SETTINGS_MAGIC);
    if (s.magic == SETTINGS_MAGIC) TEST_PASS(); else TEST_FAIL();

    printf("      volume=%d (期望 7)", (int)s.volume);
    if (s.volume == 7) TEST_PASS(); else TEST_FAIL();

    printf("      pomodoro_work=%d (期望 30)", (int)s.pomodoro_work);
    if (s.pomodoro_work == 30) TEST_PASS(); else TEST_FAIL();
}

static void test_alarm_rw(void) {
    Alarm_t alarms[ALARM_MAX_COUNT];
    u8 ret;
    u8 i;

    printf("\r\n=== [Alarm] 读写 8 组 ===\r\n");

    /* [4] 读全部 8 组 */
    ret = Alarm_LoadAll(alarms);
    printf("  [4] Alarm_LoadAll: ret=%d", (int)ret);
    if (ret == SUCCESS) TEST_PASS(); else TEST_FAIL();

    /* 打印当前状态 */
    for (i = 0; i < ALARM_MAX_COUNT; i++) {
        printf("      [%d] %02d:%02d en=%d song=%d\r\n",
               (int)i, (int)alarms[i].hour, (int)alarms[i].minute,
               (int)alarms[i].enabled, (int)alarms[i].song_id);
    }

    /* [5] 改 idx=0 */
    {
        Alarm_t a_new;
        a_new.hour    = 7;
        a_new.minute  = 30;
        a_new.weekday = 0x3E;   /* 工作日 */
        a_new.enabled = 1;
        a_new.song_id = 2;
        ret = Alarm_Save(0, &a_new);
        printf("  [5] Alarm_Save(idx=0, 7:30, song=2): ret=%d", (int)ret);
        if (ret == SUCCESS) TEST_PASS(); else TEST_FAIL();
    }

    /* [6] 再读, 验证 idx=0 */
    ret = Alarm_LoadAll(alarms);
    printf("  [6] 再读 Alarm_LoadAll: ret=%d", (int)ret);
    if (ret != SUCCESS) { TEST_FAIL(); return; } else TEST_PASS();

    printf("      alarms[0]=%02d:%02d en=%d song=%d",
           (int)alarms[0].hour, (int)alarms[0].minute,
           (int)alarms[0].enabled, (int)alarms[0].song_id);
    if (alarms[0].hour    == 7 &&
        alarms[0].minute  == 30 &&
        alarms[0].enabled == 1 &&
        alarms[0].song_id == 2) TEST_PASS(); else TEST_FAIL();
}

static void test_log_append(void) {
    u8 ret;
    u8 i;

    printf("\r\n=== [Log_Append] 追加 5 条 ===\r\n");
    for (i = 0; i < 5; i++) {
        /* temp 放大 10 倍: 253=25.3℃; humi=% */
        ret = Log_Append(250 + i, 60 + i);
        printf("  [7.%d] Log_Append(25%d, %d%%): ret=%d %s\r\n",
               (int)i, (int)i, 60 + (int)i, (int)ret,
               ret == SUCCESS ? "PASS" : "FAIL");
    }
}

static void test_pcf8563_time(void) {
    Clock_t c_set, c_get;

    printf("\r\n=== [RTC] SetTime → GetTime ===\r\n");

    /* [8a] 构造十进制时间, 写入 */
    c_set.year    = 26;     /* 2026 */
    c_set.month   = 9;
    c_set.day     = 20;
    c_set.weekday = 6;      /* 周六 (0=周日 → 6=周五? 看 PCF8563 周寄存器: 0=周日, 1=周一... 6=周六) */
    c_set.hour    = 10;
    c_set.minute  = 30;
    c_set.second  = 45;

    printf("  [8a] PCF8563_SetTime -> %04d-%02d-%02d %02d:%02d:%02d week=%d\r\n",
           2000 + (int)c_set.year, (int)c_set.month, (int)c_set.day,
           (int)c_set.hour, (int)c_set.minute, (int)c_set.second, (int)c_set.weekday);
    PCF8563_SetTime(&c_set);

    /* 等一小下让 PCF8563 同步 */
    os_wait2(K_TMO, 20);   /* 约 100ms */

    /* [8b] 读回并逐项对比 (秒可能会变, 放宽秒的检查) */
    PCF8563_GetTime(&c_get);
    printf("  [8b] PCF8563_GetTime -> %04d-%02d-%02d %02d:%02d:%02d week=%d\r\n",
           2000 + (int)c_get.year, (int)c_get.month, (int)c_get.day,
           (int)c_get.hour, (int)c_get.minute, (int)c_get.second, (int)c_get.weekday);

    printf("      year  %d vs %d", (int)c_get.year,    (int)c_set.year);
    if (c_get.year    == c_set.year)    TEST_PASS(); else TEST_FAIL();

    printf("      month %d vs %d", (int)c_get.month,   (int)c_set.month);
    if (c_get.month   == c_set.month)   TEST_PASS(); else TEST_FAIL();

    printf("      day   %d vs %d", (int)c_get.day,      (int)c_set.day);
    if (c_get.day     == c_set.day)     TEST_PASS(); else TEST_FAIL();

    printf("      hour  %d vs %d", (int)c_get.hour,    (int)c_set.hour);
    if (c_get.hour    == c_set.hour)    TEST_PASS(); else TEST_FAIL();

    printf("      min   %d vs %d", (int)c_get.minute,  (int)c_set.minute);
    if (c_get.minute  == c_set.minute)  TEST_PASS(); else TEST_FAIL();

    printf("      sec   %d vs %d (允许 ±2s)", (int)c_get.second, (int)c_set.second);
    {
        int8 diff = (int8)((int)c_get.second - (int)c_set.second);
        if (diff < 0) diff = -diff;
        if (diff <= 2) TEST_PASS(); else TEST_FAIL();
    }
}

static void test_pcf8563_alarm(void) {
    printf("\r\n=== [RTC] SetAlarm ===\r\n");

    /* [9] 写一组闹钟到 PCF8563 硬件寄存器, 并开启中断
     *     由于没有接蜂鸣器回调, 我们只验证函数能返回且不会卡死
     *     (实际触发需 P3.7 外部中断, 中断里第一件事调 PCF8563_ClearAlarmFlag) */
    PCF8563_SetAlarm(10, 31);
    printf("  [9] PCF8563_SetAlarm(10, 31) 写入 + 启用 AIE -> OK\r\n");

    /* 延时让 PCF8563 走到 10:31 附近 (手动中断测试) */
    printf("      等待 65s 让闹钟触发... 若已过点请忽略\r\n");
    os_wait2(K_TMO, 13000);  /* RTX51 tick 5ms: 13000 * 5ms = 65s */

    /* 清标志 (防止下次再触发) */
    PCF8563_ClearAlarmFlag();
    printf("  [9b] PCF8563_ClearAlarmFlag -> OK\r\n");
}

/*=====================================================================*/
/*                        测试任务主函数                                */
/*=====================================================================*/

void Task_Storage_RTC_Test() _task_ App_Storage_RTC_Test_Id {
    printf("\r\n");
    printf("========================================================\r\n");
    printf("   Storage + RTC 综合测试 (角色 B)\r\n");
    printf("========================================================\r\n");

    /* 先探测: 是否首次上电 (magic 不存在) */
    {
        Settings_t s_probe;
        u8 first_boot = (Storage_LoadSettings(&s_probe) != SUCCESS);
        printf("\r\n[Probe] first_boot=%d (%s)\r\n",
               (int)first_boot, first_boot ? "EEPROM 空, 将写默认值" : "已有数据");

        /* 为了每次测试都有可预测的起点, 强制重新写一套 */
        Storage_GetDefault(&s_probe);
        s_probe.volume = 7;
        s_probe.pomodoro_work = 30;
        s_probe.alarms[0].enabled = 1;
        s_probe.alarms[0].hour    = 6;
        s_probe.alarms[0].minute  = 45;
        s_probe.alarms[0].song_id = 1;
        if (Storage_SaveSettings(&s_probe) == SUCCESS) {
            printf("[Reset] 已强制写一套默认 Settings (volume=7)\r\n");
        } else {
            printf("[Reset] Storage_SaveSettings 失败!\r\n");
        }
    }

    test_storage_load_verify();   /* 读回验证 */
    test_alarm_rw();              /* Alarm 读写 */
    test_log_append();            /* 日志追加 */
    test_pcf8563_time();          /* RTC SetTime/GetTime */
    /* 闹钟触发等 65s, 放最后 */
    test_pcf8563_alarm();

    printf("\r\n========================================================\r\n");
    printf("   全部测试完成\r\n");
    printf("========================================================\r\n");

    /* 跑完后空转, 让串口一直能看到时间 */
    while (1) {
        Clock_t now;
        PCF8563_GetTime(&now);
        printf("  [RTC] %04d-%02d-%02d %02d:%02d:%02d\r\n",
               2000 + (int)now.year, (int)now.month, (int)now.day,
               (int)now.hour, (int)now.minute, (int)now.second);
        os_wait2(K_TMO, 1000);   /* 每 5s 打一次 (RTX51 tick 5ms) */
    }
}
