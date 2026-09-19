/*
 * App_Menu.c - 页面栈、事件分发、TASK_LOGIC
 *
 * 按键约定（4 个独立键）—— 2026-09-19 UI 改造后的定稿：
 *   KEY1 = 上移 / 上一个          KEY3 = 下移 / 下一个
 *   KEY2 = 确认 / 进入下一级      KEY4 = 返回上一级
 *
 * 主屏两级：
 *   第 0 级 = 大字时钟（最初始一级）
 *   第 1 级 = 任务清单（KEY2 进入；KEY1/KEY3 挪光标；KEY2 进任务；KEY4 回第 0 级）
 *   第 2 级及以上 = 任务内部（页面栈往上摞）
 * 响铃是紧急状态，键位单独定义：KEY2 = 停止响铃，KEY4 = 贪睡。
 *
 * 【重要】下面各页面的 case 分支内部用的仍是**旧键位编号**
 * （旧 K1 = 进入、旧 K2 = 返回、旧 K3 = 上移、旧 K4 = 下移），
 * 靠 Menu_OnEvent 里那一次 key_remap() 统一翻译过去。
 * 不要逐个分支去改 —— 5 个页面十几个分支，改必漏。
 *
 * 矩阵键盘：闹钟编辑页用来直接输入数字。
 * 旋钮：主界面调音量。
 *
 * 硬规矩（《04》H 角色"铁律"）：本文件里不出现任何寄存器操作，
 * 也不直接碰屏幕/蜂鸣器，全部通过别人的函数。
 */
#include "App_Menu.h"

#include "App_Clock.h"
#include "App_Alarm.h"
#include "App_Display.h"
#include "App_Input.h"
#include "App_Music.h"
#include "App_Sensor.h"
#include "App_Storage.h"
#include "App_Uart.h"
#include "App_Game.h"
#include "Servo.h"
#include "Motor.h"
#include "Buzzer.h"      /* 云台：进页面初始化 PWM、离开时关输出 */
#include "LED.h"        /* 上电进度指示收尾 + 按键事件指示灯 */
#include "NixieScan.h"  /* 数码管内容更新 */

#define MENU_STACK_MAX  4

/* 云台页每按一次键走几度（放这里是为了和菜单表在一起，方便调整） */
#define SERVO_KEY_STEP  10    /* 用户要求：云台每次改 10 度 */

static UIPageId_t xdata s_stack[MENU_STACK_MAX];
static u8 s_depth = 0;

/* 空闲计时 */
static u32 s_lastActMs = 0;

/*----------------------------------------------------------------------
 * 列表 / 编辑时的选择状态
 *----------------------------------------------------------------------*/
static u8 s_listSel   = 0;      /* 闹钟列表选中的下标 */
static u8 s_alarmEdit = 0;      /* 闹钟页状态：0 = 列表态，1 = 编辑态（不压栈）*/

/* ---- 「日期和时间」任务的状态（第 3 点）---- */
static u8  s_dtActive = 0;      /* 1 = 正在这个任务里 */
static u8  s_dtState  = 0;      /* 0 = 选选项，1 = 输入中 */
static u8  s_dtChoice = 0;      /* 0 = 改年月日，1 = 改时分 */
static u16 s_dtY      = 0;      /* 已输入的年 */
static u16 s_dtMD     = 0;      /* 已输入的月日 */
static u8  s_dtH      = 0;      /* 已输入的时 */
static u8  s_dtM      = 0;      /* 已输入的分 */
static u8  s_dtCnt    = 0;      /* 已输入位数 */
static u8 s_editIdx   = IDX_NONE;
static u8 s_editField = 0;      /* 0=时 1=分 2=星期 3=开关 4=曲目 */
static AlarmItem_t s_editBuf;

/* 星期预设，按 KEY3/KEY4 轮换 */
static u8 code s_dayPreset[] =
{
    DAY_ALL, DAY_WORKDAY, DAY_WEEKEND,
    DAY_SUN, DAY_MON, DAY_TUE, DAY_WED, DAY_THU, DAY_FRI, DAY_SAT
};
#define DAY_PRESET_COUNT    (sizeof(s_dayPreset) / sizeof(s_dayPreset[0]))

/* 在星期预设列表里往前/往后走一格。dir = 1 前进，0 后退 */
static u8 menu_day_step(u8 cur, u8 dir)
{
    u8 i;

    for (i = 0; i < (u8)DAY_PRESET_COUNT; i++)
    {
        if (s_dayPreset[i] == cur)
        {
            if (dir)
            {
                return s_dayPreset[(u8)((i + 1U) % DAY_PRESET_COUNT)];
            }
            return s_dayPreset[(u8)((i + DAY_PRESET_COUNT - 1U) % DAY_PRESET_COUNT)];
        }
    }

    return s_dayPreset[0];
}

/*----------------------------------------------------------------------
 * 番茄钟
 *----------------------------------------------------------------------*/
static u8  s_pomoRunning = 0;
static u8  s_pomoRest    = 0;       /* 0=专注阶段 1=休息阶段 */
static u16 s_pomoRemain  = 0;       /* 剩余秒数 */
static u8  s_pomoDone    = 0;       /* 已完成几个专注段 */
static u8  s_pomoEdit    = 0;       /* 番茄钟页状态：0 = 列表态，1 = 编辑态（不压栈）*/
static u8  s_pomoSel     = 0;       /* 列表态选中的项：0=FOCUS 1=REST 2=RUN */

/*----------------------------------------------------------------------
 * 设置页
 *----------------------------------------------------------------------*/
static u8 s_setSel = 0;
static u8 s_setEdit = 0;    /* 设置页状态：0 = 列表态，1 = 编辑态（不压栈）*/
static u32 s_volSaveMs = 0;   /* 音量上次存 EEPROM 的时刻，避免每次拧都擦写 */
/* 设置项顺序（SET_ITEM_COUNT 在 App_Menu.h 里，显示层也要用）：
 *   0 音量(%)  1 曲目  2 提醒方式  3 主屏开关  4 日出唤醒  5 贪睡时长  6 震动强度(%)
 * 音量 / 震动强度 用**电位器**调（1% 步进），其余用 KEY1/KEY3 调。 */

/*----------------------------------------------------------------------
 * 主屏任务清单的两级状态（函数体在下面"主屏任务清单"一节）
 * 放在这里是因为 Menu_Init / Menu_Back / Menu_Home 都要用它们，
 * 而 C51 不允许"先用后声明"。
 *----------------------------------------------------------------------*/
static u8 s_menuOpen   = 0;     /* 0 = 第 0 级（大字时钟），1 = 第 1 级（任务清单） */
static u8 s_menuCursor = 0;     /* 任务清单里的光标位置 */

/*========================================================================
 *                              页面栈
 *========================================================================*/

void Menu_Init(void)
{
    s_depth = 0;
    s_stack[0] = PAGE_HOME;
    s_depth = 1;

    /* 开机停在最初始一级：大字时钟（菜单关闭） */
    s_menuOpen   = 0;
    s_menuCursor = 0;

    s_lastActMs = SysTick_Get();

    /* 初始化时同步给显示层，副屏标题才对 */
    Display_SetPage(PAGE_HOME);
}

UIPageId_t Menu_Current(void)
{
    if (s_depth == 0)
    {
        return PAGE_HOME;
    }

    return s_stack[s_depth - 1];
}

/*========================================================================
 *      任务页的"进入 / 离开"钩子 —— 照 demo30 的做法统一挂在这里
 *
 * 参考工程 demo30 的模型（用户 2026-09-19 要求照做）：
 *     进入任务 -> os_create_task(TASK_xxx)，任务体第一件事就是把该任务的外设
 *                 **完整配置一遍**（GPIO_config + PWM_config + ADC_config …）；
 *     离开任务 -> TASK_xxx_reset()：~关掉外设~（如 PWMB_CC6E_Disable）+ os_delete_task()。
 *   也就是 **"进入 = 完整初始化，离开 = 彻底收尾"**，不依赖上一次留下的状态。
 *
 * 本工程是页面栈模型（进一层 / 退一层 / 清栈三种走法），所以把这一对动作
 * 挂在这里、由 Menu_Push / Menu_Back / Menu_Home 三处统一调用 ——
 * **不要散在各个页面的按键分支里**。云台就漏过一次：
 * 在云台页被闹钟打断 -> 响铃页 -> 停止后 Menu_Home 清栈，
 * 云台页被弹出去了，但它的外设没人关，舵机就一直挂在总线上。
 *========================================================================*/

/* 进入某个任务页时要做的事 */
static void menu_task_enter(UIPageId_t pg)
{
    switch (pg)
    {
    case PAGE_SERVO:
        Servo_Init();
        break;

    default:
        break;
    }
}

/* 离开某个任务页时要做的事（收尾 + 把引脚恢复到安全电平） */
static void menu_task_leave(UIPageId_t pg)
{
    switch (pg)
    {
    case PAGE_SERVO:
        Servo_Off();
        break;

    case PAGE_HOME:
        /* 【第 3 点】离开"日期和时间"任务 = 退出它的子模式 */
        if (s_dtActive)
        {
            s_dtActive = 0;
            s_dtState  = 0;
        }
        break;

    default:
        break;
    }
}

void Menu_Push(UIPageId_t page)
{
    /* 【照 demo30】进入任务 = 先把它的外设完整初始化，再谈显示 */
    menu_task_enter(page);

    if (s_depth >= MENU_STACK_MAX)
    {
        /* 栈满：把最上面那层换掉，避免溢出 */
        s_stack[MENU_STACK_MAX - 1] = page;
    }
    else
    {
        s_stack[s_depth] = page;
        s_depth++;
    }

    Display_SetPage(page);
    Display_RequestFull();

    /* 页面切换给一声提示音 —— 没有这一下，用户按了键只能靠"盯第 1 行字母有没有变"
     * 判断有没有生效（真机反馈原话："只切换最上面一行字母，没有听到蜂鸣器响"）。 */
    Music_Beep(40);
}

void Menu_Back(void)
{
    UIPageId_t leaving;

    if (s_depth <= 1)
    {
        return;
    }

    leaving = Menu_Current();       /* 先记下"从哪一页退出来" */
    menu_task_leave(leaving);
    s_depth--;

    /* 离开页面时把"试听"停掉。
     * 之前没有这一步，用户在设置页试听曲目之后，按任何键都停不下来 ——
     * 因为试听是通过 Music_Play 起的，而所有按键路径都不调 Music_Stop。
     * 响铃中不能停（那是闹钟，得按 KEY1/KEY2 才停）。 */
    if (!Alarm_IsRinging())
    {
        Music_Stop();
    }

    /* 【2026-09-19 S2】"返回上一级"落回主界面时，退到的是
     * **第 1 级 任务清单**，不是直接跳回大字时钟 ——
     * 用户原话："KEY4 返回上一级菜单"。再按一次 KEY4 才回第 0 级。 */
    if (Menu_Current() == PAGE_HOME)
    {
        /* 【2026-09-19 修】光标要停在**刚离开的那个任务**上。
         * 原来这里写的是 Menu_IndexOf(PAGE_HOME) —— 而此刻当前页已经是主页了，
         * 算出来恒为 0，于是"从第三级返回第二级时光标又跳回第一项"。
         * 真机现象就是这个。改用 leaving（pop 之前的页面）来算。 */
        s_menuOpen   = 1;
        s_menuCursor = Menu_IndexOf(leaving);
    }

    Display_SetPage(Menu_Current());
    Display_RequestFull();
    Music_Beep(40);
}

void Menu_Home(void)
{
    /* 【照 demo30 · 关键修正】清栈时**栈里每一层都要收尾**，不能只看当前页。
     * 真机漏过一次：在云台页被闹钟打断 -> 响铃页 -> 停止后回主页，
     * 当前页是响铃页，于是云台页被弹出去了却没人关它的舵机 PWM，
     * 舵机就一直挂在总线上（表现为"没进云台页舵机也会动"）。 */
    while (s_depth > 0)
    {
        s_depth--;
        menu_task_leave(s_stack[s_depth]);
    }

    s_depth = 1;
    s_stack[0] = PAGE_HOME;

    /* 回"最初始一级" = 大字时钟（用户 2026-09-19 定的：大字时钟就是第 0 级） */
    s_menuOpen = 0;

    if (!Alarm_IsRinging())
    {
        Music_Stop();
    }

    Display_SetPage(PAGE_HOME);
    Display_RequestFull();
}

/*========================================================================
 *                     主屏任务清单（导航层）
 *========================================================================*/

UIPageId_t Menu_TaskPage(u8 idx)
{
    switch (idx)
    {
    case 0: return PAGE_HOME;
    case 1: return PAGE_ALARM_LIST;
    case 2: return PAGE_POMODORO;
    case 3: return PAGE_RANGE;
    case 4: return PAGE_SERVO;
    case 5: return PAGE_SETTINGS;
    case 6: return PAGE_GAME_HALL;
    default: return PAGE_HOME;
    }
}

char *Menu_TaskName(u8 idx)
{
    switch (idx)
    {
    case 0: return "日期和时间";
    case 1: return "闹钟";
    case 2: return "番茄钟";
    case 3: return "测距仪";
    case 4: return "云台";
    case 5: return "设置";
    case 6: return "游戏";
    default: return "----";
    }
}

u8 Menu_IndexOf(UIPageId_t p)
{
    u8 i;

    /* 子页面归位到父任务，否则光标会"跑出菜单" */
    switch (p)
    {
    case PAGE_ALARM_EDIT: return 1;     /* 闹钟任务的子页 */
    case PAGE_RINGING:    return 0;     /* 响铃归到"时钟"任务 */
    case PAGE_GAME_SNAKE:
    case PAGE_GAME_BRICK:
    case PAGE_GAME_PLANE:
    case PAGE_GAME_DAILY:
    case PAGE_GAME_OVER:  return 6;     /* 游戏任务的各子页 */
    default: break;
    }

    for (i = 0; i < MENU_TASK_COUNT; i++)
    {
        if (Menu_TaskPage(i) == p)
        {
            return i;
        }
    }
    return 0;
}

u8 Menu_IsOpen(void)
{
    return s_menuOpen;
}

u8 Menu_CursorIdx(void)
{
    return s_menuCursor;
}

/* 从任务清单进入某个任务。
 * 进来时要顺手把该任务的运行状态复位 —— 原来这些复位写在 PAGE_HOME 的
 * 四个按键分支里，现在统一收在这里，就不会出现"从菜单进番茄钟没复位"这种漏。 */
static void menu_enter(u8 idx)
{
    UIPageId_t pg = Menu_TaskPage(idx);

    s_menuOpen = 0;

    switch (pg)
    {
    case PAGE_HOME:
        /* 【第 3 点】"日期和时间"任务：
         * 不摞页面，而是进入"日期和时间"模式 —— SPI 显示选项、I2C 显示图标，
         * 由 menu_task_enter/leave 的钩子负责收尾。 */
        s_dtActive = 1;
        s_dtState  = 0;
        s_dtChoice = 0;
        s_dtCnt    = 0;
        Display_RequestMain();
        Display_RequestFull();
        Music_Beep(20);
        break;

    case PAGE_ALARM_LIST:
        s_listSel   = 0;
        s_alarmEdit = 0;            /* 从任务清单进来总是先看列表 */
        Menu_Push(pg);
        break;

    case PAGE_POMODORO:
        s_pomoRunning = 0;
        s_pomoRest    = 0;
        s_pomoRemain  = (u16)g_settings.pomodoro_work * 60U;
        s_pomoEdit    = 0;
        s_pomoSel     = 0;
        Menu_Push(pg);
        break;

    case PAGE_SETTINGS:
        s_setSel  = 0;
        s_setEdit = 0;
        Menu_Push(pg);
        break;

    default:
        Menu_Push(pg);
        break;
    }
}

/*----------------------------------------------------------------------
 * 【2026-09-19 UI 改造 S2 · 全局按键置换】
 *
 * 用户定的新键位：  K1 = 上移   K3 = 下移   K2 = 确认/进入   K4 = 返回
 * 本工程原来的键位：K1 = 进入   K3 = 上移   K2 = 返回        K4 = 下移
 * （K3/K4 在编辑页是"值−/值+"，同样属于"上/下"这一族）
 *
 * 两者正好是一个置换：新1=旧3、新2=旧1、新3=旧4、新4=旧2。
 * 所以不逐个 case 去改（那要动 5 个页面、十几个分支，改必漏一处），
 * 只在这一处把事件号翻译过去 —— 所有页面自动跟着走。
 *----------------------------------------------------------------------*/
static u8 key_remap(u8 t)
{
    switch (t)
    {
    case EVT_KEY1:      return EVT_KEY3;
    case EVT_KEY2:      return EVT_KEY1;
    case EVT_KEY3:      return EVT_KEY4;
    case EVT_KEY4:      return EVT_KEY2;

    case EVT_KEY1_LONG: return EVT_KEY3_LONG;
    case EVT_KEY2_LONG: return EVT_KEY1_LONG;
    case EVT_KEY3_LONG: return EVT_KEY4_LONG;
    case EVT_KEY4_LONG: return EVT_KEY2_LONG;

    default:            return t;
    }
}

/*========================================================================
 *                              工具
 *========================================================================*/

static void touch(void)
{
    s_lastActMs = SysTick_Get();
}

void Menu_CheckIdle(void)
{
    UIPageId_t p;

    p = Menu_Current();

    /* 响铃页、编辑页不参与空闲返回 —— 正在操作的事不能被弹走 */
    if (p == PAGE_RINGING || p == PAGE_ALARM_EDIT || p == PAGE_POMODORO)
    {
        return;
    }

    if (p == PAGE_HOME)
    {
        return;
    }

    if (SysTick_Elapsed(s_lastActMs) >= (u32)(IDLE_BACK_HOME_SEC * 1000U))
    {
        Menu_Home();
    }
}

/*========================================================================
 *                            事件分发
 *========================================================================*/

/*========================================================================
 *            「日期和时间」任务的按键与输入（任务清单第 3 点）
 *========================================================================*/

/* 提交：位数够了就校验 + 写芯片。合法回选项选择态，非法长鸣并留在输入态 */
static void dt_commit(void)
{
    u8 ok = 0;

    if (s_dtChoice == 0)
    {
        u16 y  = s_dtY;
        u8  mo = (u8)(s_dtMD / 100U);
        u8  d  = (u8)(s_dtMD % 100U);

        if (y >= 2000 && y <= 2099 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31)
        {
            g_clock.year  = y;
            g_clock.month = mo;
            g_clock.day   = d;
            Clock_Set(&g_clock);        /* 内部会按年月日重算周几 */
            ok = 1;
            printf("[DT] date set %04u-%02u-%02u\r\n", (unsigned)y, (unsigned)mo, (unsigned)d);
        }
    }
    else
    {
        if (s_dtH <= 23 && s_dtM <= 59)
        {
            g_clock.hour   = s_dtH;
            g_clock.minute = s_dtM;
            g_clock.second = 0;
            Clock_Set(&g_clock);
            Alarm_ApplyHardware();
            ok = 1;
            printf("[DT] time set %02u:%02u\r\n", (unsigned)s_dtH, (unsigned)s_dtM);
        }
    }

    if (ok)
    {
        Music_Beep(80);
        s_dtState = 0;              /* 成功 -> 回选项选择 */
    }
    else
    {
        /* 非法：长音提示，**留在输入态**让用户接着删掉重输 */
        Music_Beep(200);
        printf("[DT] invalid, ignored\r\n");
    }

    Display_RequestMain();
    Display_RequestFull();
}

/* 矩阵键盘输入一位 */
static void dt_input(u8 digit)
{
    if (s_dtChoice == 0)            /* 8 位：前 4 位年，后 4 位月日 */
    {
        if (s_dtCnt < 4) { s_dtY  = (u16)(s_dtY  * 10U + digit); }
        else             { s_dtMD = (u16)(s_dtMD * 10U + digit); }
        s_dtCnt++;
        Music_Beep(15);
        if (s_dtCnt >= 8) { dt_commit(); return; }
    }
    else                            /* 4 位：前 2 位时，后 2 位分 */
    {
        if (s_dtCnt < 2) { s_dtH = (u8)(s_dtH * 10U + digit); }
        else             { s_dtM = (u8)(s_dtM * 10U + digit); }
        s_dtCnt++;
        Music_Beep(15);
        if (s_dtCnt >= 4) { dt_commit(); return; }
    }

    Display_RequestFull();
}

/* KEY4：删掉最后一位。删到 0 位就取消修改、退回上一级（选项选择态）。
 * 位数用 s_dtCnt 单独计数，所以"全是 0"也要按对应次数才删得完。 */
static void dt_delete(void)
{
    if (s_dtCnt == 0)
    {
        s_dtState = 0;              /* 已经删完 -> 退回选项选择 */
        Music_Beep(20);
        Display_RequestMain();
        Display_RequestFull();
        return;
    }

    s_dtCnt--;

    if (s_dtChoice == 0)
    {
        if (s_dtCnt < 4) { s_dtY  = (u16)(s_dtY  / 10U); }
        else             { s_dtMD = (u16)(s_dtMD / 10U); }
    }
    else
    {
        if (s_dtCnt < 2) { s_dtH = (u8)(s_dtH / 10U); }
        else             { s_dtM = (u8)(s_dtM / 10U); }
    }

    Music_Beep(15);
    Display_RequestFull();
}

/* 「日期和时间」任务的全部按键（**新键位编号**：K1=上移 K3=下移 K2=确认 K4=返回）*/
static void date_time_key(const Event_t *evt)
{
    if (s_dtState != 0)
    {
        /* ---- 输入态：只认矩阵键盘数字 和 KEY4 删位 ---- */
        if (evt->type == EVT_MATRIX)
        {
            if (evt->param < 10) { dt_input((u8)evt->param); }
        }
        else if (evt->type == EVT_KEY4)
        {
            dt_delete();
        }
        return;
    }

    /* ---- 选项选择态 ---- */
    if (evt->type == EVT_KEY1 || evt->type == EVT_KEY3)
    {
        s_dtChoice = (u8)(!s_dtChoice);     /* 两个选项来回切 */
        Music_Beep(15);
        Display_RequestMain();
        Display_RequestFull();
    }
    else if (evt->type == EVT_KEY2)
    {
        /* 确认进入输入：清零各值和位数 */
        s_dtState = 1;
        s_dtY = 0; s_dtMD = 0; s_dtH = 0; s_dtM = 0; s_dtCnt = 0;
        Music_Beep(40);
        Display_RequestMain();
        Display_RequestFull();
    }
    else if (evt->type == EVT_KEY4)
    {
        /* 返回上一级 = 回任务清单 */
        s_dtActive = 0;
        s_menuOpen = 1;
        s_menuCursor = Menu_IndexOf(PAGE_HOME);
        Display_RequestMain();
        Display_RequestFull();
        Music_Beep(20);
    }
}

u8  Menu_DateIsActive(void) { return s_dtActive; }
u8  Menu_DateState(void)    { return s_dtState; }
u8  Menu_DateChoice(void)   { return s_dtChoice; }
u16 Menu_DateY(void)        { return s_dtY; }
u16 Menu_DateMD(void)       { return s_dtMD; }
u8  Menu_DateH(void)        { return s_dtH; }
u8  Menu_DateM(void)        { return s_dtM; }
u8  Menu_DateCnt(void)      { return s_dtCnt; }

/*========================================================================
 *                  设置页：调一项的值、状态查询（第 7 点）
 *========================================================================*/
static void settings_step(u8 up)
{
    switch (s_setSel)
    {
    case 0:     /* 音量 0..100，步进 1% */
        if (up) { if (g_settings.volume  < VOLUME_MAX) { g_settings.volume++;  } }
        else    { if (g_settings.volume  > 0)          { g_settings.volume--;  } }
        break;

    case 1:     /* 曲目：换一首并试听 */
        if (up) { g_settings.song = (u8)((g_settings.song + 1) % SONG_COUNT); }
        else    { g_settings.song = (u8)((g_settings.song + SONG_COUNT - 1) % SONG_COUNT); }
        Music_Play((u8)(g_settings.song + 1), g_settings.volume);
        break;

    case 2:     /* 提醒方式 */
        g_settings.alert_mode = (g_settings.alert_mode == ALERT_RING) ? ALERT_VIBRATE : ALERT_RING;
        break;

    case 3:     /* 主屏开关 */
        g_settings.screen_on = (u8)(!g_settings.screen_on);
        Display_MainPower(g_settings.screen_on);
        break;

    case 4:     /* 日出唤醒 */
        g_settings.sunrise_en = (u8)(!g_settings.sunrise_en);
        break;

    case 5:     /* 贪睡时长 1..30 */
        if (up) { g_settings.snooze_min = (u8)((g_settings.snooze_min % 30) + 1); }
        else    { g_settings.snooze_min = (u8)(((g_settings.snooze_min + 28) % 30) + 1); }
        break;

    default:    /* 震动强度 0..100，步进 1% */
        if (up) { if (g_settings.vibrate < 100) { g_settings.vibrate++; } }
        else    { if (g_settings.vibrate > 0)   { g_settings.vibrate--; } }
        break;
    }

    Music_Beep(15);
}

u8 Menu_SetIsEditing(void) { return s_setEdit; }
u8 Menu_SetSel(void)       { return s_setSel; }

/*========================================================================
 *                     闹钟页：列表态 / 编辑态的按键
 *========================================================================*/

static void alarm_list_key(const Event_t *evt)
{
    switch (evt->type)
    {
    case EVT_KEY3:                  /* 新 KEY1 = 上移 */
        if (s_listSel > 0) { s_listSel--; } else { s_listSel = (u8)(ALARM_MAX - 1); }
        Display_RequestFull();
        break;

    case EVT_KEY4:                  /* 新 KEY3 = 下移 */
        s_listSel++;
        if (s_listSel >= ALARM_MAX) { s_listSel = 0; }
        Display_RequestFull();
        break;

    case EVT_KEY1:                  /* 新 KEY2 = 进入编辑（只切状态，不压栈）*/
        s_editIdx   = s_listSel;
        s_editBuf   = g_alarms[s_listSel];
        s_editField = 0;
        s_alarmEdit = 1;
        Music_Beep(20);
        Display_RequestFull();
        break;

    case EVT_KEY1_LONG:             /* 新 KEY2 长按 */
    case EVT_KEY4_LONG:             /* 新 KEY3 长按 —— 开关这组闹钟 */
        Alarm_Toggle(s_listSel);
        Music_Beep(60);
        Display_RequestFull();
        break;

    case EVT_MATRIX:                /* 矩阵键盘直接跳到对应组 */
        if (evt->param < ALARM_MAX)
        {
            s_listSel = evt->param;
            Display_RequestFull();
        }
        break;

    default:
        break;
    }
}

/* 编辑态：调字段的值。"返回并保存"统一由外面的 EVT_KEY2 处理，这里不碰 */
static void alarm_edit_key(const Event_t *evt)
{
    switch (evt->type)
    {
    case EVT_KEY1:                  /* 新 KEY2 = 下一个字段 */
        s_editField++;
        if (s_editField > 4)
        {
            Alarm_Set(s_editIdx, &s_editBuf);
            s_alarmEdit = 0;
            Menu_Back();
            return;
        }
        Display_RequestFull();
        break;

    case EVT_KEY3:                  /* 新 KEY1 = 上移 = 值减 */
        switch (s_editField)
        {
        case 0: s_editBuf.hour   = (u8)((s_editBuf.hour + 23) % 24); break;
        case 1: s_editBuf.minute = (u8)((s_editBuf.minute + 59) % 60); break;
        case 2: s_editBuf.days   = menu_day_step(s_editBuf.days, 0); break;
        case 3: s_editBuf.enable = (u8)(!s_editBuf.enable); break;
        case 4: s_editBuf.song   = (u8)((s_editBuf.song + SONG_COUNT) % (SONG_COUNT + 1)); break;
        default: break;
        }
        Display_RequestFull();
        break;

    case EVT_KEY4:                  /* 新 KEY3 = 下移 = 值加 */
        switch (s_editField)
        {
        case 0: s_editBuf.hour   = (u8)((s_editBuf.hour + 1) % 24); break;
        case 1: s_editBuf.minute = (u8)((s_editBuf.minute + 1) % 60); break;
        case 2: s_editBuf.days   = menu_day_step(s_editBuf.days, 1); break;
        case 3: s_editBuf.enable = (u8)(!s_editBuf.enable); break;
        case 4: s_editBuf.song   = (u8)((s_editBuf.song + 1) % (SONG_COUNT + 1)); break;
        default: break;
        }
        Display_RequestFull();
        break;

    case EVT_MATRIX:                /* 矩阵键盘直接输数字：在"时/分"字段上按 0-9 会填进去 */
        if (evt->param < 10)
        {
            if (s_editField == 0 && evt->param < 3)
            {
                s_editBuf.hour = (u8)((s_editBuf.hour % 10) * 10 + evt->param);
                if (s_editBuf.hour > 23) { s_editBuf.hour = evt->param; }
            }
            else if (s_editField == 1 && evt->param < 6)
            {
                s_editBuf.minute = (u8)((s_editBuf.minute % 10) * 10 + evt->param);
                if (s_editBuf.minute > 59) { s_editBuf.minute = evt->param; }
            }
            Display_RequestFull();
        }
        break;

    default:
        break;
    }
}

u8 Menu_AlarmIsEditing(void) { return s_alarmEdit; }
u8 Menu_AlarmSel(void)       { return s_listSel; }
u8 Menu_AlarmEditIndex(void) { return s_editIdx; }
u8 Menu_AlarmEditField(void) { return s_editField; }
const AlarmItem_t *Menu_AlarmEditBuf(void) { return &s_editBuf; }

u8 Menu_PomoIsEditing(void) { return s_pomoEdit; }
u8 Menu_PomoRunning(void)   { return s_pomoRunning; }
u8 Menu_PomoIsRest(void)    { return s_pomoRest; }
u16 Menu_PomoRemain(void)   { return s_pomoRemain; }
u8 Menu_PomoSel(void)       { return s_pomoSel; }

void Menu_OnEvent(const Event_t *evt)
{
    UIPageId_t p;
    Event_t    remap;       /* 按键置换后的副本，见下面 key_remap() 的说明 */

    if (evt == NULL)
    {
        return;
    }

    touch();

    /* 【诊断】按键事件回声。默认关闭（DEBUG_EVT_ECHO = 0）：
     * 按键通路已验证通过，不必再让第 2 颗灯随按键翻转。
     * 以后再排查"按键没反应"时把它改成 1 即可。 */
#if DEBUG_EVT_ECHO
    if (evt->type >= EVT_KEY1 && evt->type <= EVT_KEY4_LONG)
    {
        static u8 s_dbgLed = 0;
        s_dbgLed = (u8)(!s_dbgLed);
        Led_SetSingle(1, s_dbgLed);
    }
    if (evt->type != EVT_POT)       /* 旋钮太频繁，不打日志 */
    {
        printf("[EVT] type=%d param=%d page=%d\r\n",
               (int)evt->type, (int)evt->param, (int)Menu_Current());
    }
#endif

    /* ---------- 全局事件：任何页面都生效 ---------- */

    /* 响铃中：KEY1 停止、KEY2 贪睡，其它键忽略 */
    if (Alarm_IsRinging())
    {
        /* 【2026-09-19 S2】响铃是紧急状态，键位单独定义、不走下面的置换：
         *   KEY2（确认）= 停止闹铃        KEY4（返回）= 贪睡，推到下一轮
         * 其余键忽略。 */
        if (evt->type == EVT_KEY2 || evt->type == EVT_KEY2_LONG)
        {
            Alarm_Stop();
            Menu_Home();
        }
        else if (evt->type == EVT_KEY4)
        {
            Alarm_Snooze();
            Menu_Home();
        }
        return;
    }

    /* ---------- 「日期和时间」任务：它自己吃所有按键（第 3 点）---------- */
    if (s_dtActive)
    {
        date_time_key(evt);
        return;
    }

    /* ---------- 旋钮：**只在"设置"页的编辑态**才响应（第 7 点）----------
     * 用户要求："当在外面旋转电位器时不会影响到已经设置好的百分比"。
     * 所以这里加了"页面 + 状态"的双重守卫：不在设置页编辑态就直接返回，
     * 外面怎么拧都不会动音量/震动强度。
     * 另外只有当前项是"音量"或"震动强度"时才由旋钮接管，其它项旋钮不动它。 */
    if (evt->type == EVT_POT)
    {
        /* 只要**人在"设置"页**就响应（列表态、编辑态都算）——
         * 用户的原话是"进入这一级菜单后旋转电位器调节音量"，
         * 不该再多一道"先按 KEY2 进编辑态"的门槛。
         * 但**离开设置页就一律不响应** —— 这才是用户要的
         * "在外面旋转电位器时不会影响到已经设置好的百分比"。 */
        if (Menu_Current() != PAGE_SETTINGS)
        {
            return;                             /* 外面拧 -> 什么都不做 */
        }

        if (s_setSel == 0)
        {
            g_settings.volume  = evt->param;    /* 0..100，1% 步进 */
            Display_RequestMain();
            Display_RequestFull();
        }
        else if (s_setSel == 6)
        {
            g_settings.vibrate = evt->param;
            Display_RequestMain();
            Display_RequestFull();
        }
        return;
    }


    /* 【2026-09-19 删除 · 用户要求】
     * 用户原话："只有在进入任务 0 才能修改时间，其他任何地方不准修改时间。"
     * 所以这里原来那段"任何页面都能用矩阵键盘输 HHMM 校时"的全局通路整块删掉了。
     * 改时间的唯一入口是第 0 个任务「日期和时间」（点 3 做）。 */

    /*========================================================================
     * 【2026-09-19 UI 改造 S2】主屏两级 —— 第 0 级大字时钟 / 第 1 级任务清单
     *
     * 用户定的模型：
     *   第 0 级（最初始）= 大字时钟
     *   KEY2 → 第 1 级任务清单（光标可上下移动）
     *   KEY2 → 第 2 级：进入该任务（页面栈往上摞）
     *   KEY4 → 返回上一级；在第 1 级按 KEY4 就回到第 0 级的大字时钟
     *
     * 这一段是本轮新写的，用的是**新键位编号**；
     * 下面的页面分支是旧代码，用旧编号 —— 中间靠 key_remap() 翻译。
     *========================================================================*/
    if (Menu_Current() == PAGE_HOME && evt->type >= EVT_KEY1 && evt->type <= EVT_KEY4)
    {
        if (!s_menuOpen)
        {
            /* 第 0 级：大字时钟。只有 KEY2 有定义。
             * KEY1/KEY3 在这里没有"上一个/下一个任务"可言；
             * KEY4 已经在最顶层，也无处可返回。 */
            if (evt->type == EVT_KEY2)
            {
                s_menuCursor = Menu_IndexOf(PAGE_HOME);
                s_menuOpen   = 1;
                Display_RequestMain();      /* 主屏：把菜单画出来 */
                Display_RequestFull();      /* 副屏：从"时钟内容"换成"任务图标"，要清屏 */
                Music_Beep(20);
            }
            return;
        }

        /* 第 1 级：任务清单 */
        switch (evt->type)
        {
        case EVT_KEY1:                      /* 光标上移（到顶回绕） */
            if (s_menuCursor == 0)
            {
                s_menuCursor = (u8)(MENU_TASK_COUNT - 1U);
            }
            else
            {
                s_menuCursor--;
            }
            Display_RequestMain();      /* 主屏：菜单光标 */
            Display_RequestIcon();      /* 副屏：换成这个任务的图标 */
            break;

        case EVT_KEY3:                      /* 光标下移（到底回绕） */
            if (s_menuCursor >= (u8)(MENU_TASK_COUNT - 1U))
            {
                s_menuCursor = 0;
            }
            else
            {
                s_menuCursor++;
            }
            Display_RequestMain();      /* 主屏：菜单光标 */
            Display_RequestIcon();      /* 副屏：换成这个任务的图标 */
            break;

        case EVT_KEY2:                      /* 进入该任务 */
            menu_enter(s_menuCursor);
            break;

        case EVT_KEY4:                      /* 回第 0 级：大字时钟 */
            s_menuOpen = 0;
            Display_RequestMain();          /* 主屏：回到大字时钟 */
            Display_RequestFull();          /* 副屏：从"任务图标"换回"时钟内容"，要清屏 */
            Music_Beep(20);
            break;

        default:
            break;
        }
        return;
    }

    /*========================================================================
     * 【S2 · 全局按键置换】把"新键位"翻译成各页面分支原先用的"旧键位"，
     * 理由见上面 key_remap() 的注释。
     * 必须放在第 0/1 级处理**之后**：上面是新代码（新编号），下面是旧代码（旧编号）。
     *========================================================================*/
    remap.type  = key_remap(evt->type);
    remap.param = evt->param;
    evt = &remap;

    p = Menu_Current();

    switch (p)
    {
    /*============================================================
     * 主界面：第 0/1 级已在上面处理完。走到这里说明是长按之类的事件，
     * 本页不响应。
     *============================================================*/
    case PAGE_HOME:
        break;

    /*============================================================
     * 云台（舵机）—— 2026-09-19 新增
     * 键位经上面的置换后：EVT_KEY3 = 新 KEY1（上移），
     *                    EVT_KEY4 = 新 KEY3（下移），
     *                    EVT_KEY1 = 新 KEY2（确认），
     *                    EVT_KEY2 = 新 KEY4（返回）。
     *============================================================*/
    case PAGE_SERVO:
    {
        u8 a = Servo_GetAngle();

        switch (evt->type)
        {
        case EVT_KEY3:                  /* 新 KEY1 = 上移 = 角度减小 */
            Servo_SetAngle((a >= SERVO_KEY_STEP) ? (u8)(a - SERVO_KEY_STEP) : 0);
            Display_RequestGauge();
            break;

        case EVT_KEY4:                  /* 新 KEY3 = 下移 = 角度增大 */
            Servo_SetAngle((a <= (u8)(180 - SERVO_KEY_STEP)) ? (u8)(a + SERVO_KEY_STEP) : 180);
            Display_RequestGauge();
            break;

        case EVT_KEY1:                  /* 新 KEY2 = 确认 = 回中位 90° */
            Servo_SetAngle(90);
            Display_RequestGauge();
            break;

        case EVT_KEY2:                  /* 新 KEY4 = 返回上一级（会顺手 Servo_Off） */
            Menu_Back();
            break;

        default:
            break;
        }
        break;
    }

    /*============================================================
     * 测距仪（超声波）—— 2026-09-19 新增
     * 目前只接上"返回上一级"，S4 再填测距逻辑。
     *============================================================*/
    case PAGE_RANGE:
        switch (evt->type)
        {
        case EVT_KEY2:                  /* 新 KEY4 = 返回上一级 */
            Menu_Back();
            break;

        default:
            break;
        }
        break;

    /*============================================================
     * 闹钟 —— 一个页面、两个状态【任务清单第 4 点】
     *
     * 原来"列表"和"编辑"是两个页面并且压栈，层级是
     *   任务清单 → 闹钟列表 → 闹钟编辑
     * 从编辑回任务清单要按两次 KEY4（用户明确抱怨过）。
     * 现在它们只是同一页的 s_alarmEdit 两个状态，**不压栈**：
     *   任务清单 → 闹钟页（列表态 ⇄ 编辑态）
     * KEY4 在**任何状态**都一次回任务清单 —— 这一页的上一级就是任务清单。
     *============================================================*/
    case PAGE_ALARM_LIST:
    {
        /* 新 KEY4 = 返回：一次回任务清单（两个状态都这样）。
         * 编辑态下顺带把改动写回 —— 用户按 KEY4 就是"我改完了"。 */
        if (evt->type == EVT_KEY2)
        {
            if (s_alarmEdit)
            {
                Alarm_Set(s_editIdx, &s_editBuf);
                s_alarmEdit = 0;
            }
            Menu_Back();
            break;
        }

        if (s_alarmEdit)
        {
            alarm_edit_key(evt);
        }
        else
        {
            alarm_list_key(evt);
        }
        break;
    }

    /*============================================================
     * 番茄钟 —— 一个页面、两个状态【任务清单第 5 点】
     * 用户要求"和 1 闹钟 一样"：列表 + 选中项靠左、其余靠右。
     *============================================================*/
    case PAGE_POMODORO:
    {
        /* 新 KEY4 = 返回：一次回任务清单（两个状态都这样）*/
        if (evt->type == EVT_KEY2)
        {
            s_pomoEdit = 0;
            Storage_SaveAll();      /* 改过的时长落盘 */
            s_pomoRunning = 0;
            Menu_Back();
            break;
        }

        if (!s_pomoEdit)
        {
            /* ---- 列表态：选 FOCUS / REST / RUN ---- */
            if (evt->type == EVT_KEY3)
            {
                if (s_pomoSel > 0) { s_pomoSel--; } else { s_pomoSel = 2; }
                Display_RequestFull();
            }
            else if (evt->type == EVT_KEY4)
            {
                s_pomoSel++;
                if (s_pomoSel > 2) { s_pomoSel = 0; }
                Display_RequestFull();
            }
            else if (evt->type == EVT_KEY1)
            {
                if (s_pomoSel == 2)
                {
                    /* RUN 项：直接开始/暂停 */
                    s_pomoRunning = (u8)(!s_pomoRunning);
                }
                else
                {
                    s_pomoEdit = 1;     /* 前两项：进编辑态 */
                }
                Music_Beep(20);
                Display_RequestFull();
            }
            break;
        }

        /* ---- 编辑态 ---- */
        if (evt->type == EVT_KEY1)          /* 新 KEY2 = 确认回列表 */
        {
            s_pomoEdit = 0;
            Display_RequestFull();
        }
        else if (evt->type == EVT_KEY3)     /* 新 KEY1 = 值减 / 切换 */
        {
            if (s_pomoSel == 0 && g_settings.pomodoro_work > 1)
            {
                g_settings.pomodoro_work--;
                if (!s_pomoRunning) { s_pomoRemain = (u16)g_settings.pomodoro_work * 60U; }
            }
            else if (s_pomoSel == 1 && g_settings.pomodoro_rest > 1)
            {
                g_settings.pomodoro_rest--;
            }
            else if (s_pomoSel == 2)
            {
                s_pomoRunning = (u8)(!s_pomoRunning);
            }
            Display_RequestFull();
        }
        else if (evt->type == EVT_KEY4)     /* 新 KEY3 = 值加 / 切换 */
        {
            if (s_pomoSel == 0 && g_settings.pomodoro_work < 99)
            {
                g_settings.pomodoro_work++;
                if (!s_pomoRunning) { s_pomoRemain = (u16)g_settings.pomodoro_work * 60U; }
            }
            else if (s_pomoSel == 1 && g_settings.pomodoro_rest < 99)
            {
                g_settings.pomodoro_rest++;
            }
            else if (s_pomoSel == 2)
            {
                s_pomoRunning = (u8)(!s_pomoRunning);
            }
            Display_RequestFull();
        }
        break;
    }


    /*============================================================
     * 设置 —— 一个页面、两个状态【任务清单第 7 点】
     *
     * 用户要求：
     *   · 6 项全保留 + "震动强度" ⇒ 7 项
     *   · 按 KEY2 进下一级：**SPI 显示全部设置项，I2C 显示该项的功能内容**
     *   · 电位器调"音量"和"震动强度"，**每百分之一步进**
     *   · KEY4 返回时把百分比**固化**（存 EEPROM）
     *   · 在外面旋电位器**不影响**已设好的值（守卫在 EVT_POT 分支里）
     *   · 按 KEY2 确认时给 **100ms** 的反馈
     *============================================================*/
    case PAGE_SETTINGS:
    {
        /* 新 KEY4 = 返回：一次回任务清单，并把当前百分比存进固定值 */
        if (evt->type == EVT_KEY2)
        {
            s_setEdit = 0;
            Storage_SaveAll();
            Music_Beep(20);
            Menu_Back();
            break;
        }

        if (!s_setEdit)
        {
            /* ---- 列表态：选设置项 ---- */
            if (evt->type == EVT_KEY3)
            {
                s_setSel = (s_setSel == 0) ? (u8)(SET_ITEM_COUNT - 1) : (u8)(s_setSel - 1);
                Display_RequestMain();
                Display_RequestFull();
            }
            else if (evt->type == EVT_KEY4)
            {
                s_setSel++;
                if (s_setSel >= SET_ITEM_COUNT) { s_setSel = 0; }
                Display_RequestMain();
                Display_RequestFull();
            }
            else if (evt->type == EVT_KEY1)
            {
                s_setEdit = 1;
                Music_Beep(20);
                Display_RequestMain();
                Display_RequestFull();
            }
            break;
        }

        /* ---- 编辑态 ---- */
        if (evt->type == EVT_KEY1)          /* 新 KEY2 = 确认 + 100ms 反馈 */
        {
            /* 【第 7 点】用户原话是"确认后**马达或者蜂鸣器**给出持续 100ms 的反馈"。
             * 两项都给反馈，但各用最"对口"的那个执行器：
             *   · 震动强度 -> 马达：直接**感受到**强度大小，改完立刻知道合不合适
             *   · 其余项（含音量）-> 蜂鸣器：直接**听到**音量大小
             * 两者都是非阻塞的（到点在各自的 Tick 里收尾），不会卡住按键。
             *
             * 【2026-09-20 补】用户把蜂鸣器总开关关掉之后，蜂鸣器这条路静音了。
             * 用户要的反馈是"马达**或**蜂鸣器"，所以这里让马达顶上 ——
             * 否则在设置页按 KEY2 确认会**一点回应都没有**，
             * 用户会以为按键坏了。 */
            if (s_setSel == 6 || !Buzzer_IsEnabled())
            {
                Motor_Vibrate(100, g_settings.vibrate);
                printf("[SET] item=%d feedback by MOTOR 100ms (vibrate=%d)\r\n",
                       (int)s_setSel, (int)g_settings.vibrate);
            }
            else
            {
                Music_Beep(100);
                printf("[SET] item=%d confirmed by BUZZER\r\n", (int)s_setSel);
            }

            Storage_SaveAll();
        }
        else if (evt->type == EVT_KEY3)     /* 新 KEY1 = 值减 */
        {
            settings_step(0);
            Display_RequestMain();
            Display_RequestFull();
        }
        else if (evt->type == EVT_KEY4)     /* 新 KEY3 = 值加 */
        {
            settings_step(1);
            Display_RequestMain();
            Display_RequestFull();
        }
        break;
    }


    /*============================================================
     * 响铃页（上面的"响铃中"分支已经拦住了，这里只兜底）
     *============================================================*/
    case PAGE_RINGING:
        if (evt->type == EVT_KEY1)
        {
            Alarm_Stop();
        }
        else if (evt->type == EVT_KEY2)
        {
            Alarm_Snooze();
        }
        Menu_Home();
        break;

    /*============================================================
     * 掌机模式（M2 的页面，编号已经占好）
     *============================================================*/
    default:
        /* M1 的掌机页只做一件事：KEY2 返回。
         * 之前漏了这个分支 —— 进去之后按什么键都没反应，
         * 只能干等 30 秒让 Menu_CheckIdle() 把页面弹回主页。
         * 返回键必须在这里处理：App_Game.c 只管游戏逻辑，不管导航。 */
        if (evt->type == EVT_KEY2)
        {
            Menu_Back();
        }
        else
        {
            Game_OnEvent(evt);
        }
        break;
    }
}

/*========================================================================
 *                              每秒
 *========================================================================*/

void Menu_Tick1s(void)
{
    /* 【2026-09-19 搬走】原来这里调 Nixie_SetTime 写数码管内容。
     * 但本任务在"每秒节拍"里只是**登记** g_reqClockRefresh，
     * 真正读 PCF8563 是 TASK_RENDER 稍后才做的 —— 所以这里拿到的 g_clock
     * 还是上一秒的值，数码管就比副屏整慢一秒（真机现象）。
     *
     * 现在数码管改由 TASK_RENDER 在 Clock_Refresh() 之后立刻写
     * （见 App_Display.c 的 Display_Poll），三块屏用同一个刚读到的 g_clock。 */

    /* 时钟还没起来就自救：只登记请求，I2C 由 TASK_RENDER 执行 */
    if (!Clock_IsValid())
    {
        Clock_TryRecover();
    }

    /* 番茄钟倒计时：靠 g_sysTick 的 1 秒节拍推进 */
    if (Menu_Current() == PAGE_POMODORO && s_pomoRunning)
    {
        if (s_pomoRemain > 0)
        {
            s_pomoRemain--;
        }

        if (s_pomoRemain == 0)
        {
            /* 到点：提示音（不阻塞），并切到下一阶段 */
            if (!s_pomoRest)
            {
                s_pomoDone++;
                s_pomoRest   = 1;
                s_pomoRemain = (u16)g_settings.pomodoro_rest * 60U;
                Music_Play(SONG_BIRTHDAY, g_settings.volume);
                printf("[POMO] focus done #%d -> rest %d min\r\n",
                       (int)s_pomoDone, (int)g_settings.pomodoro_rest);
            }
            else
            {
                s_pomoRest   = 0;
                s_pomoRunning = 0;
                s_pomoRemain = (u16)g_settings.pomodoro_work * 60U;
                Music_Play(SONG_STAR, g_settings.volume);
                printf("[POMO] rest done -> back to focus\r\n");
            }
        }
    }

    /* 掌机模式的秒级逻辑（M2 会用，现在空转） */
    Game_Tick1s();
}

/*========================================================================
 *                    TASK_LOGIC：10ms 一拍
 *========================================================================*/

void task_logic(void) _task_ TASK_LOGIC
{
    u32 lastSecMs = 0;
    Event_t evt;

    /* 等外设和屏幕初始化完（TASK_RENDER 等 100ms，这里多等一点） */
    os_wait2(K_TMO, 30);        /* 150ms */

    /* 【方案 B】Clock_Init() 也搬去 TASK_RENDER —— 它内部要读 PCF8563（I2C），
     * 必须和屏幕写在同一个任务里，否则又变成两个任务碰 I2C。 */

    Alarm_Init();
    Uart_Init();
    Menu_Init();

    /* 走到这里说明五个任务全部起来了。
     * 把上电进度条收掉，只留第 1 颗灯常亮 = "系统在跑"。
     * （日出唤醒会在闹钟临近时接管这 8 颗灯） */
    BOOT_CRUMB(0);
    Led_SetSingle(0, 1);

    printf("[LOGIC] task ready, clock valid=%d\r\n", (int)Clock_IsValid());

    /* "每秒"用 g_sysTick 判断，不用 os_wait 计数 —— 这样即使任务被抢占，
     * 秒还是会准时到（《02》4.1：时间推进不依赖等待）。 */
    lastSecMs = SysTick_Get();

    while (1)
    {
        /* 1) 上位机协议：每 10ms 消费一次接收缓冲 */
        Uart_Poll();

        /* 2) 时钟芯片到点：只登记请求，I2C 清标志由 TASK_RENDER 执行 */
        if (g_rtcIrqFlag)
        {
            g_rtcIrqFlag = 0;
            g_reqRtcIrq  = 1;
        }

        /* 3) 每秒：读时间 -> 闹钟判断 */
        if (SysTick_Elapsed(lastSecMs) >= 1000U)
        {
            lastSecMs = SysTick_Get();

            /* 【方案 B】不再直接读时钟 —— 只登记请求，由 TASK_RENDER 执行 I2C。
             * 这样所有 I2C 访问都在同一个任务里串行，不需要总线锁。 */
            g_reqClockRefresh = 1;

            if (Alarm_Tick1s())
            {
                /* 起闹了：切到响铃页 */
                Menu_Push(PAGE_RINGING);
            }

            Menu_Tick1s();
        }

        /* 4) 输入事件 */
        while (Input_GetEvent(&evt))
        {
            Menu_OnEvent(&evt);
        }

        /* 5) 30 秒没操作自动回主界面 */
        Menu_CheckIdle();

        os_wait2(K_TMO, 2);     /* 2 x 5ms = 10ms */
    }
}
