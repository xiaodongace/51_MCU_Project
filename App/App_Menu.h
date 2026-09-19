/*
 * App_Menu.h - 页面栈与页面处理（TASK_LOGIC 的界面部分）
 *
 * 页面栈像一个"盘子摞"：进一页往上摞一个盘子，按返回就拿掉最上面的。
 * 这样多级菜单不会迷路（《04》H 角色第 1 条）。
 *
 * 每个页面准备三件事：进来时干什么、收到按键干什么、每秒干什么。
 * 本工程用"页面编号 + 一个 switch"实现，没有用函数指针数组 ——
 * 理由：M1 只有 6 个页面，switch 更省 RAM、也更容易读懂；
 * 掌机模式（M2）要加 5 个页面时同样只是往 switch 里加 case，
 * 页面栈本身不用改。
 */
#ifndef __APP_MENU_H
#define __APP_MENU_H

#include "App_Public.h"

/* 初始化：清栈、定位到主界面 */
void Menu_Init(void);

/* 压入一页 */
void Menu_Push(UIPageId_t page);

/* 返回上一页（栈底不动） */
void Menu_Back(void);

/* 回到主界面（栈清到只剩主页） */
void Menu_Home(void);

/* 当前页面 */
UIPageId_t Menu_Current(void);

/*========================================================================
 *            主屏任务清单（"主屏 = 切换任务"的导航层）
 *
 * 主屏两级（用户 2026-09-19 定的模型）：
 *   第 0 级 = 大字时钟（最初始一级）
 *   第 1 级 = 任务清单（按 KEY2 从第 0 级进入）
 *   第 2 级及以上 = 任务内部（页面栈往上摞）
 *
 * 任务清单是"主屏画什么"的唯一来源，所以表放在本文件（导航层）；
 * 显示层只按它画字，两边不会再各维护一份。
 *========================================================================*/

#define MENU_TASK_COUNT     7

/* 设置项数量（第 7 点从 6 项加到 7 项，末尾是"震动强度"）。
 * 放在头文件里是因为显示层要按它画列表。 */
#define SET_ITEM_COUNT      7

/* 第 idx 个任务对应哪个页面 */
UIPageId_t Menu_TaskPage(u8 idx);

/* 第 idx 个任务的中文名（给主屏用，走屏上硬件字库） */
char *Menu_TaskName(u8 idx);

/* 某个页面属于清单里的第几项（闹钟编辑、响铃、游戏子页归位到父任务） */
u8 Menu_IndexOf(UIPageId_t p);

/* 主屏当前是不是停在"第 1 级 任务清单" */
u8 Menu_IsOpen(void);

/* 任务清单里的光标位置 */
u8 Menu_CursorIdx(void);

/*========================================================================
 *            闹钟页的两个状态（任务清单第 4 点）
 *
 * 原来"闹钟列表"和"闹钟编辑"是两个页面（还压栈），层级变成 4 级、
 * 从编辑回任务清单要按两次 KEY4。现在它们只是同一个页面的两个状态，
 * **不压栈** —— 层级回到 3 级，KEY4 一次就回任务清单。
 * 显示层要按状态画不同版面，所以把状态暴露出来。
 *========================================================================*/

/* 0 = 列表态（选哪一组），1 = 编辑态（调这一组的字段） */
u8 Menu_AlarmIsEditing(void);

/* 列表态：当前选中的是第几组 */
u8 Menu_AlarmSel(void);

/* 编辑态：正在编辑第几组 / 第几个字段（0..4 = 时/分/周/开关/曲目） */
u8 Menu_AlarmEditIndex(void);
u8 Menu_AlarmEditField(void);

/* 编辑态：编辑缓冲（还没写回 g_alarms，显示层要读它才看得到调整效果） */
const AlarmItem_t *Menu_AlarmEditBuf(void);

/* 番茄钟页的两个状态（同第 4 点的写法） */
u8 Menu_PomoIsEditing(void);
u8 Menu_PomoRunning(void);
u8 Menu_PomoIsRest(void);
u16 Menu_PomoRemain(void);
u8 Menu_PomoSel(void);

/*========================================================================
 *          「日期和时间」任务的状态（任务清单第 3 点）
 *
 * 两级：
 *   s_dtState = 0  选"改年月日 / 改时分"——**选项显示在 SPI 屏上**，I2C 仍显示图标
 *   s_dtState = 1  正在输入——SPI 只显示当前选项；I2C 显示输入反馈
 * 矩阵键盘输数字；KEY4 删掉最后一位，删完（位数为 0）就取消并退回上一级。
 * 位数用 s_dtCnt 单独计数，所以"输入全是 0"也要按对应次数才删得完。
 *========================================================================*/
u8  Menu_DateIsActive(void);
u8  Menu_DateState(void);       /* 0=选选项 1=输入中 */
u8  Menu_DateChoice(void);      /* 0=改年月日 1=改时分 */
u16 Menu_DateY(void);           /* 已输入的年 */
u16 Menu_DateMD(void);          /* 已输入的月日 */
u8  Menu_DateH(void);           /* 已输入的时 */
u8  Menu_DateM(void);           /* 已输入的分 */
u8  Menu_DateCnt(void);         /* 已输入位数（删除计数就靠它）*/

/* 设置页的两个状态（第 7 点）*/
u8 Menu_SetIsEditing(void);
u8 Menu_SetSel(void);

/* 处理一条输入事件 */




void Menu_OnEvent(const Event_t *evt);

/* 每秒调用一次（页面自己的秒级逻辑，例如番茄钟倒计时） */
void Menu_Tick1s(void);

/*
 * 空闲检查：30 秒没人操作就自动回主界面（《04》H 角色 H5）。
 * 响铃页和编辑页不参与空闲返回（正在操作的东西不能被弹走）。
 */
void Menu_CheckIdle(void);

#endif
