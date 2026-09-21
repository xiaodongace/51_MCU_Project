#ifndef __APP_MENU_H
#define __APP_MENU_H

#include "App_Public.h"

/* 菜单项目编号，编号顺序就是SPI屏幕上的显示顺序 */
#define MENU_ITEM_ALARM          0
#define MENU_ITEM_MUSIC_BOX      1
#define MENU_ITEM_PIANO          2
#define MENU_ITEM_DHT11          3
#define MENU_ITEM_MOTOR_SCOPE    4

/* 菜单项目数量 */
#define MENU_ITEM_COUNT          5

/*
 * 当前页面编号。
 * 音乐盒继续使用现有PAGE_BUZZER名称，避免修改已经完成的蜂鸣器任务。
 */
#define PAGE_MENU                0
#define PAGE_ALARM               1
#define PAGE_BUZZER              2
#define PAGE_PIANO               3
#define PAGE_DHT11               4
#define PAGE_MOTOR_SCOPE         5

/*
 * 当前菜单选中的项目
 * 由菜单任务修改
 */
extern volatile u8 menu_selected;

/*
 * 当前正在运行的页面
 * 其他功能任务根据它判断是否工作
 */
extern volatile u8 current_page;

/*
 * 刷新SPI屏幕菜单
 */
void Menu_Refresh(void);

#endif
