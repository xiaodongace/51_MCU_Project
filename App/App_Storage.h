/*
 * App_Storage.h - 掉电保存（片内 EEPROM 4KB @ 0xF000）
 *
 * 分区（每区 512 字节，正好是这颗芯片的最小擦除单位）：
 *
 *   0 号区 0xF000  设置 Settings_t + ALARM_MAX 组闹钟 AlarmItem_t[ALARM_MAX]（当前 3 组）
 *   1 号区 0xF200  【空着，留给游戏掌机存档】—— M1 期间一个字节都不写
 *   2 号区 0xF400  ┐
 *   3 号区 0xF600  │
 *   4 号区 0xF800  ├ 温湿度记录，顺序填写，2 字节一条
 *   5 号区 0xFA00  │   {温度(s8, 整数℃), 湿度(u8, 0..100)}
 *   6 号区 0xFC00  │   6 个区共 1536 条，10 分钟一条 -> 约 10.7 天
 *   7 号区 0xFE00  ┘   擦除后是 0xFF，湿度不可能等于 0xFF，用它当"空位"标记
 *
 * 为什么这样切（对齐《04-M1分工规格》B 角色注意事项）：
 *   "存数据前要擦干净整页再写（最小擦除单位 512 字节），所以先在内存里备好
 *     完整数据再擦，避免擦完写失败导致全丢。"
 *   —— 设置和闹钟放在同一区，一次擦除、一次写回，中间不会出现"擦了没写"的窗口。
 *
 * 记录为什么不用每次都回写 0 号区：
 *   记录区是先擦后写的顺序填充，已擦除的字节就是 0xFF，可以直接按字节写进去，
 *   不需要每写一条就擦一次整页。写指针存在 0 号区的 logCount 里，
 *   但只在"每 32 条"或"设置变更"时才回写 0 号区，
 *   避免 10 分钟擦一次 0 号区（那样 100k 次寿命只够两年）。
 */
#ifndef __APP_STORAGE_H
#define __APP_STORAGE_H

#include "App_Public.h"

#define EE_BASE         0xF000U     /* EEPROM 起始地址：64KB 从末尾往前划 4KB */
#define EE_PAGE_SIZE    512U        /* 最小擦除单位 */
#define EE_SETTINGS     0xF000U     /* 0 号区：设置 + 闹钟 */
#define EE_GAME_SAVE    0xF200U     /* 1 号区：游戏存档（M1 不碰） */
#define EE_LOG_BASE     0xF400U     /* 2 号区起：温湿度记录 */
#define EE_LOG_PAGES    6U          /* 2..7 共 6 个区 */
#define EE_LOG_PER_PAGE (EE_PAGE_SIZE / 2U)                 /* 每区 256 条 */
#define EE_LOG_CAPACITY (EE_LOG_PER_PAGE * EE_LOG_PAGES)    /* 共 1536 条 */

/* 返回值：0 = 成功，负数 = 失败 */
#define ST_OK           0
#define ST_ERR_MAGIC    (-1)        /* 0 号区没有有效数据（第一次上电） */

/* 上电装载：读设置与闹钟。数据无效时填入默认值（返回 ST_ERR_MAGIC）。 */
u8 Storage_Load(void);

/* 保存设置 + 闹钟（一次擦除 + 一次写回，原子性最好） */
u8 Storage_SaveAll(void);

/* 把内存里的设置和闹钟恢复成出厂默认值（不写 EEPROM） */
void Storage_Default(void);

/* 追加一条温湿度记录（temp 为整数摄氏度） */
u8 Storage_AppendLog(s8 temp, u8 humi);

/* 已写记录条数 */
u16 Storage_LogCount(void);

/*
 * 读第 idx 条记录（0 是最早的一条）。成功返回 1。
 * 记录区满 1536 条后会从头覆盖，这里按"逻辑序号"取最近的一条。
 */
u8 Storage_ReadLog(u16 idx, s8 *temp, u8 *humi);

#endif
