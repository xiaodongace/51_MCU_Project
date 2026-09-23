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
#include "Servo.h"
#include "Motor.h"
#include "Buzzer.h"
#include "HC_SR04.h"      /* 云台：进页面初始化 PWM、离开时关输出 */
#include "LED.h"        /* 上电进度指示收尾 + 按键事件指示灯 */
#include "Keys.h"       /* Keys_IsPressed：掌机模式的"4 键同按强制退出"要用 */
/* 三个游戏（在 App\Game\ 子目录，该目录不在 IncludePath 里，所以写相对路径）*/
#include "Game/App_GmSnake.h"
#include "Game/App_GmBrick.h"
#include "Game/App_GmPlane.h"        /* 上电进度指示收尾 + 按键事件指示灯 */
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

static u8  s_gameState = 0;     /* 0 列表 / 1 游玩 / 2 结算 */
static u8  s_gameSel   = 0;     /* 列表态光标 0..2 */
static u8  s_gameIdx   = 0;     /* 正在玩哪个 0..2 */
/* 【2026-09-22】游戏节拍全部改成"时间戳"，不再数 TASK_LOGIC 的拍数。
 * 单位是 g_sysTick 的毫秒值（Timer3，1ms 一格），理由见 Game_Poll 上方那段。 */
static u32 s_gameFrameMs = 0;   /* 上一次推帧的时刻 */
static u32 s_gameOverMs  = 0;   /* 进入结算态的时刻 */
static u32 s_lastFireMs  = 0;   /* 上次发射子弹的时刻（飞机大战） */
static u8  s_everFired   = 0;   /* 本局发射过没有（0 = 进游戏即可首发） */

/* ---- 「日期和时间」任务的状态（第 3 点）---- */
static u8  s_dtActive = 0;      /* 1 = 正在这个任务里 */
static u8  s_dtState  = 0;      /* 0 = 选选项，1 = 输入中 */
static u8  s_dtChoice = 0;      /* 0 = 改年月日，1 = 改时分 */
static u16 s_dtY      = 0;      /* 已输入的年 */
static u16 s_dtMD     = 0;      /* 已输入的月日 */
static u8  s_dtH      = 0;      /* 已输入的时 */
static u8  s_dtM      = 0;      /* 已输入的分 */
static u8  s_dtCnt    = 0;      /* 已输入位数 */
static u8  s_dtErr    = 0;

/* ---- 「测距仪」任务的状态 ---- */
static u16 s_rangeCm     = 0;       /* 最近一次测到的距离（厘米），0 = 没测到 */
static u32 s_rangeNextMs = 0;       /* 下一次测量的时刻（节流到 150ms 一次） */      /* 上一次提交是否因非法被拒（给 I2C 显示提示）*/
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
 *      任务页的"进入 / 离开"钩子 —— 全工程只有这一对入口
 *
 * 这里做的是**外设的初始化与收尾**，不是"创建/销毁任务"。两者要分清：
 *
 *   · 本工程的任务是按**职责**分的（渲染/输入/逻辑/音效/采集，见 App_Public.h）。
 *     每个任务要服务全部 7 个页面 —— 比如 TASK_RENDER 画所有页、TASK_INPUT 收所有键。
 *     所以**只要系统在跑，这 5 个任务就必须一直在**，不能按页面创建/销毁。
 *
 *   · 参考工程 v3.1 是另一种分法：**一个外设一个任务**（TASK_LED / TASK_NTC / ...），
 *     切业务时 os_delete_task(旧的) + os_create_task(新的)，只有当前业务在跑。
 *     它的 App_Keys.c 里那两个 switch 就是干这个的。
 *     那种分法适合"自检程序"（一次只验一个外设），不适合本工程这种"多个页面并存"的产品。
 *
 *   · 但 v3.1 有一条原则我们**照做了**，而且必须一直保持：
 *        「进入 = 完整初始化，离开 = 彻底收尾，不依赖上一次留下的状态」
 *     —— 它的做法是让任务体第一件事就 GPIO_config + PWM_config 全配一遍；
 *        本工程没有"任务体重跑"这个时机，所以把同一件事挂在下面这对钩子上。
 *
 * 这一对动作由 Menu_Push / Menu_Back / Menu_Home 三处统一调用，
 * **不要散在各个页面的按键分支里**。云台就漏过一次：
 *   在云台页被闹钟打断 -> 响铃页 -> 停止后 Menu_Home 清栈，
 *   云台页被弹出去了，但它的外设没人关，舵机就一直挂在总线上。
 *
 * 【谁要在这里挂钩子】只有**独占型外设**才需要，即"打开就要占住引脚/定时器/PWM 通道"的：
 *   云台（PWMA 通道 3）、测距仪（Timer4 + P2.4/P3.6）。
 *   纯读传感器的页面（时钟、温湿度）不需要 —— 它们的外设在 sys_init 里配一次就够。
 *========================================================================*/
/* 进入某个任务页时要做的事 */
static void menu_task_enter(UIPageId_t pg)
{
    switch (pg)
    {
    case PAGE_SERVO:
        Servo_Init();
        break;

    case PAGE_RANGE:
        /* 【测距仪】进页面：初始化超声波引脚 + 清掉上次的值 + 立刻测一次 */
        SR04_Init();
        SR04_ResetFilter();     /* 【2026-09-22】清掉中值滤波历史，
                                 * 免得把上次退出前的残留值混进新的一段测量 */
        s_rangeCm     = 0;
        s_rangeNextMs = 0;
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

    case PAGE_RANGE:
        /* 【测距仪】离页面：把 TRIG 按低，不再发波（省电）。
         * 【2026-09-22】新版本不用定时器、不用中断，所以这里没有"停表"的动作了
         * —— 顺带把 Timer4 释放了出来（之前那一版拿它当计数器）。
         * ECHO 是输入脚，不用动。 */
        SR04_Stop();
        break;

    case PAGE_HOME:
        /* 【第 3 点】离开"日期和时间"任务 = 退出它的子模式 */
        if (s_dtActive)
        {
            s_dtActive = 0;
            s_dtState  = 0;
        }
        break;

    case PAGE_GAME_HALL:
        /* 【掌机模式】离开游戏页：把状态清干净。
         * 这里可能是"正常返回"，也可能是**被闹钟打断弹走的**
         * （Menu_Home() 会把整条栈逐层 leave），所以必须清，
         * 否则下次进来会直接停在半局的游戏画面上。 */
        s_gameState = 0;
        s_everFired = 0;
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
    Display_Refresh(1);

    /* 页面切换给一声提示音 —— 没有这一下，用户按了键只能靠"盯第 1 行字母有没有变"
     * 判断有没有生效（真机反馈原话："只切换最上面一行字母，没有听到蜂鸣器响"）。 */
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
    Display_Refresh(1);
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
    Display_Refresh(1);
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
    /* 【2026-09-22 清理】原来这里还有 5 个游戏子页 ID 的 case（都返回 6）。
     * 游戏改成页内两状态后这些页面不再出现，见 App_Public.h 的说明。 */
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
        Display_Refresh(1);
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

    case PAGE_GAME_HALL:
        /* 【掌机模式】从任务清单进来总是先看"选游戏"那一屏 */
        s_gameState = 0;
        s_gameSel   = 0;
        s_everFired = 0;
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
 * （K3/K4 在编辑页是"值-/值+"，同样属于"上/下"这一族）
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

/*========================================================================
 *                  「测距仪」任务的周期测距
 *
 *  由 TASK_LOGIC 的 10ms 循环调用；内部**节流到 150ms 测一次**。
 *  只在"当前页就是测距仪"时才测 —— 别的页面完全不碰超声波模块。
 *
 *  ※ 为什么不每 10ms 测一次：SR04_Measure() 是**阻塞**的
 *     （上课代码那套软件延时数循环的必然代价）。
 *     实测口径：最常见的 2~100cm 只要 0.2~7ms，占用 0.1~5%；
 *     模块没插或超量程时，两个超时兜住，最坏约 40ms（见 HC_SR04.h）。
 *     150ms 一次 -> 界面依然跟手；10ms 一次会把 TASK_LOGIC 占满，
 *     按键和时钟节拍都会被拖。
 *
 *  测完调 Display_Refresh(0)：**只重画本页、不清屏** —— 测距值在变，
 *  清屏会让屏幕每秒闪好几下（见 Display_Refresh 的参数约定）。
 *========================================================================*/
void Range_Poll(void)
{
    if (Menu_Current() != PAGE_RANGE)
    {
        return;
    }

    if (s_rangeNextMs != 0 && SysTick_Elapsed(s_rangeNextMs) < 150U)
    {
        return;
    }

    s_rangeNextMs = SysTick_Get();
    s_rangeCm     = SR04_Measure();

    Display_Refresh(0);
}

/* 【2026-09-22 清理】原来这里有个 Menu_RangeRaw()，把驱动侧的原始计数
 * （cnt）暴露给显示层，用于屏上的 "RAW nnnn" 与一次标定。
 * 用户要求去掉 RAW 显示后它没有使用者了，已删除；
 * 驱动侧的 SR04_LastRaw() 也一并删除（见 HC_SR04.h 的说明）。 */

u16 Menu_RangeCm(void)
{
    return s_rangeCm;
}

/*========================================================================
 *                        掌机模式（"7 游戏"任务）
 *
 *  用户 2026-09-22 的设定（原文）：
 *    "『7 游戏』任务点进去之后可以显示想要游玩的游戏，点击要游玩的游戏后，
 *     SPI 屏幕直接显示当前正在游玩的游戏，矩阵键盘的第三行第二个、
 *     第四行第一、二、三个，分别用来控制上左下右，I2C 屏幕此时直接关闭，
 *     独立按键全部一起按下才能强制退出游戏。"
 *    "游戏大厅不要了，把『选游戏+游玩』做成『7 游戏』页内的两个状态
 *     （和闹钟/番茄钟同款）。"
 *
 *  【后续变更 · 同一天提的】
 *    · 强制退出：从"4 个独立按键一起按"改成 **矩阵键盘第一行第四个**
 *      （见 GAME_KEY_EXIT）。更好按、也不会误触。
 *      原来那套"同时按下"的检测代码已经删掉了。
 *    · 飞机大战新增 **矩阵键盘第四排第二个 = 发射**（见 GAME_KEY_FIRE），
 *      每 3 秒一轮、一轮 10 枚。这个键原本是"下"，所以在飞机这一局里
 *      没有"下"这个动作（飞机初始 y=50，本来也只能下移 5 像素）。
 *      在此之前飞机是"外壳每 90ms 自动替它发一枚"，那段逻辑已删。
 *    · 贪吃蛇节拍从 100ms/步 放慢到 250ms/步（真机反馈"速度极快"）。
 *    · 结算画面停留从 1.5 秒延长到 3 秒（真机反馈"一闪就没"）。
 *
 *  => **不做单独的"大厅"页面**：PAGE_GAME_HALL 就是"7 游戏"任务页本身，
 *     页内三个状态（不额外压栈，和闹钟/番茄钟一致）：
 *        s_gameState = 0  列表态：SPI 列 3 个游戏，K1/K3 选、K2 开始、K4 返回
 *        s_gameState = 1  游玩态：SPI 由游戏自己画，独立按键全部屏蔽
 *        s_gameState = 2  结算态：显示 GameOver 3 秒后自动回列表
 *
 *  三个游戏都来自参考工程《23_基于stc8的多功能时钟》的 User/ 目录
 *  （game.c / snake.c / planegame.c），移植时只做了机械适配：
 *    · 显示层从副屏(oled.h)换到主屏(SPI_OLED_*)；
 *    · Delay_ms -> delay_ms；
 *    · snake.c 的 malloc/free -> 静态节点池；
 *    · game.c 的 Game_* 改名 Brick_*（避免与 App_Game.c 撞名）。
 *  游戏逻辑本身一行没动。
 *========================================================================*/

/*------------------------------------------------------------------------
 *  矩阵键盘方向键
 *
 *  按键编号 = row * 4 + col（见 Driver/MatrixKey.h；4x4 键盘，row/col 都是 0..3）。
 *  用户指定的是**键盘上的位置**：
 *     第三行第二个  = row2, col1  =>  2*4+1 = 9    （上）
 *     第四行第一个  = row3, col0  =>  3*4+0 = 12   （左）
 *     第四行第二个  = row3, col1  =>  3*4+1 = 13   （下）
 *     第四行第三个  = row3, col2  =>  3*4+2 = 14   （右）
 *------------------------------------------------------------------------*/
#define GAME_KEY_UP     9U
#define GAME_KEY_LEFT   12U
#define GAME_KEY_DOWN   13U
#define GAME_KEY_RIGHT  14U

/* 【2026-09-22 用户要求】强制退出键：**矩阵键盘第一行第四个**。
 *     第一行第四个 = row0, col3  =>  0*4+3 = 3
 * 原来是"4 个独立按键同时按下"，用户改成这一个键（更好按、也不会误触）。
 * 游玩态下独立按键仍然一律不响应（除了这一下退出）。 */
#define GAME_KEY_EXIT   3U

/* 【2026-09-22 用户要求】飞机大战的**发射键 = 矩阵键盘第四排第二个**，
 * 也就是 param 13 —— 和"下"是同一个键。
 *
 * 处理方式：game_input() 里**先判发射、再判方向**，
 * 所以在飞机大战里这个键就是"发射"，飞机不再有"下"这个动作。
 * 影响很小：飞机初始 y=50，屏幕 64 高、机身 8 高，本来也只能下移 5 像素。
 * ※ 贪吃蛇里这个键仍然是"下"——只在飞机那一支里被改成发射。 */
#define GAME_KEY_FIRE   13U

/* 游戏个数 GAME_COUNT 定义在 App_Menu.h（显示层也要用）*/

/*------------------------------------------------------------------------
 *  【2026-09-22 三次修正 · 节拍改成"按毫秒"而不是"数拍数"】
 *
 *  原来三个节拍宏都是"数 TASK_LOGIC 被调用的次数"：
 *      GAME_FRAME_DIV  3    -> 3 拍 x 10ms = 30ms/帧
 *      GAME_STEP_SNAKE 10   -> 10 拍 x 10ms = 100ms/步
 *      GAME_OVER_DIV   150  -> 150 拍 x 10ms = 1.5 秒
 *  这套写法**依赖一个隐含假设**："TASK_LOGIC 一定每 10ms 转一圈"。
 *  而那个 10ms 又来自 os_wait2(K_TMO,2) 乘以 RTX51 的 tick 时长 ——
 *  一旦 tick 不是预期的 5ms，三个数字就全部失准。
 *  （真机现象："贪吃蛇快得没法玩"+"结算画面一闪就没"，就是它。）
 *
 *  现在改成**读 1ms 系统节拍（g_sysTick）算毫秒差**：
 *    · 节拍只由 Timer3 的 1ms 中断决定，与任务调度无关；
 *    · 任务偶尔被拖长，下一帧只是"晚一点"，不累积漂移；
 *    · 代码里写 250 就是 250ms，读的人不用再换算。
 *  这也是本工程一贯的做法（见 App_Music.c："时间推进不依赖等待"）。
 *------------------------------------------------------------------------*/

/* 一帧多少毫秒。打砖块/飞机是"每帧移动 1~4 像素"，30ms 合适（约 33 帧/秒）。
 * 刷屏（软件 SPI 写 1024 字节）约 8.5ms，30ms 留了一倍余量。 */
#define GAME_FRAME_MS       30U

/* 贪吃蛇单独一个节拍。
 *  蛇每**步**走 8 个像素（见 App_GmSnake.c 的 newX += 8），
 *  屏幕宽 128、出生点在 x=16，到右墙只有 13 步 ——
 *  所以这个值直接就是"玩家反应窗口 / 13"：
 *      100ms -> 1.3 秒撞墙，来不及按（真机："速度极快"）
 *      250ms -> 3.3 秒，正常可玩（诺基亚那代贪吃蛇大约就是这个量级）
 *  想再快/再慢只改这一个数。 */
#define GAME_STEP_MS_SNAKE  250U

/* 飞机大战的发射冷却：**每 3 秒才能发射一次**（用户要求）。 */
#define GAME_FIRE_CD_MS     3000U

/* 结算画面（GAME OVER + SCORE）停留多久再自动回列表。
 *  原来 1.5 秒 —— 真机反馈"极快就没了"，玩家还没看清分数就回了。改 3 秒。 */
#define GAME_OVER_HOLD_MS   3000U

static u16 game_step_ms(void)
{
    return (s_gameIdx == 0U) ? GAME_STEP_MS_SNAKE : (u16)GAME_FRAME_MS;
}

/* 【掌机模式的状态变量声明在文件顶部（和 s_alarmEdit 放一起）——
 * 因为 menu_enter() / menu_task_leave() 在文件很靠前的位置就要用它们，
 * 声明放在这里会报 error C202: undefined identifier。 */

/* 启动选中的游戏。三个游戏的 Init 都会自己把状态置成"游玩中"并清屏。 */
static void game_start(u8 idx)
{
    switch (idx)
    {
    case 0:  Snake_Init();      break;
    case 1:  Brick_Init();      break;
    default: PlaneGame_Init();  break;
    }

    /* 节拍从"现在"开始算：进游戏先看到静止的初始画面，过一个节拍才动
     * （否则第一帧立刻推，看着像闪一下）。
     * s_everFired 清 0 = 飞机大战进游戏就能发射第一轮，不用等冷却。 */
    s_gameFrameMs = SysTick_Get();
    s_gameOverMs  = 0;
    s_everFired   = 0;
}

/* 推一帧：Update + Draw（Draw 内部会自己调 SPI_OLED_Refresh） */
static void game_frame(void)
{
    switch (s_gameIdx)
    {
    case 0:
        Snake_Update();
        Snake_Draw();
        break;

    case 1:
        Brick_Update();
        Brick_Draw();
        break;

    default:
        /* 【2026-09-22 用户要求】飞机大战不再自动发射 ——
         * 改成玩家按"矩阵键盘第四排第二个键"手动发射，每 3 秒一轮、一轮 10 枚。
         * 原来这里由外壳定时替它发（每 3 帧 = 90ms 一发），那是
         * "用户当时没指定射击键"的临时办法，现在不需要了。 */
        PlaneGame_Update();
        PlaneGame_Draw();
        break;
    }

    s_gameFrameMs = SysTick_Get();      /* 记下这一帧的时刻 */
}

/* 强制退出：回"选游戏"列表。清掉局内状态，否则下次进来会停在半局画面上。 */
static void game_exit(void)
{
    s_gameState = 0;
    s_everFired = 0;

    /* 【2026-09-22】用 0（不清屏）而不是 1：
     * 下面 main_draw_game_list() 画的每一行都会补空格到满 128 像素
     * （见 main_draw_line_fit），四行正好覆盖整屏，**不需要先清屏**。
     * 而"不清屏"还顺带绕开了 Display_Poll 里那道**只对清屏请求生效的
     * 100ms 限流** —— 列表会立刻出现，不再有"顿一下"的感觉。 */
    Display_Refresh(0);
}

/* 游戏是否已结束（三个游戏各有一个 state 变量） */
static u8 game_is_over(void)
{
    switch (s_gameIdx)
    {
    case 0:  return (u8)(snakeGameState == SNAKE_STATE_GAME_OVER);
    case 1:  return (u8)(brickState     == GAME_OVER);
    default: return (u8)(planeGameState == PLANE_STATE_GAME_OVER);
    }
}

/* 画结算画面（三个游戏各有自己的 ShowGameOver，内部会刷新屏幕） */
static void game_show_over(void)
{
    switch (s_gameIdx)
    {
    case 0:  Snake_ShowGameOver();      break;
    case 1:  Brick_ShowGameOver();      break;
    default: PlaneGame_ShowGameOver();  break;
    }
}

/*------------------------------------------------------------------------
 *  飞机大战：发射（含 3 秒冷却）
 *
 *  用户要求："第四排第二个按键当作发射子弹，每 3 秒只能发射一次子弹，
 *            一次子弹为 10 枚"。
 *
 *  冷却判据用 s_everFired + s_lastFireMs 两个变量，而不是"当前时刻 - 3000"：
 *    · 进游戏时 s_everFired = 0 -> 第一次按立刻能发
 *      （否则开机早期 SysTick 数值还很小，用减法初始化会绕圈、不好读）；
 *    · 发过之后 s_everFired = 1，之后每次都比"距上次够不够 3 秒"。
 *
 *  ※ 冷却放在外壳而不是游戏里：游戏只管"怎么发射"，
 *    "什么时候允许发射"是外壳的规则（换游戏/换冷却都不用动游戏源码）。
 *------------------------------------------------------------------------*/
static void game_fire(void)
{
    if (s_gameState != 1U)              /* 只有游玩态能发射 */
    {
        return;
    }
    if (s_gameIdx != 2U)                /* 只有"飞机大战"有这个动作 */
    {
        return;
    }
    if (s_everFired && SysTick_Elapsed(s_lastFireMs) < GAME_FIRE_CD_MS)
    {
        return;                         /* 还在冷却中 */
    }

    s_everFired  = 1;
    s_lastFireMs = SysTick_Get();
    PlaneGame_Shoot();                  /* 内部一次发 10 枚（见 App_GmPlane.c） */
}

/* 方向键 -> 游戏输入。
 * 注意打砖块：Brick_MovePaddle() 的参数是**方向增量**（x += dir*3），
 * 而且挡板只左右移动，所以只映射左/右，上/下不理会。 */
static void game_input(u8 p)
{
    switch (s_gameIdx)
    {
    case 0:     /* 贪吃蛇：四方向（内部会挡 180 度掉头） */
        if      (p == GAME_KEY_UP)    { Snake_HandleInput(SNAKE_DIR_UP);    }
        else if (p == GAME_KEY_DOWN)  { Snake_HandleInput(SNAKE_DIR_DOWN);  }
        else if (p == GAME_KEY_LEFT)  { Snake_HandleInput(SNAKE_DIR_LEFT);  }
        else if (p == GAME_KEY_RIGHT) { Snake_HandleInput(SNAKE_DIR_RIGHT); }
        break;

    case 1:     /* 打砖块：只左右 */
        if      (p == GAME_KEY_LEFT)  { Brick_MovePaddle(-1); }
        else if (p == GAME_KEY_RIGHT) { Brick_MovePaddle(1);  }
        break;

    default:    /* 飞机大战：上/左/右 + 发射 */
        /* 【2026-09-22 用户要求】第四排第二个键（param 13）在飞机大战里改成**发射**。
         * 所以飞机这一局没有"下"这个动作 —— 影响很小：飞机初始 y=50，
         * 屏幕高 64、机身 8 高，本来也只能下移 5 个像素。
         *
         * ※ 必须先判发射：13 同时也是 GAME_KEY_DOWN，判反了就变成"往下飞"。 */
        if      (p == GAME_KEY_FIRE)  { game_fire(); }
        else if (p == GAME_KEY_UP)    { PlaneGame_HandleKey(PLANE_KEY_UP);    }
        else if (p == GAME_KEY_LEFT)  { PlaneGame_HandleKey(PLANE_KEY_LEFT);  }
        else if (p == GAME_KEY_RIGHT) { PlaneGame_HandleKey(PLANE_KEY_RIGHT); }
        break;
    }
}

u8 Menu_GameIsPlaying(void)
{
    return (u8)(s_gameState != 0);
}

u8 Menu_GameSel(void)
{
    return s_gameSel;
}

/*------------------------------------------------------------------------
 *  掌机模式的周期动作 —— 由 TASK_LOGIC 调用（大约每 10ms 一次）
 *
 *  【重要】本函数**不假设自己被调用的频率**：所有节拍都用 g_sysTick 的
 *  毫秒差判定（见下面每个分支），所以哪怕 TASK_LOGIC 偶尔被拖到 15ms、
 *  20ms 才转一圈，帧率也不会跟着漂 —— 这是 2026-09-22 把"数拍数"
 *  换成"算毫秒"的原因，具体背景见本文件节拍宏上方那段。
 *
 *  两件事：
 *    1) 按节拍推一帧游戏
 *    2) 结束检测 + 结算倒计时
 *  （强制退出键在 Menu_OnEvent 的游戏分支里处理，见 GAME_KEY_EXIT。
 *    独立按键在游玩态一律不响应 —— 那是用户要求的。）
 *------------------------------------------------------------------------*/
void Game_Poll(void)
{
    if (Menu_Current() != PAGE_GAME_HALL)
    {
        return;                     /* 不在游戏页 */
    }
    if (s_gameState == 0U)
    {
        return;                     /* 列表态：等按键，不需要周期动作 */
    }

    /* ---- 1) 结算态：停够 GAME_OVER_HOLD_MS 再回列表 ---- */
    if (s_gameState == 2U)
    {
        if (SysTick_Elapsed(s_gameOverMs) >= GAME_OVER_HOLD_MS)
        {
            s_gameState = 0;

            /* 用 Display_Refresh(0)（不清屏）：列表四行都补满整屏宽，
             * 不需要清屏；不清屏就不走 Display_Poll 里那道 100ms 限流，
             * 列表立刻出现 —— 顺带解决了"结算后顿一下才回列表"。 */
            Display_Refresh(0);
        }
        return;
    }

    /* ---- 2) 游玩态：先看有没有结束 ---- */
    if (game_is_over())
    {
        game_show_over();
        s_gameState  = 2U;          /* 进结算态 */
        s_gameOverMs = SysTick_Get();
        return;
    }

    /* ---- 3) 按节拍推一帧（贪吃蛇慢、打砖块/飞机快，见 game_step_ms）---- */
    if (s_gameFrameMs != 0U && SysTick_Elapsed(s_gameFrameMs) < game_step_ms())
    {
        return;                     /* 还没到点 */
    }
    game_frame();
}
void Menu_CheckIdle(void)
{
    /* 【2026-09-22 用户要求】只有"第二级菜单"（任务清单）才触发空闲返回。
     *
     * 用户原话："只有在第二级菜单时，才会触发长时间不操作返回主界面这个功能。"
     *
     * 本工程的层级（见 Menu_OnEvent 里那段模型说明）：
     *   第 0 级 大字时钟        : Menu_Current()==PAGE_HOME && s_menuOpen==0
     *   第 1 级 任务清单         : Menu_Current()==PAGE_HOME && s_menuOpen==1
     *                            （这就是用户口语里的"第二级菜单"）
     *   第 2 级 进了某个任务页   : Menu_Current() != PAGE_HOME
     *
     * 原来的写法是"除少数例外、其它页面都参与"，
     * 于是进了闹钟/设置/测距仪/云台/游戏页，30 秒不动就被弹回主界面。
     * 现在反过来：**只认任务清单这一层**；进了任务就不再自动返回
     * （要回主界面自己按 KEY4）。
     *
     * ※ 为什么不能只用 Menu_IsOpen() 判断：
     *   进入任务页之后 s_menuOpen **已经是 0** —— menu_enter() 第一句就清它，
     *   所以单看这个标志会把"任务页"错当成"已经不在第二级"而放行。
     *   两个条件必须一起看。
     * ※ "日期和时间"任务不压栈（走 s_dtActive 子模式），进来时 s_menuOpen
     *   同样被清 0，所以它也被自动排除，不必单独判 s_dtActive。
     * ※ 响铃页/闹钟编辑页/番茄钟也一并被排除了 —— 它们的 Menu_Current()
     *   都不是 PAGE_HOME，本来就到不了下面那句 Menu_Home()。
     */
    if (Menu_Current() != PAGE_HOME)
    {
        return;                     /* 已经在某个任务里 -> 不打扰 */
    }

    if (!Menu_IsOpen())
    {
        return;                     /* 还停在大字时钟 -> 无处可返 */
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
        }
    }

    s_dtErr = (u8)(ok ? 0 : 1);

    if (ok)
    {
        s_dtState = 0;              /* 成功 -> 回选项选择 */
    }
    else
    {
        /* 非法：长音提示，**留在输入态**让用户接着删掉重输。
         * 此时 s_dtCnt 仍是满位，dt_input 会拒绝继续输入（见那里的说明）。 */
    }

    Display_Refresh(1);
}

/* 矩阵键盘输入一位 */
static void dt_input(u8 digit)
{
    u8 maxCnt = (u8)((s_dtChoice == 0) ? 8 : 4);

    /* 【2026-09-21 修用户报的"输入错误的时间这个数字就能一直输入、
     *   DATE 地方的值也不断增加、I2C 最下面一行已经显示 30/8"】
     *
     * 根因：原来只在"刚好数到满位"那一刻才去提交一次校验；
     * 校验没过就留在输入态，而 s_dtCnt 已经等于满位了 —— 再按数字
     * 就继续累加（9、10、…30），s_dtMD 也跟着 *10 一路涨。
     *
     * 现在**满位就锁住**：拒绝继续输入并长鸣提示，用户只能按 K4 删一位再改。 */
    if (s_dtCnt >= maxCnt)
    {
        return;
    }

    s_dtErr = 0;                    /* 重新开始输就把"非法"提示清掉 */

    if (s_dtChoice == 0)            /* 8 位：前 4 位年，后 4 位月日 */
    {
        if (s_dtCnt < 4) { s_dtY  = (u16)(s_dtY  * 10U + digit); }
        else             { s_dtMD = (u16)(s_dtMD * 10U + digit); }
        s_dtCnt++;
        if (s_dtCnt >= 8) { dt_commit(); return; }
    }
    else                            /* 4 位：前 2 位时，后 2 位分 */
    {
        if (s_dtCnt < 2) { s_dtH = (u8)(s_dtH * 10U + digit); }
        else             { s_dtM = (u8)(s_dtM * 10U + digit); }
        s_dtCnt++;
        if (s_dtCnt >= 4) { dt_commit(); return; }
    }

    Display_Refresh(0);
}

/* KEY4：删掉最后一位。删到 0 位就取消修改、退回上一级（选项选择态）。
 * 位数用 s_dtCnt 单独计数，所以"全是 0"也要按对应次数才删得完。 */
static void dt_delete(void)
{
    if (s_dtCnt == 0)
    {
        s_dtState = 0;              /* 已经删完 -> 退回选项选择 */
        Display_Refresh(1);
        return;
    }

    s_dtCnt--;
    s_dtErr = 0;                    /* 删了一位就不算"非法"了 */

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

    Display_Refresh(0);
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
        Display_Refresh(0);
    }
    else if (evt->type == EVT_KEY2)
    {
        /* 确认进入输入：清零各值和位数 */
        s_dtState = 1;
        s_dtY = 0; s_dtMD = 0; s_dtH = 0; s_dtM = 0; s_dtCnt = 0;
        Display_Refresh(1);
    }
    else if (evt->type == EVT_KEY4)
    {
        /* 返回上一级 = 回任务清单 */
        s_dtActive = 0;
        s_menuOpen = 1;
        s_menuCursor = Menu_IndexOf(PAGE_HOME);
        Display_Refresh(1);
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
u8  Menu_DateErr(void)      { return s_dtErr; }

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

        /* 【2026-09-21 用户要求】按键反馈全部取消，**只有这四项保留**：
         *   音量 / 震动 / 曲目 / 提醒
         * 因为这四项的反馈是"有信息量"的 —— 改音量要听到音量、
         * 改震动要感觉到强度、改曲目要听到曲子、改提醒要知道是哪一种。 */
        Music_Beep(100);
        break;

    case 1:     /* 曲目：换一首并试听 */
        if (up) { g_settings.song = (u8)((g_settings.song + 1) % SONG_COUNT); }
        else    { g_settings.song = (u8)((g_settings.song + SONG_COUNT - 1) % SONG_COUNT); }
        Music_Play((u8)(g_settings.song + 1), g_settings.volume);
        break;

    case 2:     /* 提醒方式（Buzzer = 放曲子 / Motor = 只震动）*/
        g_settings.alert_mode = (g_settings.alert_mode == ALERT_RING) ? ALERT_VIBRATE : ALERT_RING;

        /* 用"当前选中的这一种"给反馈，用户才知道现在到底选的是哪种 */
        if (g_settings.alert_mode == ALERT_RING)
        {
            Music_Beep(150);
        }
        else
        {
            Motor_Vibrate(150, g_settings.vibrate);
        }
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

        /* 【2026-09-21 用户要求】"改震动的时候，自动震动"
         * 边调边震一下，直接感受到当前强度，不用退出去再试。
         * Motor_Vibrate 是非阻塞的（到点在 Motor_Tick 里收尾）；
         * 强度为 0 时会兜底到 10%，否则用户按了完全没有感觉。 */
        Motor_Vibrate(80, g_settings.vibrate);
        break;
    }

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
        Display_Refresh(0);
        break;

    case EVT_KEY4:                  /* 新 KEY3 = 下移 */
        s_listSel++;
        if (s_listSel >= ALARM_MAX) { s_listSel = 0; }
        Display_Refresh(0);
        break;

    /* 【2026-09-21 用户要求】「KEY2 键位用来确定开启或者关闭，
     *   最开始都关闭，KEY2 按下开启当前选择闹钟，KEY2 再次按下关闭当前闹钟」
     * -> 短按 KEY2 = 开/关当前这组闹钟。 */
    case EVT_KEY1:                  /* 新 KEY2 短按 = 开/关当前闹钟 */
        Alarm_Toggle(s_listSel);
        Display_Refresh(0);
        break;

    /* 编辑那一层改由**长按 KEY2** 进入（用户 2026-09-21 选定）：
     * 短按已经被"开/关"占用了，长按是唯一不额外占键的入口。 */
    case EVT_KEY1_LONG:             /* 新 KEY2 长按 = 进入编辑（不压栈）*/
        s_editIdx   = s_listSel;
        s_editBuf   = g_alarms[s_listSel];
        s_editField = 0;
        s_alarmEdit = 1;
        Display_Refresh(0);
        break;

    case EVT_MATRIX:                /* 矩阵键盘直接跳到对应组 */
        if (evt->param < ALARM_MAX)
        {
            s_listSel = evt->param;
            Display_Refresh(0);
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
        Display_Refresh(0);
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
        Display_Refresh(0);
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
        Display_Refresh(0);
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
            Display_Refresh(0);
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
    /* 【2026-09-22】原来这里还有一条串口回声（打印事件类型/参数/当前页）。
     * 工程收尾时全工程的调试 printf 已统一删除，这里只保留上面那颗灯。 */
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
            Display_Refresh(1);
        }
        else if (s_setSel == 6)
        {
            g_settings.vibrate = evt->param;
            Display_Refresh(1);
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
                Display_Refresh(1);      /* 副屏：从"时钟内容"换成"任务图标"，要清屏 */
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
            Display_Refresh(0);
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
            Display_Refresh(0);
            break;

        case EVT_KEY2:                      /* 进入该任务 */
            menu_enter(s_menuCursor);
            break;

        case EVT_KEY4:                      /* 回第 0 级：大字时钟 */
            s_menuOpen = 0;
            Display_Refresh(1);          /* 副屏：从"任务图标"换回"时钟内容"，要清屏 */
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
            Display_Refresh(0);
            break;

        case EVT_KEY4:                  /* 新 KEY3 = 下移 = 角度增大 */
            Servo_SetAngle((a <= (u8)(180 - SERVO_KEY_STEP)) ? (u8)(a + SERVO_KEY_STEP) : 180);
            Display_Refresh(0);
            break;

        case EVT_KEY1:                  /* 新 KEY2 = 确认 = 回中位 90° */
            Servo_SetAngle(90);
            Display_Refresh(0);
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
    /*============================================================
     * 测距仪（超声波）
     *
     * 测距本身是**周期自动做**的（Range_Poll 每 150ms 一次），
     * 所以这个页面不需要任何按键来触发测量 —— 只有"返回"。
     * 版面：副屏显示数值 + 进度条；主屏显示大字距离。
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
     * 掌机模式（"7 游戏"）—— 一个页面、两个状态
     *
     * 游玩态的规则（按用户要求）：
     *   · 只认**矩阵键盘**：方向键 + 发射键（只有飞机有）+ 退出键；
     *     **独立按键全部屏蔽**（含 KEY4 —— 要退出只能按退出键）
     *   · 退出键 = 矩阵键盘第一行第四个，见 GAME_KEY_EXIT，
     *     在下面的 case 里比对 evt->param（不是在这里判）
     *   · I2C 副屏关掉（由 sub_redraw() 的 hasContent 判定）
     *============================================================*/
    case PAGE_GAME_HALL:
    {
        /* ---- 游玩/结算态：独立按键一律不响应；
         *      矩阵键盘里只认方向键 + 强制退出键 ---- */
        if (s_gameState != 0U)
        {
            if (evt->type == EVT_MATRIX)
            {
                if (evt->param == GAME_KEY_EXIT)
                {
                    game_exit();        /* 第一行第四个 = 强制退出 */
                }
                else
                {
                    game_input(evt->param);
                }
            }
            break;
        }

        /* ---- 列表态 ---- */
        switch (evt->type)
        {
        case EVT_KEY3:                  /* 新 K1 = 上移 */
            if (s_gameSel == 0U)
            {
                s_gameSel = (u8)(GAME_COUNT - 1U);
            }
            else
            {
                s_gameSel--;
            }
            Display_Refresh(0);
            break;

        case EVT_KEY4:                  /* 新 K3 = 下移 */
            s_gameSel++;
            if (s_gameSel >= GAME_COUNT)
            {
                s_gameSel = 0;
            }
            Display_Refresh(0);
            break;

        case EVT_KEY1:                  /* 新 K2 = 开始玩这个游戏 */
            s_gameIdx   = s_gameSel;
            s_gameState = 1U;
            /* 节拍时间戳、发射状态由 game_start() 里统一初始化，这里不再逐个数。 */
            game_start(s_gameIdx);   /* 内含初始化 + 清显存，游戏自己画第一帧 */

            /* 【2026-09-22】参数用 0 而不是 1：
             * 这一下之后 s_gameState 已经是 1（游玩态），而 main_redraw 在
             * 游玩态是"一个字都不画、直接 return"——所以传 1 并不会真的清屏，
             * 唯一的效果是**白白走一道 100ms 限流**，副屏反而关得慢。
             * 传 0 则不设清屏标志，副屏当拍就关（用户要求"进游戏 I2C 直接关闭"）。 */
            Display_Refresh(0);
            break;

        case EVT_KEY2:                  /* 新 K4 = 回任务清单 */
            Menu_Back();
            break;

        default:
            break;
        }
        break;
    }

    /*============================================================
     * 闹钟 —— 一个页面、两个状态【任务清单第 4 点】
     *
     * 原来"列表"和"编辑"是两个页面并且压栈，层级是
     *   任务清单 → 闹钟列表 → 闹钟编辑
     * 从编辑回任务清单要按两次 KEY4（用户明确抱怨过）。
     * 现在它们只是同一页的 s_alarmEdit 两个状态，**不压栈**：
     *   任务清单 → 闹钟页（列表态 <-> 编辑态）
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
                Display_Refresh(0);
            }
            else if (evt->type == EVT_KEY4)
            {
                s_pomoSel++;
                if (s_pomoSel > 2) { s_pomoSel = 0; }
                Display_Refresh(0);
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
                Display_Refresh(0);
            }
            break;
        }

        /* ---- 编辑态 ---- */
        if (evt->type == EVT_KEY1)          /* 新 KEY2 = 确认回列表 */
        {
            s_pomoEdit = 0;
            Display_Refresh(0);
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
            Display_Refresh(0);
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
            Display_Refresh(0);
        }
        break;
    }


    /*============================================================
     * 设置 —— 一个页面、两个状态【任务清单第 7 点】
     *
     * 用户要求：
     *   · 6 项全保留 + "震动强度" -> 7 项
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
            Menu_Back();
            break;
        }

        if (!s_setEdit)
        {
            /* ---- 列表态：选设置项 ---- */
            if (evt->type == EVT_KEY3)
            {
                s_setSel = (s_setSel == 0) ? (u8)(SET_ITEM_COUNT - 1) : (u8)(s_setSel - 1);
                Display_Refresh(0);
            }
            else if (evt->type == EVT_KEY4)
            {
                s_setSel++;
                if (s_setSel >= SET_ITEM_COUNT) { s_setSel = 0; }
                Display_Refresh(0);
            }
            else if (evt->type == EVT_KEY1)
            {
                s_setEdit = 1;
                Display_Refresh(0);
            }
            break;
        }

        /* ---- 编辑态 ----
         * 【2026-09-21 用户要求】"所有菜单去掉最后一行 K2 OK，
         *   并把这个 KEY2 OK 功能删除掉"
         * -> 编辑态里 KEY2 的那个"确认 + 100ms 反馈"整块删掉了。
         *   现在编辑态只管用 K1/K3（音量和震动强度还能用旋钮）调值，
         *   按 KEY4 返回时自动存盘（EVT_KEY2 分支里已经做了 Storage_SaveAll）。 */
        if (evt->type == EVT_KEY3)          /* 新 KEY1 = 值减 */
        {
            settings_step(0);
            Display_Refresh(0);
        }
        else if (evt->type == EVT_KEY4)     /* 新 KEY3 = 值加 */
        {
            settings_step(1);
            Display_Refresh(0);
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
        /* 【2026-09-22 清理】原来这里还有一句 Game_OnEvent(evt)（M1 占位）。
         * 现在"7 游戏"已经有了自己的 case PAGE_GAME_HALL 分支，
         * 走不到这里；而 App_Game.c 的占位函数已整体废弃（省 Flash）。
         * 保留 KEY2 返回的行为，其余按键在这个分支里不响应。 */
        if (evt->type == EVT_KEY2)
        {
            Menu_Back();
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

        /* 【2026-09-21 修用户报的"FOCUS 时间没有刷新走动"】
         * 原来这里只把 s_pomoRemain 减了，**却没有任何人请求重画** ——
         * 副屏上的 "FOCUS mm:ss" 自然就一直停在进页面时的那个值。
         * 现在每秒请求一次"只重画行"（不清屏，不会闪）。 */
        Display_Refresh(0);

        if (s_pomoRemain == 0)
        {
            /* 到点：提示音（不阻塞），并切到下一阶段 */
            if (!s_pomoRest)
            {
                s_pomoDone++;
                s_pomoRest   = 1;
                s_pomoRemain = (u16)g_settings.pomodoro_rest * 60U;
                Music_Play(SONG_MALAN, g_settings.volume);
            }
            else
            {
                s_pomoRest   = 0;
                s_pomoRunning = 0;
                s_pomoRemain = (u16)g_settings.pomodoro_work * 60U;
                Music_Play(SONG_STAR, g_settings.volume);
            }
        }
    }

    /* 【2026-09-22 清理】原来这里调 Game_Tick1s()（M1 占位、空实现）。
     * 掌机模式的周期逻辑已经全部收进 Game_Poll()（TASK_LOGIC 里 10ms 一次），
     * 不需要秒级钩子了。 */
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

        /* 5) 测距仪：在测距仪页每 150ms 测一次（内部自己判断页面与节流） */
        Range_Poll();

        /* 【掌机模式】10ms 一拍：4 键同按退出 + 按节拍推帧 */
        Game_Poll();

        /* 6) 30 秒没操作自动回主界面 */
        Menu_CheckIdle();

        os_wait2(K_TMO, 2);     /* 2 x 5ms = 10ms */
    }
}
