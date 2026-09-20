/*---------------------------------------------------------------------*/
/* App_Storage.h - 数据存储模块（角色 B）                               */
/* 负责 EEPROM 分区规划、设置存取、闹钟存取、温湿度日志追加             */
/*                                                                     */
/* 作者：角色 B | 版本：V1.0 | 日期：2026-09-20                        */
/*---------------------------------------------------------------------*/

#ifndef __APP_STORAGE_H
#define __APP_STORAGE_H

#include "Config.h"    /* 提供 u8/u16/u32、SUCCESS/FAIL 等类型 */

/*=====================================================================*/
/*                        EEPROM 地址映射表                            */
/*  STC8H8K64U 内部 EEPROM 共 4KB (0x0000 ~ 0x0FFF), 8 个扇区,         */
/*  每扇区 512 字节。擦除单位为扇区 (512 字节)。                       */
/*---------------------------------------------------------------------*/
/*  扇区 0 (0x0000-0x01FF) : Settings_t  系统设置 + 8 组闹钟           */
/*  扇区 1 (0x0200-0x03FF) : 【保留给 M2 游戏存档, M1 禁止占用】       */
/*  扇区 2-7(0x0400-0x0FFF): 温湿度日志环形缓冲 (6 扇区, 3072 字节)    */
/*=====================================================================*/

#define EEPROM_SECTOR_SIZE      512u

/* Settings 扇区 (含 8 组闹钟, 一并读改写, 省去单独的 Alarm 扇区) */
#define EEPROM_ADDR_SETTINGS    0x0000u
#define SETTINGS_MAGIC          0xA5u       /* 魔数: 0xA5 = '有效数据' */

/* 日志环形缓冲 (从扇区 2 开始到 EEPROM 末尾) */
#define EEPROM_ADDR_LOG_START   0x0400u
#define EEPROM_ADDR_LOG_END     0x1000u              /* 不含, 0x0FFF+1 */
#define EEPROM_LOG_CAPACITY     (EEPROM_ADDR_LOG_END - EEPROM_ADDR_LOG_START)  /* 3072 字节 */
#define LOG_RECORD_SIZE         2u                   /* 每条: temp(1) + humi(1) */
#define LOG_MAX_RECORDS         (EEPROM_LOG_CAPACITY / LOG_RECORD_SIZE)  /* 1536 条 */

/*=====================================================================*/
/*                           数据结构定义                              */
/*=====================================================================*/

/* Alarm_t - 单组闹钟 (5 字节) */
typedef struct {
    u8 hour;        /* 0-23 */
    u8 minute;      /* 0-59 */
    u8 weekday;     /* 周几位图: bit0=周一, bit6=周日; 0=每天 */
    u8 enabled;     /* 0=关闭, 1=开启 */
    u8 song_id;     /* 0-2, 三首闹铃曲 */
} Alarm_t;

#define ALARM_MAX_COUNT     8u

/* Clock_t - 时钟 (PCF8563 读写用, 7 字节) */
typedef struct {
    u8 year;        /* 0-99  (2000 + year) */
    u8 month;       /* 1-12 */
    u8 day;         /* 1-31 */
    u8 weekday;     /* 0-6 */
    u8 hour;        /* 0-23 */
    u8 minute;      /* 0-59 */
    u8 second;      /* 0-59 */
} Clock_t;

/* Settings_t - 系统设置 (约 51 字节, 放扇区 0)
 * 布局: [magic(1)][field...][alarms[40]][checksum(1)]
 * checksum = 除 magic/checksum 外所有字节累加和取低 8 位
 */
typedef struct {
    u8  magic;              /* 必须等于 SETTINGS_MAGIC */
    u8  volume;             /* 音量 0-10 */
    u8  screen_on;          /* 主屏开关 0/1 */
    u8  pomodoro_work;      /* 番茄工作分钟数, 默认 25 */
    u8  pomodoro_rest;      /* 番茄休息分钟数, 默认 5 */
    u8  mode;               /* 预留: 0=闹钟, 1=游戏 */
    u8  reserved[2];        /* 预留填充 */
    u16 log_write_pos;      /* 日志环形缓冲内的写入偏移 (0~3071) */
    Alarm_t alarms[ALARM_MAX_COUNT];   /* 8 组闹钟 (40 字节) */
    u8  checksum;           /* 校验和 */
} Settings_t;

/*=====================================================================*/
/*                          函数声明                                  */
/*=====================================================================*/

/* ---- Settings 存取 ---- */
/* 返回 SUCCESS(0) = 读到有效数据; 返回 FAIL(-1) = EEPROM 无效,
 * 调用者应将 s 置为默认值后再使用 */
u8 Storage_LoadSettings(Settings_t *s);

/* 返回 SUCCESS(0) = 保存成功; 返回 FAIL = EEPROM 操作失败 */
u8 Storage_SaveSettings(const Settings_t *s);

/* ---- Alarm 存取 ---- */
/* 读取全部 8 组闹钟, 调用者提供 Alarm_t[8] 数组.
 * 返回 SUCCESS = 正常; FAIL = 无效 (调用者用默认闹钟) */
u8 Alarm_LoadAll(Alarm_t out[ALARM_MAX_COUNT]);

/* 存一组闹钟. idx: 0~7, 必须调用者自己保证范围.
 * 返回 SUCCESS = 正常; FAIL = EEPROM 写失败 */
u8 Alarm_Save(u8 idx, const Alarm_t *a);

/* ---- 温湿度日志 ---- */
/* 追加一条温湿度记录. temp: 温度整数值, humi: 湿度整数值.
 * 内部自动环形回绕 (覆盖最旧记录).
 * 返回 SUCCESS = 正常; FAIL = 写入失败 */
u8 Log_Append(u8 temp, u8 humi);

/*=====================================================================*/
/*                          默认值 (首次上电用)                        */
/*=====================================================================*/

/* 返回一个填好默认值的 Settings_t (magic 未设置, 需调用 SaveSettings) */
void Storage_GetDefault(Settings_t *s);

#endif  /* __APP_STORAGE_H */
