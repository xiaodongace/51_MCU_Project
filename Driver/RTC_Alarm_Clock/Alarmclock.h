/*---------------------------------------------------------------------*/
/* Alarmclock.h - PCF8563 RTC 时钟驱动（角色 B）                        */
/* 负责时钟芯片 I2C 读写、时间设置、闹钟触发、标志清理                   */
/*                                                                     */
/* 引脚: SCL=P3.2, SDA=P3.3, INT(闹钟中断)=P3.7                       */
/* 参考: 黑马 day10 PCF8563 驱动 + 《04-M1分工规格》接口约定             */
/* 作者：角色 B | 版本：V1.0 | 日期：2026-09-20                        */
/*---------------------------------------------------------------------*/

#ifndef __ALARMCLOCK_H__
#define __ALARMCLOCK_H__

#include "Storage.h"   /* 提供项目统一的 Clock_t / Alarm_t 定义 (Driver/Storage/) */

/*=====================================================================*/
/*                          PCF8563 寄存器定义                          */
/*=====================================================================*/

#define PCF8563_DEV_ADDR   (0x51 << 1)   /* 写地址 0xA2, 读 0xA3 (|1) */

/* 控制寄存器 (CS1, CS2) */
#define PCF8563_REG_CS1    0x00
#define PCF8563_REG_CS2    0x01
  /* CS2 位定义 */
  #define PCF8563_CS2_AF   (1 << 3)   /* Alarm Flag, 闹钟标志 */
  #define PCF8563_CS2_TF   (1 << 2)   /* Timer Flag */
  #define PCF8563_CS2_AIE  (1 << 1)   /* Alarm Interrupt Enable */
  #define PCF8563_CS2_TIE  (1 << 0)   /* Timer Interrupt Enable */

/* 时间寄存器: 秒分天时周月年 (从 0x02 开始连续 7 个) */
#define PCF8563_REG_SEC    0x02
/* 闹钟寄存器: 分 时 日 周 (从 0x09 开始连续 4 个) */
#define PCF8563_REG_ALM_MIN   0x09
#define PCF8563_REG_ALM_HOUR  0x0A
#define PCF8563_REG_ALM_DAY   0x0B
#define PCF8563_REG_ALM_WEEK  0x0C
  /* 闹钟寄存器 bit7: 0=启用该字段, 1=禁用 (无关) */
  #define PCF8563_ALARM_DISABLE  0x80

/*=====================================================================*/
/*                          接口声明                                   */
/*=====================================================================*/

/* 初始化 GPIO(P3.2/P3.3 I2C + P3.7 中断), I2C 外设 + EXT_INT3 下降沿 */
void PCF8563_Init(void);

/* 读出 PCF8563 当前时间, 自动 BCD→十进制 + year 解偏移
 * 读到的时间写入 t 指向的结构 */
void PCF8563_GetTime(Clock_t *t);

/* 将十进制时间写入 PCF8563, 自动 十进制→BCD + year 偏移编码 */
void PCF8563_SetTime(const Clock_t *t);

/* 写入并启用"下一组"闹钟到 PCF8563 硬件寄存器 (写 CS2.AIE=1)
 * 注: PCF8563 只有一套硬件闹钟寄存器, 此函数只负责把一组具体的
 *   h/m 写进去并开启中断; 8 组闹钟的调度挑选由上层 (App_Alarm) 完成 */
void PCF8563_SetAlarm(u8 hour, u8 minute);

/* 清 PCF8563 的 Alarm Flag (CS2.AF bit3)
 * 必须在 P3.7 外部中断服务里第一件事调用, 否则"门铃"会一直响 */
void PCF8563_ClearAlarmFlag(void);

#endif  /* __ALARMCLOCK_H__ */
