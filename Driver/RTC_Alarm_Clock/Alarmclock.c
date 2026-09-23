/*---------------------------------------------------------------------*/
/* Alarmclock.c - PCF8563 RTC 时钟驱动（角色 B）                        */
/* 实现时钟芯片 I2C 读写、时间设置、闹钟触发、标志清理                   */
/*                                                                     */
/* 参考: 黑马 day10 PCF8563 驱动                                       */
/* 适配: 《04-M1分工规格》接口约定 + App_Storage.h 数据结构             */
/* 作者：角色 B | 版本：V1.0 | 日期：2026-09-20                        */
/*                                                                     */
/* 依赖: GPIO.h, I2C.h, NVIC.h, Switch.h, Exti.h                      */
/*---------------------------------------------------------------------*/

#include "Alarmclock.h"
#include "GPIO.h"
#include "I2C.h"
#include "NVIC.h"
#include "Switch.h"
#include "Exti.h"

/*=====================================================================*/
/*                TODO: 总线锁 (角色 A 负责实现)                        */
/*  副屏 OLED 和 PCF8563 共用 P3.2/P3.3 I2C 总线                        */
/*  A 会提供 extern 函数; 在此之前用宏开关禁用, 避免链接错误            */
/*=====================================================================*/
#define ALARMCLOCK_USE_BUS_LOCK  0    /* 1=启用 (需要 A 先提供) */

#if ALARMCLOCK_USE_BUS_LOCK
extern void I2C_Lock(void);
extern void I2C_Unlock(void);
#define BUS_LOCK()   I2C_Lock()
#define BUS_UNLOCK() I2C_Unlock()
#else
#define BUS_LOCK()
#define BUS_UNLOCK()
#endif

/* RTC时间读写使用有界等待，避免I2C异常时卡住菜单任务。 */
#define RTC_I2C_WAIT_LIMIT  60000U

/* 等待硬件完成当前I2C命令；返回0表示超时。 */
static u8 RTC_I2C_WaitDone(void) {
    u16 remaining = RTC_I2C_WAIT_LIMIT;

    while ((I2CMSST & 0x40) == 0) {
        if (--remaining == 0) return 0;
    }
    I2CMSST &= ~0x40;
    return 1;
}

/* 执行一条I2C主机命令并检查其是否完成。 */
static u8 RTC_I2C_Command(u8 command) {
    I2CMSCR = command;
    return RTC_I2C_WaitDone();
}

/* 发送一个字节，并等待原驱动使用的ACK读取命令完成。 */
static u8 RTC_I2C_SendByte(u8 value) {
    I2CTXD = value;
    if (!RTC_I2C_Command(0x02)) return 0;
    return RTC_I2C_Command(0x03);
}

/* 事务失败时尝试发出STOP；即使总线仍异常，也会在超时后返回。 */
static void RTC_I2C_Abort(void) {
    RTC_I2C_Command(0x06);
}

/*=====================================================================*/
/*                        内部静态辅助函数                               */
/*=====================================================================*/

/**
 * @brief PCF8563 GPIO 配置
 *        P3.2/P3.3 开漏输出 (I2C 标准), P3.7 上拉输入 (INT)
 */
static void RTC_GPIO_Config(void) {
    GPIO_InitTypeDef st;

    /* I2C 两根线: 开漏输出 */
    st.Pin  = GPIO_Pin_2 | GPIO_Pin_3;
    st.Mode = GPIO_OUT_OD;
    GPIO_Inilize(GPIO_P3, &st);

    /* INT 线: 上拉输入 */
    st.Pin  = GPIO_Pin_7;
    st.Mode = GPIO_PullUp;
    GPIO_Inilize(GPIO_P3, &st);
}

/**
 * @brief I2C 外设初始化 + 引脚切换到 P3.2/P3.3
 *        400Kbit/s 速率 (Speed=13)
 */
static void RTC_I2C_Config(void) {
    I2C_InitTypeDef st;

    st.I2C_Mode      = I2C_Mode_Master;
    st.I2C_Enable    = ENABLE;
    st.I2C_MS_WDTA   = DISABLE;
    st.I2C_Speed     = 13;     /* 400Kbit/s @ 24MHz */

    I2C_Init(&st);
    I2C_SW(I2C_P33_P32);      /* SCL=P3.2, SDA=P3.3 */
}

/**
 * @brief 外部中断 INT3 配置 (P3.7, 下降沿触发)
 */
static void RTC_Exti_Config(void) {
    EXTI_InitTypeDef st;

    st.EXTI_Mode = EXT_MODE_Fall;
    Ext_Inilize(EXT_INT3, &st);
    NVIC_INT3_Init(ENABLE, Priority_0);
}

/*=====================================================================*/
/*                           接口实现                                   */
/*=====================================================================*/

/**
 * @brief PCF8563 初始化
 *        配好 GPIO → I2C 外设 → P3.7 外部中断
 */
void PCF8563_Init(void) {
    EAXSFR();                  /* STC8H 扩展寄存器访问使能, 必须先调 */
    RTC_GPIO_Config();
    RTC_I2C_Config();
    RTC_Exti_Config();
}

/*---------------------------------------------------------------------*/
/*                         时间读写 (BCD ? 十进制)                        */
/*---------------------------------------------------------------------*/

/**
 * @brief 将 Clock_t 十进制值打包成 7 字节 BCD 数组 (PCF8563 格式)
 * @param t   项目统一的 Clock_t (year 为 u8, 2000 偏移)
 * @param buf 输出缓冲, 7 字节 [秒分天时周月年]
 */
static void pack_bcd(const Clock_t *t, u8 buf[7]) {
    /* 秒: VL bit7 清 0 表示有效 */
    buf[0] = ((t->second / 10) << 4) | (t->second % 10);
    /* 分 */
    buf[1] = ((t->minute / 10) << 4) | (t->minute % 10);
    /* 时 */
    buf[2] = ((t->hour   / 10) << 4) | (t->hour   % 10);
    /* 日 */
    buf[3] = ((t->day    / 10) << 4) | (t->day    % 10);
    /* 周 (3bit) */
    buf[4] = t->weekday & 0x07;
    /* 月: century bit7 清 0 = 20xx 年 */
    buf[5] = ((t->month  / 10) << 4) | (t->month  % 10);
    /* 年 (year 已是 %100, 直接用) */
    buf[6] = ((t->year   / 10) << 4) | (t->year   % 10);
}

/**
 * @brief 将 PCF8563 的 7 字节 BCD 数组解包成项目 Clock_t
 * @param buf 输入缓冲, 7 字节 [秒分天时周月年]
 * @param t   输出的项目 Clock_t
 */
static void unpack_bcd(const u8 buf[7], Clock_t *t) {
    t->second  = ((buf[0] >> 4) & 0x07) * 10 + (buf[0] & 0x0F);
    t->minute  = ((buf[1] >> 4) & 0x07) * 10 + (buf[1] & 0x0F);
    t->hour    = ((buf[2] >> 4) & 0x03) * 10 + (buf[2] & 0x0F);
    t->day     = ((buf[3] >> 4) & 0x03) * 10 + (buf[3] & 0x0F);
    t->weekday =  buf[4] & 0x07;
    t->month   = ((buf[5] >> 4) & 0x01) * 10 + (buf[5] & 0x0F);
    /* PCF8563 年寄存器是 %100, 直接读就是项目需要的 u8 year */
    t->year    = ((buf[6] >> 4) & 0x0F) * 10 + (buf[6] & 0x0F);
}

/**
 * @brief 读出 PCF8563 当前时间, 自动 BCD→十进制
 */
u8 PCF8563_GetTime(Clock_t *t) {
    u8 buf[7];
    u8 i;

    if (t == NULL) return 0;

    BUS_LOCK();
    /* 沿用原I2C驱动的读时序，但每条硬件命令都有超时。 */
    if (!RTC_I2C_Command(0x01) ||
        !RTC_I2C_SendByte(PCF8563_DEV_ADDR) ||
        !RTC_I2C_SendByte(PCF8563_REG_SEC) ||
        !RTC_I2C_Command(0x01) ||
        !RTC_I2C_SendByte(PCF8563_DEV_ADDR | 1)) goto read_failed;

    for (i = 0; i < 7; i++) {
        if (!RTC_I2C_Command(0x04)) goto read_failed;
        buf[i] = I2CRXD;
        I2CMSST = (i == 6) ? 0x01 : 0x00;
        if (!RTC_I2C_Command(0x05)) goto read_failed;
    }

    if (!RTC_I2C_Command(0x06)) goto read_done;
    BUS_UNLOCK();
    unpack_bcd(buf, t);
    return 1;

read_failed:
    RTC_I2C_Abort();
read_done:
    BUS_UNLOCK();
    return 0;
}

/**
 * @brief 将十进制时间写入 PCF8563, 自动 十进制→BCD
 */
u8 PCF8563_SetTime(const Clock_t *t) {
    u8 buf[7];
    u8 i;

    if (t == NULL) return 0;

    pack_bcd(t, buf);

    BUS_LOCK();
    /* 沿用原I2C驱动的写时序，失败时不继续发送后续字节。 */
    if (!RTC_I2C_Command(0x01) ||
        !RTC_I2C_SendByte(PCF8563_DEV_ADDR) ||
        !RTC_I2C_SendByte(PCF8563_REG_SEC)) goto write_failed;

    for (i = 0; i < 7; i++) {
        if (!RTC_I2C_SendByte(buf[i])) goto write_failed;
    }

    if (!RTC_I2C_Command(0x06)) goto write_done;
    BUS_UNLOCK();
    return 1;

write_failed:
    RTC_I2C_Abort();
write_done:
    BUS_UNLOCK();
    return 0;
}

/*---------------------------------------------------------------------*/
/*                          闹钟写入与清理                                */
/*---------------------------------------------------------------------*/

/**
 * @brief 写入"下一组"闹钟到 PCF8563 硬件寄存器并启用中断
 *        只关心 h/m 两个字段 (bit7=0 启用), day/week 置 bit7=1 表示"不关心"
 *        同时写 CS2 清 AF 置 AIE=1
 *
 *  @param hour   闹钟小时 0-23
 *  @param minute 闹钟分钟 0-59
 *
 *  @note PCF8563 只有一套硬件闹钟寄存器, 每次写都会覆盖上一组
 *   8 组闹钟的"哪组先响"调度由上层 (App_Alarm) 完成,
 *   上层决定好后调本函数把那组 h/m 写进 PCF8563
 */
void PCF8563_SetAlarm(u8 hour, u8 minute) {
    u8 alarm[4];
    u8 cs2;

    /* 闹钟寄存器布局: [min, hour, day, week] */
    alarm[0] = (minute / 10 << 4) | (minute % 10);       /* bit7=0: 启用分钟匹配 */
    alarm[1] = (hour   / 10 << 4) | (hour   % 10);       /* bit7=0: 启用小时匹配 */
    alarm[2] = PCF8563_ALARM_DISABLE;                      /* bit7=1: 日期不关心 */
    alarm[3] = PCF8563_ALARM_DISABLE;                      /* bit7=1: 周几不关心 */

    BUS_LOCK();
    I2C_WriteNbyte(PCF8563_DEV_ADDR, PCF8563_REG_ALM_MIN, alarm, 4);

    /* 清 AF 置 AIE, 确保中断能再次触发 */
    I2C_ReadNbyte(PCF8563_DEV_ADDR, PCF8563_REG_CS2, &cs2, 1);
    cs2 &= ~PCF8563_CS2_AF;     /* 清 AF 标志 */
    cs2 |=  PCF8563_CS2_AIE;    /* 开 AIE 闹钟中断 */
    I2C_WriteNbyte(PCF8563_DEV_ADDR, PCF8563_REG_CS2, &cs2, 1);
    BUS_UNLOCK();
}

/**
 * @brief 清 PCF8563 的 Alarm Flag (CS2.AF bit3)
 *        必须在 P3.7 外部中断服务里第一件事调用
 */
void PCF8563_ClearAlarmFlag(void) {
    u8 cs2;

    BUS_LOCK();
    I2C_ReadNbyte(PCF8563_DEV_ADDR, PCF8563_REG_CS2, &cs2, 1);
    cs2 &= ~PCF8563_CS2_AF;
    I2C_WriteNbyte(PCF8563_DEV_ADDR, PCF8563_REG_CS2, &cs2, 1);
    BUS_UNLOCK();
}
