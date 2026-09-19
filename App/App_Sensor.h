/*
 * App_Sensor.h - 采集任务（DHT11 + NTC + 定时存档）
 */
#ifndef __APP_SENSOR_H
#define __APP_SENSOR_H

#include "App_Public.h"

/* 连续失败多少次之后认为湿度不可用（界面显示 "--"）——《04》F 角色第 4 条 */
#define HUMI_FAIL_LIMIT     5

/* 记录间隔（秒）：10 分钟 —— 《04》F 角色 F3 */
#define SENSOR_LOG_INTERVAL 600

/* 初始化两个传感器 */
void Sensor_Init(void);

/* 读一次两个传感器，更新全局值。返回 1 表示这一轮湿度有效 */
u8 Sensor_ReadOnce(void);

/* 湿度是否可用（连续失败次数未超限） */
u8 Sensor_HumiValid(void);

/* 由 TASK_SENSOR 每秒调用一次；内部自己数够 600 秒才写一条记录 */
u8 Sensor_Tick1s(void);

#endif
