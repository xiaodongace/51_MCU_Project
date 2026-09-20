/*---------------------------------------------------------------------*/
/* App_Storage.c - 数据存储模块（角色 B）                               */
/* 实现 EEPROM 上设置存取、闹钟存取、温湿度日志追加                     */
/*                                                                     */
/* 作者：角色 B | 版本：V1.0 | 日期：2026-09-20                        */
/*---------------------------------------------------------------------*/

#include "Storage.h"
#include "EEPROM.h"
#include <string.h>   /* 仅使用 memcpy, 无动态分配 */

/*=====================================================================*/
/*                          内部辅助函数                                */
/*=====================================================================*/

/**
 * @brief 计算 Settings_t 的校验和
 *        规则: 对 [magic 之后, checksum 之前] 所有字节求和, 取低 8 位
 * @param s 要计算校验的 Settings 结构 (magic 已置, checksum 字段忽略)
 * @return u8 校验和
 * @note 此函数只读, 不改 EEPROM
 */
static u8 calc_checksum(const Settings_t *s) {
    /* 从 volume 开始 (magic 是第 0 字节, 不参与),
     * 到 checksum 之前结束 (checksum 是最后一字节, 不参与) */
    const u8 *p = (const u8 *)s;
    u16 sum = 0;
    u16 i;

    /* 跳过 magic (offset 0), 从 offset 1 开始 */
    for (i = 1; i < sizeof(Settings_t) - 1; i++) {
        sum += p[i];
    }
    return (u8)(sum & 0xFF);
}

/*=====================================================================*/
/*                          默认值初始化                                */
/*=====================================================================*/

void Storage_GetDefault(Settings_t *s) {
    u8 i;

    if (s == NULL) return;

    memset(s, 0, sizeof(Settings_t));

    /* 基本设置 */
    s->volume        = 5;      /* 中音量 */
    s->screen_on     = 1;      /* 主屏默认打开 */
    s->pomodoro_work = 25;     /* 默认 25 分钟 */
    s->pomodoro_rest = 5;      /* 默认 5 分钟 */
    s->mode          = 0;      /* 闹钟模式 */

    /* 日志指针归零 (从扇区 2 起始处开始写) */
    s->log_write_pos = 0;

    /* 8 组闹钟全部默认关闭, song_id=0 */
    for (i = 0; i < ALARM_MAX_COUNT; i++) {
        s->alarms[i].hour    = 7;
        s->alarms[i].minute  = 30;
        s->alarms[i].weekday = 0x3E;   /* 默认工作日 (bit1~bit5, 周一~周五) */
        s->alarms[i].enabled = 0;      /* 默认全部关闭, 用户手动开 */
        s->alarms[i].song_id = 0;
    }

    /* 注意: magic 和 checksum 由 Storage_SaveSettings 负责填充 */
}

/*=====================================================================*/
/*                         Settings 存取                                */
/*=====================================================================*/

/**
 * @brief 从 EEPROM 扇区 0 加载系统设置
 *        先读 magic, 再读完整结构, 最后算 checksum 交叉验证
 * @param s 调用者提供的 Settings_t 缓冲, 成功时被填充
 * @return SUCCESS(0) = 读到有效数据
 *         FAIL(-1)  = magic 或 checksum 不对, 调用者应改用默认值
 */
u8 Storage_LoadSettings(Settings_t *s) {
    u8 magic_byte;
    Settings_t tmp;

    if (s == NULL) return FAIL;

    /* 先读第一个字节 (magic), 快速判断 EEPROM 是否首次使用 */
    EEPROM_read_n(EEPROM_ADDR_SETTINGS, &magic_byte, 1);
    if (magic_byte != SETTINGS_MAGIC) {
        return FAIL;   /* 0xFF 或其他值 = 未初始化过 */
    }

    /* 再读完整结构 */
    EEPROM_read_n(EEPROM_ADDR_SETTINGS, (u8 *)&tmp, sizeof(Settings_t));

    /* 校验 checksum */
    if (tmp.checksum != calc_checksum(&tmp)) {
        return FAIL;
    }

    /* 拷贝给调用者 */
    memcpy(s, &tmp, sizeof(Settings_t));
    return SUCCESS;
}

/**
 * @brief 将系统设置写入 EEPROM 扇区 0
 *        会自动填充 magic 和 checksum, 调用者无需关心
 * @param s 要保存的设置 (内部 magic/checksum 会被覆盖)
 * @return SUCCESS(0) = 写入成功
 *         FAIL      = EEPROM 操作失败
 * @note 会擦除整个扇区 0 (512 字节), 但该扇区仅存放 Settings, 安全
 */
u8 Storage_SaveSettings(const Settings_t *s) {
    Settings_t tmp;

    if (s == NULL) return FAIL;

    /* 拷贝一份, 填上 magic 和 checksum 后再写 */
    memcpy(&tmp, s, sizeof(Settings_t));
    tmp.magic    = SETTINGS_MAGIC;
    tmp.checksum = calc_checksum(&tmp);

    /* 擦扇区 → 写结构体 */
    EEPROM_SectorErase(EEPROM_ADDR_SETTINGS);
    EEPROM_write_n(EEPROM_ADDR_SETTINGS, (u8 *)&tmp, sizeof(Settings_t));

    /* 读回验证, 防止写失败 */
    {
        Settings_t verify;
        EEPROM_read_n(EEPROM_ADDR_SETTINGS, (u8 *)&verify, sizeof(Settings_t));
        if (verify.magic != SETTINGS_MAGIC ||
            verify.checksum != calc_checksum(&verify)) {
            return FAIL;
        }
    }

    return SUCCESS;
}

/*=====================================================================*/
/*                          闹钟存取                                    */
/*=====================================================================*/

/**
 * @brief 读取全部 8 组闹钟
 *        实际操作: 从 Settings 的 alarms[] 字段拷贝
 * @param out 调用者提供的 Alarm_t[8] 数组
 * @return SUCCESS(0) = EEPROM 数据有效
 *         FAIL      = EEPROM 无效, 调用者应改用默认闹钟
 */
u8 Alarm_LoadAll(Alarm_t out[ALARM_MAX_COUNT]) {
    Settings_t s;
    u8 ret;

    if (out == NULL) return FAIL;

    ret = Storage_LoadSettings(&s);
    if (ret != SUCCESS) {
        return FAIL;
    }

    memcpy(out, s.alarms, sizeof(Alarm_t) * ALARM_MAX_COUNT);
    return SUCCESS;
}

/**
 * @brief 存一组闹钟
 *        会先加载当前 Settings, 修改指定索引, 再整体写回
 * @param idx 闹钟索引 0~7 (调用者自行保证范围)
 * @param a   要保存的闹钟数据
 * @return SUCCESS(0) = 保存成功
 *         FAIL      = EEPROM 操作失败
 */
u8 Alarm_Save(u8 idx, const Alarm_t *a) {
    Settings_t s;

    if (a == NULL || idx >= ALARM_MAX_COUNT) return FAIL;

    /* 先加载当前 Settings (确保保留其他字段和剩余 7 组闹钟) */
    if (Storage_LoadSettings(&s) != SUCCESS) {
        /* 首次上电, 先给一套默认值再改 */
        Storage_GetDefault(&s);
    }

    /* 替换目标闹钟 */
    s.alarms[idx] = *a;

    /* 整体写回 */
    return Storage_SaveSettings(&s);
}

/*=====================================================================*/
/*                       温湿度日志追加                                 */
/*=====================================================================*/

/**
 * @brief 追加一条温湿度记录到 EEPROM 环形缓冲区
 *        每次写入 2 字节, 自动管理写入位置和扇区擦除
 * @param temp 温度整数值 (建议用放大 10 倍后的整数, 如 253 表示 25.3℃)
 * @param humi 湿度整数值 (%)
 * @return SUCCESS(0) = 写入成功
 *         FAIL      = 写入失败
 * @note 环形回绕时会擦除最旧数据所在扇区, 自动覆盖
 *       每 10 分钟调用一次即可 (文档规定的采集频率)
 */
u8 Log_Append(u8 temp, u8 humi) {
    Settings_t s;
    u16 phys_addr;
    u16 pos;
    u8 record[2];

    record[0] = temp;
    record[1] = humi;

    /* 1. 读取当前写入位置 */
    if (Storage_LoadSettings(&s) != SUCCESS) {
        Storage_GetDefault(&s);   /* 首次上电, pos=0 */
    }

    pos = s.log_write_pos;

    /* 2. 计算物理地址 (从日志区起始偏移 pos 字节) */
    phys_addr = EEPROM_ADDR_LOG_START + pos;

    /* 3. 扇区擦除: 仅在写入位置正好落在扇区起始时才擦
     *    原因: STC8H EEPROM 写入前必须扇区擦除 (0→1),
     *    顺序追加时同一扇区首次写入前一次擦除即可;
     *    回绕到新扇区起始时必须重新擦除 */
    if ((pos % EEPROM_SECTOR_SIZE) == 0) {
        EEPROM_SectorErase(phys_addr);
    }

    /* 4. 写入 2 字节记录 */
    EEPROM_write_n(phys_addr, record, LOG_RECORD_SIZE);

    /* 5. 更新环形写入位置 (回绕到 0) */
    pos += LOG_RECORD_SIZE;
    if (pos >= EEPROM_LOG_CAPACITY) {
        pos = 0;
    }

    /* 6. 更新 Settings 的写入位置并保存 */
    s.log_write_pos = pos;
    return Storage_SaveSettings(&s);
}
