/*
 * App_Display.c - 两块屏幕的绘制（TASK_RENDER，5ms 一拍）
 *
 * 只读别人的数据（g_clock / g_settings / g_alarms / 传感器全局量），不改任何人。
 * 副屏走 I2C，访问前先申请总线锁（《02》3.3 的"走廊通行权"）。
 */
#include "App_Display.h"

#include "spi_oled.h"       /* 主屏：SPI，带硬件字库，可显示汉字 */
#include "I2C_OLED.h"       /* 副屏：demo30 原生驱动，自包含软件 I2C */
#include "I2C.h"            /* I2C 底层（副屏批量写试验已回退，保留声明备用） */
#include "App_Clock.h"
#include "App_Menu.h"   /* Menu_IsOpen/菜单表：主屏任务清单的唯一来源 */
#include "App_Input.h"   /* Input_GetPotLevel：副屏显示实时旋钮档位 */
#include "App_Alarm.h"
#include "App_Sensor.h"
#include "App_Game.h"
#include "App_Font.h"
#include "I2C_Lock.h"
#include "LED.h"
#include "NixieScan.h"
#include "Servo.h"
#include "App_Icons.h"   /* Icon_Get：任务清单的图标（64x48） */       /* Servo_GetAngle：云台页显示当前角度 */   /* Nixie_SetTime：数码管内容，读时钟后立刻同步 */

/* 副屏驱动改用 demo30 的原生实现（Driver/I2C_OLED/I2C_OLED.c）。
 * demo30 的这套驱动是自包含的：自带 I2C_Start/Send_Byte/I2C_WaitAck，
 * 函数名本来就是 I2C_OLED_*，不需要任何别名包装。
 * 【方案 B】原样照抄，不再自己接线。 */
#include "I2C_OLED.h"


/*------------------------------------------------------------------------
 * 主屏版面（128 x 64，单位是"页"，1 页 = 8 像素行）
 *
 *   y = 0..3  大字时间  HH : MM   字模 16x32
 *   y = 4..5  日期 + 星期         8x16（汉字来自屏幕上的字库 IC）
 *   y = 6..7  温湿度 + 闹钟开关   8x16
 *------------------------------------------------------------------------*/
#define MAIN_BIG_Y      0
#define MAIN_BIG_X0     24          /* 第 1 位数字的 x */
#define MAIN_BIG_STEP   18          /* 每位之间隔 18 列（字宽 16） */
#define MAIN_DATE_Y     4
#define MAIN_ENV_Y      6

/* 大字时间的 5 个位置里，第 3 个（下标 2）是冒号 */
#define BIG_SLOT_COUNT  5
#define BIG_COLON_SLOT  2

/*------------------------------------------------------------------------
 * 内部状态
 *------------------------------------------------------------------------*/
static u8 s_fullRedraw = 1;
static UIPageId_t s_page = PAGE_HOME;
/* 主屏（SPI）单独的整屏重画请求。与 s_fullRedraw 分开是因为：
 * 主屏重画不需要限流（软件 SPI 约 1ms），而 s_fullRedraw 要管着 I2C 的 800ms 限流。 */
static u8 s_mainDirty = 0;


/* 用于判断"内容变了没有" */
static u8  s_lastMinute  = 0xFF;
static u8  s_lastHour    = 0xFF;
static u8  s_lastDay     = 0xFF;
static u8  s_lastHumi    = 0xFF;
static s16 s_lastTemp    = 0x7FFF;
static u8  s_lastAlarmOn = 0xFF;
static u8  s_lastValid   = 0xFF;
static u16 s_envTimer    = 0;

static u8 s_mainOn = 1;

/* 前置声明：C51 不允许调用"后面才定义"的函数，必须先声明。 */

static u32 s_lastFullExecMs = 0;   /* 上一次真正执行整屏重画的时刻（执行点限流） */

static u16 s_dbgPoll = 0;
static u16 s_dbgSub  = 0;
static u16 s_dbgFull = 0;
static u16 s_dbgHb   = 0;

/*
 * 【2026-09-18 新增 · 诊断】TASK_RENDER 活性探针。
 *
 * 真机现象：副屏画面"被拉宽占据整屏"，重启后画面恢复正常但**再也不更新**，
 * 而此时矩阵键盘还能改时间（说明 TASK_LOGIC 与 I2C 总线都是活的）。
 * 那问题就只剩一个可能：TASK_RENDER 首次画完之后不再刷新。
 *
 * 所以这里统计三件事，每 5 秒打一行：
 *   poll    : Display_Poll 被调用的次数（TASK_RENDER 是否还在跑）
 *   sub     : 副屏真正被刷新的次数（走到写字那一步没有）
 *   full    : 整屏重画的次数
 * 判读：
 *   poll 不涨           -> TASK_RENDER 卡住了（或被 RTX 调度饿死）
 *   poll 涨、sub/full 不涨 -> 走到了 Display_Poll 但没走到刷新（判据/限流的问题）
 *   sub 涨但屏幕不动     -> 控制器寻址模式被改坏（需要控制器级恢复）

/*========================================================================
 *                          主屏（SPI）
 *========================================================================*/

/* 画一个 16x32 的大字（4 页 x 16 列） */
static void spi_draw_digit32(u8 x, u8 y, u8 idx)
{
    u8 page;
    u8 i;

    for (page = 0; page < FONT_PAGES; page++)
    {
        SPI_OLED_address(x, (u8)(y + page));
        for (i = 0; i < FONT_WIDTH; i++)
        {
            SPI_OLED_WR_Byte(DIGIT32[idx][(u16)page * FONT_WIDTH + i], SPI_OLED_DATA);
        }
    }
}

/* 大字时间：HH:MM。
 * 【真机修正】原来小时 < 10 时把首位留空（消隐前导零），显示成 " 0:01"，
 * 用户要求显示 "00:01"。现在两位数字全画。 */
/* 【第 7 点】设置页主屏版面的前置声明。
 * main_full_redraw() 里要用它，而它的定义在文件后面（显示层的安排是
 * "先主屏的大字时钟/菜单，再各任务页版面"），所以这里先声明一下。
 * C51 不允许调用后面才定义的函数。 */
static void main_draw_settings(void);

static void main_draw_bigtime(void)
{
    u8 hh = g_clock.hour;
    u8 mm = g_clock.minute;
    u8 x;

    x = MAIN_BIG_X0;

    spi_draw_digit32(x, MAIN_BIG_Y, (u8)(hh / 10));
    x = (u8)(x + MAIN_BIG_STEP);

    spi_draw_digit32(x, MAIN_BIG_Y, (u8)(hh % 10));
    x = (u8)(x + MAIN_BIG_STEP);

    spi_draw_digit32(x, MAIN_BIG_Y, FONT_COLON);
    x = (u8)(x + MAIN_BIG_STEP);

    spi_draw_digit32(x, MAIN_BIG_Y, (u8)(mm / 10));
    x = (u8)(x + MAIN_BIG_STEP);

    spi_draw_digit32(x, MAIN_BIG_Y, (u8)(mm % 10));
}

/* 把 32 像素高的一行清掉（重画前先擦，避免旧数字残留） */
static void main_clear_row(u8 x, u8 cols)
{
    u8 page;
    u8 i;

    for (page = 0; page < FONT_PAGES; page++)
    {
        SPI_OLED_address(x, (u8)(MAIN_BIG_Y + page));
        for (i = 0; i < cols; i++)
        {
            SPI_OLED_WR_Byte(0x00, SPI_OLED_DATA);
        }
    }
}

/* 星期汉字。用 switch 返回字面量，不用"字符指针数组"——
 * 汉字在 GBK 下是 2 字节，用定宽二维数组很容易算错大小，
 * 字面量交给编译器算长度最稳。 */
static char *week_cn(u8 w)
{
    switch (w)
    {
    case 0: return "周日";
    case 1: return "周一";
    case 2: return "周二";
    case 3: return "周三";
    case 4: return "周四";
    case 5: return "周五";
    case 6: return "周六";
    default: return "---";
    }
}

/* 日期行：2026-09-17 周四  （汉字走屏幕上的字库 IC） */
static void main_draw_date(void)
{
    char buf[32];

    sprintf(buf, "%04d-%02d-%02d %s",
            (int)g_clock.year, (int)g_clock.month, (int)g_clock.day,
            week_cn(g_clock.week));

    SPI_OLED_Display_GB2312_string(0, MAIN_DATE_Y, (u8 *)buf);
}

/* 温湿度行：T25.3C H60% ALT3 */
static void main_draw_env(void)
{
    char buf[32];
    char tbuf[16];
    s16  t;
    u8   i;
    u8   cnt = 0;

    t = g_tempX10;

    if (t < 0)
    {
        t = (s16)(0 - t);
        sprintf(tbuf, "-%d.%d", (int)(t / 10), (int)(t % 10));
    }
    else
    {
        sprintf(tbuf, "%d.%d", (int)(t / 10), (int)(t % 10));
    }

    /* 湿度无效时按《04》F 角色要求显示 "--" */
    if (Sensor_HumiValid())
    {
        sprintf(buf, "T%sC H%d%%", tbuf, (int)g_humi);
    }
    else
    {
        sprintf(buf, "T%sC H--%%", tbuf);
    }

    /* 顺带统计开着几组闹钟，方便一眼看出闹钟是开着的 */
    for (i = 0; i < ALARM_MAX; i++)
    {
        if (g_alarms[i].enable)
        {
            cnt++;
        }
    }

    /* "ALM1" = 当前有 1 组闹钟处于开启状态。
     * 原来写的是 "ALT1"，用户看不懂；而且整行 18 个字符(144px)超过了 128px 的屏宽，
     * 末尾会被截掉。现在缩到 16 个字符正好占满。 */
    sprintf(buf + strlen(buf), " ALM%d", (int)cnt);

    /* 【2026-09-19】补空格到满行 16 个字符（= 128 列）。
     * 湿度从 1 位变 2 位（"H7%" -> "H57%"）会让整行变长、
     * 反过去变短时行尾就会留下上一帧的尾巴。
     * 补满之后每次写入都正好覆盖整行，不用先擦屏、也就不会闪。 */
    {
        u8 n = (u8)strlen(buf);

        while (n < 16)
        {
            buf[n] = ' ';
            n++;
        }
        buf[n] = '\0';
    }

    SPI_OLED_Display_GB2312_string(0, MAIN_ENV_Y, (u8 *)buf);
}

/* 把主屏某一行的 2 页（16 像素高）整行擦掉，避免上一帧更长的字符串留尾巴 */
static void main_clear_line(u8 y)
{
    u8 page;
    u8 i;

    for (page = 0; page < 2; page++)
    {
        SPI_OLED_address(0, (u8)(y + page));
        for (i = 0; i < 128; i++)
        {
            SPI_OLED_WR_Byte(0x00, SPI_OLED_DATA);
        }
    }
}

/* 掌机模式的占位页。
 * 用主屏的硬件字库画中文，证明"M2 的位置已经留好、页面框架已通"，
 * 也顺便验证了主屏的字库链路是好的。 */
static void main_draw_placeholder(void)
{
    SPI_OLED_Display_GB2312_string(32, 2, (u8 *)"掌机模式");
    SPI_OLED_Display_GB2312_string(36, 4, (u8 *)"M2 开发中");
    SPI_OLED_Display_GB2312_string(16, 6, (u8 *)"按KEY2返回主界面");
}

/*========================================================================
 *              主屏（SPI）：任务切换界面   【2026-09-19 UI 改造 S2】
 *
 * 主屏分两级（用户定的模型）：
 *   第 0 级 = 大字时钟（最初始一级）—— 在 main_full_redraw() 里直接画
 *   第 1 级 = 任务清单（本函数）
 * 进了某个任务之后（第 2 级），主屏**仍然画任务清单**，
 * 光标停在那个任务所在的行上 —— 这就是"主屏 = 切换任务"的直接体现。
 *
 * 任务清单的内容（名字、对应页面）在 App_Menu.c 里维护，本函数只负责画，
 * 两边不会再各存一份表。
 *
 * 版面（x = 像素列，y = 页；汉字 16 列宽、ASCII 8 列宽）：
 *     y=0   > 0 时钟
 *     y=2     1 闹钟
 *     y=4     2 番茄钟
 *     y=6     3 测距仪      （共 7 项，窗口随光标滚动）
 * 每行 = 光标(8) + 空格(8) + 序号(8) + 空格(8) + 中文名(16×3) = 80 列（屏宽 128）。
 *========================================================================*/

#define MENU_ROWS   4       /* 主屏一屏放 4 行（8x16 字体，每行占 2 页） */

static void main_draw_menu(void)
{
    /* 第 1 级：光标由导航层给；第 2 级及以上：光标归位到当前任务所在的行 */
    u8   idx = Menu_IsOpen() ? Menu_CursorIdx() : Menu_IndexOf(s_page);

    /* 【2026-09-19 用户要求 · 任务清单第 1 点】
     * 第 1 级（任务清单，菜单打开）：显示全部 4 行选项；
     * 第 2 级（已经进了某个任务）：**只显示当前任务那一行，下面全部留空** ——
     * 只有当 KEY4 返回到第 1 级时才把全部选项显示回来。
     * 这样"进任务"和"在菜单里"在视觉上是两种状态，一眼能分清。 */
    u8   rows = Menu_IsOpen() ? MENU_ROWS : 1;
    u8   row;
    u8   k;
    char buf[24];

    /* 【2026-09-19 照 demo30 改】**光标永远在第一行**，
     * 当前任务固定在顶上，下面依次排它后面的任务，到末尾回绕。
     *
     * demo30 就是这么写的：`(global_cur_pos + i) % menu_cnt`，
     * 并且 `i ? ' ' : '>'` —— 只有第 0 行画光标。
     *
     * 我原来的实现是"光标在 4 行窗口里走、到边界才滚动"，
     * 用户明确要求改成 demo30 那样（光标恒在首行）。
     * 顺带少了一个 top 变量 —— 窗口起点就是光标项本身。 */
    for (row = 0; row < rows; row++)
    {
        k = (u8)(idx + row);
        if (k >= MENU_TASK_COUNT)
        {
            k = (u8)(k - MENU_TASK_COUNT);      /* 回绕；7 项、4 行窗口，最多减一次 */
        }

        /* 格式照 demo30 的菜单行：光标 + 序号 + 名称 */
        sprintf(buf, "%c %d %s", (row == 0) ? '>' : ' ', (int)k, Menu_TaskName(k));

        SPI_OLED_Display_GB2312_string(0, (u8)(row * 2), (u8 *)buf);
    }
}

static void main_full_redraw(void)
{
    SPI_OLED_Clear();

    /* 【第 3 点】「日期和时间」任务：SPI 只显示当前选项（其他选项全部消失）*/
    if (Menu_DateIsActive())
    {
        if (Menu_DateState() == 0)
        {
            SPI_OLED_Display_GB2312_string(0, 0, (u8 *)(Menu_DateChoice() ? "改时分" : "改年月日"));
        }
        else
        {
            SPI_OLED_Display_GB2312_string(0, 0, (u8 *)(Menu_DateChoice() ? "输入时分" : "输入年月日"));
        }
        return;
    }

    /* 【第 7 点】设置任务：主屏显示**全部设置项**（其他页面都是"当前任务一行"）*/
    if (s_page == PAGE_SETTINGS)
    {
        main_draw_settings();
        return;
    }

    /* 【2026-09-19 UI 改造 S2】主屏两级。
     *
     * 第 0 级 = 大字时钟。按用户 2026-09-19 的原话：
     *   "大字时钟依旧放在原处，按键 KEY2 确认键或者叫进入里面一级菜单键的时候，
     *    进入主屏任务清单选项；KEY4 键返回上一级时，大字时钟就是最初始一级"
     * 所以：**只有当前任务就是"时钟"、并且任务清单没打开时，主屏才是大字时钟**。
     *
     * 其余情况（清单打开中、或已进入别的任务）主屏一律画任务清单。 */
    if (s_page == PAGE_HOME && !Menu_IsOpen())
    {
        main_draw_bigtime();
        main_draw_date();
        main_draw_env();
        return;
    }

    main_draw_menu();
}

/*========================================================================
 *                          副屏（I2C，只能 ASCII）
 *========================================================================*/

/*
 * 安全的控制器恢复序列 —— 【2026-09-18 新增 · 针对"黑屏后连重启都救不回来"】
 *
 * 真机现象：按键/电位器操作多了之后副屏全黑，而且**按重启按钮也亮不起来**，
 * 只有拔插 USB（给 OLED 断电）才能恢复 ——
 * 说明坏的不是 MCU 里的状态，而是 **OLED 控制器自己的寄存器**（MCU 重启时它一直供电）。
 *
 * 关键补充：只发 0xAF 是救不回来的。因为最可能被写坏的是**电荷泵使能**：
 *     OLED_Init() 里 0xAE 之后紧跟着 0x8D 0x14（开电荷泵）；
 *     一旦序列被打断、停在 0x8D 0x10（关电荷泵）附近，屏幕就是全黑，
 *     此后只发 0xAF 毫无作用 —— 必须重发 0x8D 0x14 才能点亮面板。
 *
 * 所以这里把控制器的**全部关键配置**重发一遍，唯独：
 *   不发 0xAE（关显示）   -> 任何时刻被打断都不会留下黑屏
 *   不清显存              -> 已有画面不会丢
 * 代价只有 16 次字节事务（约 2ms），所以放在每次整屏重画之前，
 * 相当于"每次重画前先把控制器修好再写"。/* 画一行 16 像素高的 ASCII。line 取 0..3，对应页 0/2/4/6。
 *
 * 【2026-09-18 回退说明】
 * 上一版我把这里改成"整页一次事务批量写"（自己拼 256 字节缓冲 + I2C_WriteNbyte(..,128)），
 * 目的是把每行 258 次 I2C 事务降到 4 次、根治"整屏垂直滚动"。
 * 实机结果是副屏整屏乱码、按键对副屏完全无反应 —— 说明批量写这条路径没走通，
 * 而且很可能卡在里面（TASK_RENDER 出不来），连带主屏环境行也不再刷新。
 *
 * 所以回退到驱动自带的 OLED_ShowString（逐字节、已验证能正确显示），
 * 只用总线锁保证不与读时钟交叉。垂直滚动的隐患改用"周期性整屏重画"兜底：
 * 即使某一次被冲乱，最多 3 秒后自动恢复正确画面。 */
static void sub_draw_line(u8 line, const char *s)
{
    /* 【2026-09-19 修正 · 必须固定宽度】
     *
     * 现在副屏的刷新粒度是"只重画变化的那一行"（照 demo30），
     * **一次只写这一行、不做整行清屏**。如果新字符串比上一帧短，
     * 旧内容的尾巴就会留在屏幕上：
     *   真机现象：VOL 从 "VOL 10/10 SONG 1"(15 字) 降到 "VOL 0/10 SONG 1"(14 字) 后
     *             显示成 "VOL 0/10 SONG 11" —— 多出来的那个 1 就是上一帧的残留。
     *
     * demo30 的做法就是固定宽度：`sprintf(strbuff,"Duty:%6.2f%%",...)`（宽度写死）、
     * `"Temp :    C"`（手动补空格）。这里照做：统一补到 16 个字符 = 128 列满宽，
     * 超长则截断。这样每行写入都正好覆盖整行，不会留尾巴。 */
    char padded[18];
    u8 n;
    u8 i;

    n = (u8)strlen(s);
    if (n > 16)
    {
        n = 16;
    }
    for (i = 0; i < n; i++)
    {
        padded[i] = s[i];
    }
    for (; i < 16; i++)
    {
        padded[i] = ' ';
    }
    padded[16] = '\0';

    I2C_Lock();
    I2C_OLED_ShowString(0, (u8)(line * 2), (u8 *)padded, 16);
    I2C_Unlock();
}

static const char *page_name(UIPageId_t p)
{
    switch (p)
    {
    case PAGE_HOME:        return "CLOCK";
    case PAGE_ALARM_LIST:  return "ALARM LIST";
    case PAGE_ALARM_EDIT:  return "ALARM EDIT";
    case PAGE_POMODORO:    return "POMODORO";
    case PAGE_SETTINGS:    return "SETTINGS";
    case PAGE_RINGING:     return "*** RING ***";
    case PAGE_RANGE:       return "RANGE";
    case PAGE_SERVO:       return "GIMBAL";
    case PAGE_GAME_HALL:   return "GAME HALL";
    case PAGE_GAME_SNAKE:  return "SNAKE";
    case PAGE_GAME_BRICK:  return "BRICK";
    case PAGE_GAME_PLANE:  return "PLANE";
    case PAGE_GAME_DAILY:  return "DAILY";
    case PAGE_GAME_OVER:   return "GAME OVER";
    default:               return "?";
    }
}

/* 星期缩写（副屏只有 ASCII，不能显示汉字） */
static char *week_abbr(u8 w)
{
    switch (w)
    {
    case 0: return "SUN";
    case 1: return "MON";
    case 2: return "TUE";
    case 3: return "WED";
    case 4: return "THU";
    case 5: return "FRI";
    case 6: return "SAT";
    default: return "---";
    }
}

/*
 * 副屏每秒只需要刷"时间那行"。整屏 4 行刷一遍要走 4 x 256 字节 I2C，
 * 约 80~90ms，期间总线锁一直被占着，会把 TASK_LOGIC 读时间卡住。
 * 只刷一行约 20ms，占用降到一个可以接受的水平。/*========================================================================
 *      副屏批量写：一页一次 I2C 事务   —— 任务清单第 2b 点
 *
 * 【为什么值得做】
 *   驱动原来的 I2C_OLED_WR_Byte() 是**每写 1 个字节就发一次完整 I2C 事务**
 *   （START + 设备地址 + 控制字节 + 数据 + STOP，约 340us）。
 *   整屏 128x64 = 1024 字节 = **1024 次事务 ≈ 350ms**，挪一下光标要等三分之一秒。
 *
 *   而厂家库的 I2C_WriteNbyte() 本身就是"一次事务连发 number 个字节"
 *   （Lib/I2C.c 里是 do{ SendData(*p++); RecvACK(); } while(--number);），
 *   参数 number 是 u8，最大 255 —— 一页 128 字节正好。
 *   ⇒ 整屏只要 **8 次数据事务 + 8 次定位命令 = 16 次 ≈ 5ms，快约 70 倍。**
 *
 * 【为什么以前失败过、这次敢重试】
 *   09-18 第 6 轮试过一次"整页一次事务批量写"，结果副屏全乱码、按键无反应，回退了。
 *   但那时工程还是"**两个任务共用一个 I2C 控制器 + 4 秒超时强夺**"的旧架构，
 *   根因（并发访问同一条总线）到第 26 轮才消除 —— 现在全部 I2C 已经收进
 *   TASK_RENDER 一个任务、无锁竞争。**前提条件已经不同，所以重试。**
 *
 * 【这是受控试验：出问题怎么回退】
 *   只改了这一处。如果副屏出现花屏/错位/乱码，把 sub_blit 里那句
 *   I2C_WriteNbyte 换成下面注释掉的逐字节循环即可，其它代码不用动。
 */
static u8 xdata s_lineBuf[128];     /* 从 code 区（图标）搬到 xdata 的中转行 */

static void sub_blit(u8 x, u8 page, u8 w, u8 *buf)
{
    I2C_Lock();
    I2C_OLED_Set_Pos(x, page);                  /* 定位：3 字节命令事务 */
    I2C_WriteNbyte(0x78, 0x40, buf, w);         /* 数据：一次事务写 w 字节 */

    /* 回退用（逐字节版）：
     * { u8 i; for (i = 0; i < w; i++) { I2C_OLED_WR_Byte(buf[i], I2C_OLED_DATA); } }
     */

    I2C_Unlock();
}

/*========================================================================
 *          云台仪表盘（I2C 副屏）—— 任务清单第 6 点
 *
 * 用户要求原文：「确认后最后一行显示为 Angle 180 DEG，上面一行可以改为一个
 *   只有边缘有线的半圆，里面有一个粗针，每次改变角度，粗针就改变角度。
 *   关于云台方面，每次改变 10 角度就很好。」
 *
 * 版面（I2C 128x64）：
 *     page 0~1   GIMBAL              任务名（由 sub_full_redraw 画）
 *     page 2~5   半圆轮廓 + 粗针        32 行高（本模块负责）
 *     page 6~7   Angle  90 DEG        角度行（本模块负责）
 *
 * 几何：圆心 (64, 46)，半径 30，指针长 24。
 *       角度映射 —— 舵机 0° 指左、90° 指上、180° 指右。
 *
 * 【为什么不用取模图片】
 *   指针有 19 个位置（0/10/.../180），取模要 19 张图。
 *   改用"定点 cos 表 + Bresenham 画线"实时算：91 字节的表 + 几十行代码，
 *   零取模素材、零浮点，而且以后改成 5° 步进也不用重新取模。
 *
 * 【只重画一小块，不整屏重画】
 *   区域 64 列 x 4 页 = 256 字节，约 85ms @400kHz。
 *   整屏是 1024 字节 = 350ms，转一次针花 350ms 手感很差，所以只写这一块。
 *========================================================================*/

/* 【2026-09-19 用户要求】"4 云台 最上面一行应该原本显示的字母消失了，
 *   那就把云台的半圆占据上面的全部内容" —— 原来 page0~1 是标题行(GIMBAL)，
 *   现在标题不画了，区域整体上移、放大：占 page0~5（整个上半屏 48 行）。
 *   角度行仍在 page6~7（由 sub_draw_line(3,...) 写）。 */
#define GAUGE_X0       0    /* 区域左上角（像素列）—— 整屏宽 */
#define GAUGE_W      128
#define GAUGE_PAGE0    0    /* 从第 0 页开始，上面不留标题 */
#define GAUGE_PAGES    6    /* 6 页 = 48 行，到角度行之前 */
#define GAUGE_H       (GAUGE_PAGES * 8)

#define GAUGE_CX      64    /* 圆心 x（画布正中）*/
#define GAUGE_CY      46    /* 圆心 y（区域局部坐标）：46-46=0 ⇒ 弧顶正好落在第 0 行 */
#define GAUGE_R       46    /* 半圆半径：左右到 x=18 / x=110 */
#define GAUGE_NEEDLE  38    /* 指针长度 */
#define GAUGE_NEEDLE_W 4    /* 指针粗细（像素）—— 用户要求"粗针" */

/* cos(0~90°) x 255。取 255 而不是 256 —— 256 放不进 u8。
 * sin(a) 由 cos(90-a) 得到，所以只要这 91 个值。 */
static u8 code s_cos255[91] =
{
    255, 255, 255, 255, 254, 254, 254, 253, 253, 252, 251, 250, 249,
    248, 247, 246, 245, 244, 243, 241, 240, 238, 236, 235, 233, 231,
    229, 227, 225, 223, 221, 219, 216, 214, 211, 209, 206, 204, 201,
    198, 195, 192, 190, 186, 183, 180, 177, 174, 171, 167, 164, 160,
    157, 153, 150, 146, 143, 139, 135, 131, 128, 124, 120, 116, 112,
    108, 104, 100,  96,  91,  87,  83,  79,  75,  70,  66,  62,  57,
     53,  49,  44,  40,  35,  31,  27,  22,  18,  13,   9,   4,   0
};

/* 这一小块显存的镜像（xdata：Compact 模型下大数组必须显式指定）*/
static u8 xdata s_gauge[GAUGE_W * GAUGE_PAGES];

/* 单独重画仪表盘的请求标志（由 TASK_LOGIC 置，实际绘制由 TASK_RENDER 做）*/
static u8 s_gaugeDirty = 0;

/* 在区域内点一个像素（越界自动丢弃，调用方不用做边界检查）*/
static void gauge_pixel(u8 x, u8 y)
{
    if (x >= GAUGE_W || y >= GAUGE_H)
    {
        return;
    }

    s_gauge[(u16)(y >> 3) * GAUGE_W + x] |= (u8)(1U << (y & 7));
}

static void gauge_clear(void)
{
    u16 i;

    for (i = 0; i < (u16)(GAUGE_W * GAUGE_PAGES); i++)
    {
        s_gauge[i] = 0;
    }
}

/* 取 cos/sin（角度 0~180，仍是 255 倍）。
 * cos(180-a) = -cos(a)，sin(180-a) = sin(a) —— 用余弦表的对称性覆盖整个半圆。 */
static void gauge_cossin(u16 deg, s16 *pc, s16 *ps)
{
    if (deg <= 90)
    {
        *pc = (s16)s_cos255[deg];
        *ps = (s16)s_cos255[90 - deg];
    }
    else
    {
        *pc = (s16)(0 - (s16)s_cos255[180 - deg]);
        *ps = (s16)s_cos255[deg - 90];
    }
}

/* Bresenham 画线。每走一步就盖一个 w x w 的方块 —— 这样不管针是竖直、
 * 水平还是斜的，粗细都一致（原来只画 (x,y) 和 (x+1,y) 两个点，
 * 竖着的针只有 2 像素宽，在 128 宽的屏上显得太细，用户说"针要粗"）。 */
static void gauge_line(s16 x0, s16 y0, s16 x1, s16 y1, u8 w)
{
    s16 dx, dy, sx, sy, err, e2;
    s16 h = (s16)(w / 2);
    s16 i, j;

    dx = (s16)((x1 > x0) ? (x1 - x0) : (x0 - x1));
    dy = (s16)((y1 > y0) ? (y1 - y0) : (y0 - y1));
    sx = (s16)((x0 < x1) ? 1 : -1);
    sy = (s16)((y0 < y1) ? 1 : -1);
    err = (s16)(dx - dy);

    while (1)
    {
        for (i = 0; i < (s16)w; i++)
        {
            for (j = 0; j < (s16)w; j++)
            {
                gauge_pixel((u8)(s16)(x0 - h + i), (u8)(s16)(y0 - h + j));
            }
        }

        if (x0 == x1 && y0 == y1)
        {
            break;
        }

        e2 = (s16)(err << 1);

        if (e2 > (s16)(0 - dy))
        {
            err = (s16)(err - dy);
            x0  = (s16)(x0 + sx);
        }
        if (e2 < dx)
        {
            err = (s16)(err + dx);
            y0  = (s16)(y0 + sy);
        }
    }
}

/* 画半圆轮廓：每 1° 取一个点（半径 30 时 1° 才 0.52 像素，保证不断线）。
 * "只有边缘有线" —— 内部不填充，就是这些点连成的弧。 */
static void gauge_arc(void)
{
    u16 a;
    s16 c, s;

    for (a = 0; a <= 180; a++)
    {
        gauge_cossin(a, &c, &s);

        gauge_pixel((u8)(s16)(GAUGE_CX + (s16)(((s16)GAUGE_R * c) / 255)),
                    (u8)(s16)(GAUGE_CY - (s16)(((s16)GAUGE_R * s) / 255)));
    }
}

/* 轴心：以圆心为中心的 3x3 实心块，让粗针看起来"钉"在圆心上 */
static void gauge_hub(void)
{
    s16 dx, dy;

    for (dy = -3; dy <= 3; dy++)
    {
        for (dx = -3; dx <= 3; dx++)
        {
            gauge_pixel((u8)(s16)(GAUGE_CX + dx), (u8)(s16)(GAUGE_CY + dy));
        }
    }
}

/* 把这一小块显存写进 OLED。按页写，每页 64 字节。 */
static void gauge_flush(void)
{
    u8 page;

    /* 【点 2b】整页一次事务写下去。每页 128 字节 -> 1 次事务，
     * 6 页共 6 次数据事务 + 6 次定位 ≈ 4ms（原来逐字节是 768 次事务 ≈ 260ms）。 */
    for (page = 0; page < GAUGE_PAGES; page++)
    {
        sub_blit(GAUGE_X0, (u8)(GAUGE_PAGE0 + page), GAUGE_W,
                 &s_gauge[(u16)page * GAUGE_W]);
    }
}

/* 云台页专用：重画"半圆 + 粗针 + 角度行" */
static void sub_draw_gauge(void)
{
    char buf[20];
    u16  screenDeg;
    u8   angle = Servo_GetAngle();
    s16  c, s;

    gauge_clear();
    gauge_arc();
    gauge_hub();

    /* 指针方向：舵机 0° 指左、180° 指右 => 屏幕方向角 = 180 - 舵机角 */
    screenDeg = (u16)(180 - (u16)angle);
    gauge_cossin(screenDeg, &c, &s);

    gauge_line((s16)GAUGE_CX, (s16)GAUGE_CY,
               (s16)(GAUGE_CX + (s16)(((s16)GAUGE_NEEDLE * c) / 255)),
               (s16)(GAUGE_CY - (s16)(((s16)GAUGE_NEEDLE * s) / 255)),
               GAUGE_NEEDLE_W);

    gauge_flush();

    sprintf(buf, "Angle%4d DEG", (int)angle);
    sub_draw_line(3, buf);
}

/*========================================================================
 *        第 1 级任务清单的图标（I2C 副屏）—— 任务清单第 2 点 · 版面 A
 *
 * 用户要求：「在第二级菜单时，SPI 屏幕中"`>`"当前指向的任务，
 *   对应的 I2C 屏幕会显示当前任务的图片，图片的样子最贴切当前任务的名称。」
 * 用户选的是**版面 A** —— 整屏只有一张图，不放文字（任务名在主屏菜单上已经有了）。
 *
 * 位置 (32, 8)：64 宽居中、48 高上下各留 8 行，并且**正好落在 page1~6 上**（完全页对齐，
 * 不需要读-改-写，直接整页覆盖）。
 *
 * 光标挪一次只重写这 6 页 = 384 字节；
 * 整块每次都是全量覆盖（每字节都有明确值），所以不会留上一张图的残影、也不用先清屏。
 *========================================================================*/

/* 【2026-09-19 修正 · 用户报的"图标被切成 6 段错位"】
 * 图标已经从 64x48 换成了**整屏 128x64**，但下面这几个参数还是 64x48 时代的旧值，
 * 于是"数据按 128 列打包、却按 64 列去取" —— 每页只取了前半行，
 * 而且从 page1 开始只写 6 页，正好就是用户看到的"切成 6 段、每段独占一行"。
 * 现在跟着数据布局一起改成全屏。 */
#define ICON_X      0       /* 横向从第 0 列开始 */
#define ICON_PAGE0  0       /* 纵向从第 0 页开始 */
#define ICON_W      128     /* 整屏宽 */
#define ICON_PAGES  8       /* 8 页 = 64 行，整屏 */

/* 只重画图标块的请求标志 */
static u8 s_iconDirty = 0;

static void sub_draw_icon(void)
{
    u8 code *p = Icon_Get(Menu_IsOpen() ? Menu_CursorIdx() : Menu_IndexOf(s_page));
    u8 page;
    u8 i;

    /* 【点 2b】整页一次事务。注意图标数据在 **code 区**（Flash），
     * 而 I2C_WriteNbyte() 要的是 data 指针，所以先把这一页 128 字节
     * 搬到 xdata 的 s_lineBuf，再整页写出去。
     * 8 页共 8 次数据事务 + 8 次定位 ≈ 5ms（原来 1024 次事务 ≈ 350ms）。 */
    for (page = 0; page < ICON_PAGES; page++)
    {
        for (i = 0; i < ICON_W; i++)
        {
            s_lineBuf[i] = *(p + (u16)page * ICON_W + i);
        }

        sub_blit(ICON_X, (u8)(ICON_PAGE0 + page), ICON_W, s_lineBuf);
    }
}

/* 把第 4 行（最后一行）的内容写进 buf。
 * 单独抽出来是为了让"只刷一行"（s_line3Dirty）能复用同一套优先级逻辑。 */
/*========================================================================
 *                              对外接口
 *========================================================================*/

/* 只请求重画云台仪表盘（半圆 + 粗针 + 角度行）。
 * 一次约 256 字节 ≈ 85ms，同样不受整屏限流约束 —— 转针要跟手。 */
/*========================================================================
 *          闹钟页版面 —— 任务清单第 4 点
 *
 * 用户规格：「I2C 第一行只显示 Alarm Edit，下面显示为三个闹钟，
 *   而且全部字体靠右显示，只有第一个闹钟字体靠左显示，
 *   靠左显示代表我当前可以设置这个闹钟。」
 * ⇒ **靠左 = 当前选中项**；编辑态的字段用同一套规则。
 *
 * 列表态                          编辑态
 *   Alarm Edit                      Edit 3/8
 *   07:30 WKDY        ← 靠左        HOUR 07        ← 靠左
 *          12:00 DAILY ← 靠右             MIN 30   ← 靠右
 *          18:00 WKND  ← 靠右             DAY WKDY ← 靠右
 *========================================================================*/

/* DAY_xxx 掩码 -> 一行短描述。用户确认"周为 0 默认每天都生效"，
 * 所以 DAY_NONE(0) 和 DAY_ALL(0x7F) 都按"每天"显示。 */
static char *alarm_days_str(u8 days)
{
    if (days == DAY_NONE || days == DAY_ALL) { return "DAILY"; }
    if (days == DAY_WORKDAY)                 { return "WKDY"; }
    if (days == DAY_WEEKEND)                 { return "WKND"; }

    switch (days)
    {
    case DAY_MON: return "MON";
    case DAY_TUE: return "TUE";
    case DAY_WED: return "WED";
    case DAY_THU: return "THU";
    case DAY_FRI: return "FRI";
    case DAY_SAT: return "SAT";
    case DAY_SUN: return "SUN";
    default:      return "MULTI";
    }
}

/* 一行文字：靠左或靠右，并**补齐到满行 16 字符**（短串不补会在行尾留残影） */
static void sub_draw_row(u8 line, char *s, u8 alignLeft)
{
    char buf[18];
    u8 n = (u8)strlen(s);
    u8 i;

    if (n > 16) { n = 16; }

    for (i = 0; i < 16; i++) { buf[i] = ' '; }
    buf[16] = '\0';

    if (alignLeft)
    {
        for (i = 0; i < n; i++) { buf[i] = s[i]; }
    }
    else
    {
        for (i = 0; i < n; i++) { buf[16 - n + i] = s[i]; }
    }

    sub_draw_line(line, buf);
}

static char *alarm_field_name(u8 f)
{
    switch (f)
    {
    case 0: return "HOUR";
    case 1: return "MIN";
    case 2: return "DAY";
    case 3: return "SW";
    case 4: return "SONG";
    default: return "???";
    }
}

static void sub_draw_alarm_page(void)
{
    char  buf[24];
    char  val[12];
    u8    i;
    u8    top;
    u8    k;
    const AlarmItem_t *p;

    if (!Menu_AlarmIsEditing())
    {
        sprintf(buf, "Alarm Edit");
        sub_draw_row(0, buf, 1);

        top = 0;
        if (Menu_AlarmSel() >= 1)
        {
            top = (u8)(Menu_AlarmSel() - 1);
            if (top > (u8)(ALARM_MAX - 3)) { top = (u8)(ALARM_MAX - 3); }
        }

        for (i = 0; i < 3; i++)
        {
            k = (u8)(top + i);
            if (k >= ALARM_MAX) { break; }

            sprintf(buf, "%02d:%02d %s%s",
                    (int)g_alarms[k].hour, (int)g_alarms[k].minute,
                    alarm_days_str(g_alarms[k].days),
                    g_alarms[k].enable ? "" : " off");

            sub_draw_row((u8)(i + 1), buf, (u8)(k == Menu_AlarmSel()));
        }
        return;
    }

    p = Menu_AlarmEditBuf();

    sprintf(buf, "Edit %d/%d", (int)Menu_AlarmEditIndex() + 1, (int)ALARM_MAX);
    sub_draw_row(0, buf, 1);

    top = 0;
    if (Menu_AlarmEditField() >= 1)
    {
        top = (u8)(Menu_AlarmEditField() - 1);
        if (top > 2) { top = 2; }
    }

    for (i = 0; i < 3; i++)
    {
        k = (u8)(top + i);
        if (k > 4) { break; }

        switch (k)
        {
        case 0: sprintf(val, "%02d", (int)p->hour);           break;
        case 1: sprintf(val, "%02d", (int)p->minute);         break;
        case 2: sprintf(val, "%s", alarm_days_str(p->days));  break;
        case 3: sprintf(val, "%s", p->enable ? "ON" : "OFF"); break;
        default: sprintf(val, "%d", (int)p->song + 1);        break;
        }

        sprintf(buf, "%s %s", alarm_field_name(k), val);
        sub_draw_row((u8)(i + 1), buf, (u8)(k == Menu_AlarmEditField()));
    }
}

/* 番茄钟运行状态的一行描述（RUN/PAUSE + 剩余时间）*/
static char *s_pomoRunStateStr(void)
{
    static char s[20];

    if (!Menu_PomoRunning())
    {
        sprintf(s, "IDLE %d min", (int)g_settings.pomodoro_work);
    }
    else
    {
        sprintf(s, "%s %02d:%02d",
                Menu_PomoIsRest() ? "REST" : "FOCUS",
                (int)(Menu_PomoRemain() / 60U), (int)(Menu_PomoRemain() % 60U));
    }
    return s;
}

/*========================================================================
 *          番茄钟页版面 —— 任务清单第 5 点（同第 4 点的"靠左/靠右"）
 *
 * 列表态                          编辑态
 *   Pomodoro                        Edit FOCUS
 *   FOCUS 25        ← 靠左          25 min         ← 靠左
 *         REST 5    ← 靠右
 *         RUN 12:34 ← 靠右
 *========================================================================*/
static void sub_draw_pomo_page(void)
{
    char buf[24];
    char nam[8];
    u8   i;
    u8   sel = Menu_PomoSel();

    if (Menu_PomoIsEditing())
    {
        /* ---- 编辑态 ---- */
        /* 【修】原来是 sprintf(buf, "Edit %s", buf) —— 源和目标是同一个缓冲区，
         * 属于未定义行为（C51 的 sprintf 边读边写会互相破坏）。
         * 改成先写临时串再拼接。 */
        switch (sel)
        {
        case 0: sprintf(nam, "FOCUS"); break;
        case 1: sprintf(nam, "REST");  break;
        default: sprintf(nam, "RUN");  break;
        }
        sprintf(buf, "Edit %s", nam);
        sub_draw_row(0, buf, 1);

        if (sel == 0)      { sprintf(buf, "%d min", (int)g_settings.pomodoro_work); }
        else if (sel == 1) { sprintf(buf, "%d min", (int)g_settings.pomodoro_rest); }
        else               { sprintf(buf, "%s", s_pomoRunStateStr()); }
        sub_draw_row(1, buf, 1);
        return;
    }

    /* ---- 列表态 ---- */
    sprintf(buf, "Pomodoro");
    sub_draw_row(0, buf, 1);

    for (i = 0; i < 3; i++)
    {
        switch (i)
        {
        case 0: sprintf(buf, "FOCUS %d", (int)g_settings.pomodoro_work); break;
        case 1: sprintf(buf, "REST %d",  (int)g_settings.pomodoro_rest); break;
        default:
            sprintf(buf, "%s", s_pomoRunStateStr());
            break;
        }
        sub_draw_row((u8)(i + 1), buf, (u8)(i == sel));
    }
}

/*========================================================================
 *          「日期和时间」任务的 I2C 版面 —— 任务清单第 3 点
 *
 * 用户规格：「没点击确认修改什么时间时，I2C 屏幕依旧显示图片；
 *   点击确认后，I2C 屏幕显示为修改时间反馈……
 *   年内容修改显示在第一行，月日修改在第二行，I2C 屏幕下面显示为当前年月日」
 *========================================================================*/
static void sub_draw_date_entry(void)
{
    char buf[24];

    if (Menu_DateChoice() == 0)
    {
        sprintf(buf, "YEAR %04u", (unsigned)Menu_DateY());
        sub_draw_row(0, buf, 1);

        sprintf(buf, "DATE %04u", (unsigned)Menu_DateMD());
        sub_draw_row(1, buf, 1);

        sprintf(buf, "NOW  %04d-%02d-%02d",
                (int)g_clock.year, (int)g_clock.month, (int)g_clock.day);
        sub_draw_row(2, buf, 1);

        sprintf(buf, "K4 DEL   %d/8", (int)Menu_DateCnt());
        sub_draw_row(3, buf, 1);
    }
    else
    {
        sprintf(buf, "HOUR %02u", (unsigned)Menu_DateH());
        sub_draw_row(0, buf, 1);

        sprintf(buf, "MIN  %02u", (unsigned)Menu_DateM());
        sub_draw_row(1, buf, 1);

        sprintf(buf, "NOW  %02d:%02d:%02d",
                (int)g_clock.hour, (int)g_clock.minute, (int)g_clock.second);
        sub_draw_row(2, buf, 1);

        sprintf(buf, "K4 DEL   %d/4", (int)Menu_DateCnt());
        sub_draw_row(3, buf, 1);
    }
}

/*========================================================================
 *          设置页版面 —— 任务清单第 7 点
 *
 * 用户要求：
 *   「"5 设置"按下确认键后，进入下一级菜单，
 *     **SPI 屏幕显示全部的设置项**，**I2C 屏幕来显示具体设置项的功能内容**」
 *
 * SPI（4 行窗口，选中行靠左带 '>'）      I2C（只显示"当前项"的功能内容）
 *   > 0 音量 60%                          VOLUME
 *     1 曲目 1                            60%
 *     2 提醒 RING                         (空)
 *     3 主屏 ON                           K2 EDIT      ← 编辑态显示 K2 OK / K4 SAVE
 *========================================================================*/

/* SPI 用的中文短名 */
static char *settings_item_cn(u8 i)
{
    switch (i)
    {
    case 0:  return "音量";
    case 1:  return "曲目";
    case 2:  return "提醒";
    case 3:  return "主屏";
    case 4:  return "日出";
    case 5:  return "贪睡";
    default: return "震动";
    }
}

/* I2C 用的英文名（副屏只能 ASCII）*/
static char *settings_item_en(u8 i)
{
    switch (i)
    {
    case 0:  return "VOLUME";
    case 1:  return "SONG";
    case 2:  return "ALERT";
    case 3:  return "SCREEN";
    case 4:  return "SUNRISE";
    case 5:  return "SNOOZE";
    default: return "VIBRATE";
    }
}

static void settings_value_str(u8 i, char *s)
{
    switch (i)
    {
    case 0:  sprintf(s, "%d%%", (int)g_settings.volume);   break;
    case 1:  sprintf(s, "%d",   (int)g_settings.song + 1); break;
    case 2:  sprintf(s, "%s",   g_settings.alert_mode == ALERT_RING ? "RING" : "VIBR"); break;
    case 3:  sprintf(s, "%s",   g_settings.screen_on ? "ON" : "OFF");  break;
    case 4:  sprintf(s, "%s",   g_settings.sunrise_en ? "ON" : "OFF"); break;
    case 5:  sprintf(s, "%dmin", (int)g_settings.snooze_min); break;
    default: sprintf(s, "%d%%", (int)g_settings.vibrate);  break;
    }
}

/* 主屏：显示全部设置项（4 行窗口，跟着选中项滚动）*/
static void main_draw_settings(void)
{
    char buf[32];
    char val[12];
    u8   sel = Menu_SetSel();
    u8   row;
    u8   k;

    /* 【和任务清单同款】光标恒在第一行、后续项依次排并到末尾回绕。
     * 这样两个列表的观感一致（用户对一致性很敏感），
     * 也避免"窗口跟着光标滚"和"光标固定"两套逻辑并存。
     * 7 项、4 行窗口 ⇒ 回绕最多减两次。 */
    for (row = 0; row < 4; row++)
    {
        k = (u8)(sel + row);
        while (k >= SET_ITEM_COUNT)
        {
            k = (u8)(k - SET_ITEM_COUNT);
        }

        settings_value_str(k, val);
        sprintf(buf, "%c %d %s %s", (row == 0) ? '>' : ' ', (int)k,
                settings_item_cn(k), val);

        SPI_OLED_Display_GB2312_string(0, (u8)(row * 2), (u8 *)buf);
    }
}

/* 副屏：只显示"当前这一项"的功能内容 */
static void sub_draw_settings_page(void)
{
    char buf[24];
    u8   sel = Menu_SetSel();

    sprintf(buf, "%s", settings_item_en(sel));
    sub_draw_row(0, buf, 1);

    settings_value_str(sel, buf);
    sub_draw_row(1, buf, 1);

    /* 操作提示按"当前项"和"状态"分别显示。
     * 音量和震动强度这两项**旋钮直接能调**（列表态、编辑态都行），
     * 所以提示里把旋钮写在最前面；其余项只能用 KEY1/KEY3 调。 */
    if (sel == 0 || sel == 6)
    {
        sub_draw_row(2, Menu_SetIsEditing() ? "KNOB / K1- K3+" : "TURN THE KNOB", 1);
    }
    else
    {
        sub_draw_row(2, Menu_SetIsEditing() ? "K1-  K3+" : "K2 EDIT", 1);
    }

    sub_draw_row(3, Menu_SetIsEditing() ? "K2 OK  K4 SAVE" : "K4 SAVE+EXIT", 1);
}

static void sub_full_redraw(void)
{
    /* 【2026-09-19 用户要求】I2C 副屏不再显示"时间 / 日期 / VOL / SONG"那一套了。
     * 现在的规则是按层级决定，**没内容就全黑**：
     *     第 1 级 任务清单   → 当前光标所指任务的图标（版面 A，整屏一张 64x48 图）
     *     云台任务页         → 半圆仪表盘 + 角度行
     *     其余（第 0 级大字时钟、还没做的任务页）→ 整屏全黑，什么都不显示
     * 用户原话："主菜单 I2C 屏幕什么都不显示，包括返回到主菜单时，I2C 屏幕也是都不显示。"
     *
     * Clear 每次都做：从"有内容"的页面退回来时必须把上一屏擦干净（副屏不会自己清）。 */
    I2C_Lock();
    I2C_OLED_Clear();
    I2C_Unlock();

    /* 「日期和时间」任务（第 3 点）：
     * 选项选择态 I2C 仍显示图标；进入输入态才显示输入反馈。 */
    if (Menu_DateIsActive())
    {
        if (Menu_DateState() == 0) { sub_draw_icon(); }
        else                       { sub_draw_date_entry(); }
        return;
    }

    if (Menu_IsOpen())
    {
        /* 第 1 级：任务图标 */
        sub_draw_icon();
        return;
    }

    if (s_page == PAGE_SERVO)
    {
        /* 云台：半圆仪表盘 + 角度行（它自带版面，不从第 1 行开始画） */
        sub_draw_gauge();
        return;
    }

    if (s_page == PAGE_ALARM_LIST)
    {
        /* 闹钟：列表态 / 编辑态两套版面（第 4 点）*/
        sub_draw_alarm_page();
        return;
    }

    if (s_page == PAGE_POMODORO)
    {
        /* 番茄钟：同样的"列表 + 靠左/靠右"（第 5 点）*/
        sub_draw_pomo_page();
        return;
    }

    if (s_page == PAGE_SETTINGS)
    {
        /* 设置：副屏显示"当前项的功能内容"（第 7 点）*/
        sub_draw_settings_page();
        return;
    }

    /* 其它页面：保持全黑 */
}

void Display_RequestGauge(void)
{
    s_gaugeDirty = 1;
}

/* 只请求重画任务图标（菜单里挪光标时用）。384 字节，不整屏重画。 */
void Display_RequestIcon(void)
{
    s_iconDirty = 1;
}

void Display_RequestFull(void)
{
    /* 【2026-09-19 修正 · 用户报的"返回上一级后副屏还留着图标"】
     *
     * 这里原来有一道**请求点的限流**：距上次请求不到 1200ms 就 `return`，
     * **连 s_fullRedraw 都不置** —— 也就是说这个请求被**直接丢弃**了。
     *
     * 真机路径：在第 1 级任务清单按 KEY4 回第 0 级，要清掉图标；
     * 如果这一刻距上次整屏重画不到 1200ms（进菜单时刚画过），请求被丢掉，
     * 副屏就永远停在图标上，再也不会变黑。
     *
     * 而执行点本来就有 800ms 限流，它的做法是"return 但保留 s_fullRedraw"，
     * **不丢信息**、下一拍继续做。所以请求点这道是多余且有害的，删掉。
     * 现在这里只做一件事：置标志。真正的限流全部交给执行点。 */
    s_fullRedraw = 1;
}

void Display_SetPage(UIPageId_t page)
{
    if (s_page != page)
    {
        s_page = page;
        s_fullRedraw = 1;
    }
}

void Display_MainPower(u8 on)
{
    if (on)
    {
        SPI_OLED_DisPlay_On();
        s_fullRedraw = 1;
    }
    else
    {
        SPI_OLED_DisPlay_Off();
    }
    s_mainOn = on;
}

void Display_Init(void)
{
    /* ---- 主屏 ---- */
    SPI_OLED_Init();

    /* 【诊断】SPI 通路自检：从屏上那块晶联讯字库 IC 里按地址读回 ASCII '8' 的 16 字节点阵。
     * 字库 IC 地址公式（驱动源码里的原文）：
     *     Address = ((MSB-0xB0)*94 + (LSB-0xA1) + 846) * 32     汉字
     *     Address = 0x3CF80 + (ASCII - 0x20) * 16               半角
     * '8' = 0x38 -> 0x3CF80 + 0x18 * 16 = 0x3D100
     *
     * 判读：
     *   打印出来的 16 个字节像点阵（不是全 00、也不是全 FF）-> SPI 的 SCL/MOSI/MISO/片选都通，
     *                                                          主屏黑就只剩"屏本身/供电"这一类可能；
     *   全 00 或全 FF                                        -> SPI 通路本身没建立起来。
     * 这一步只读不写屏，不影响任何显示逻辑。 */
    {
        u8 chk[16];
        u8 i;

        SPI_OLED_get_data_from_ROM(0x03, 0xD1, 0x00, chk, 16);

        printf("[SPI] ROM readback ('8'): ");
        for (i = 0; i < 16; i++)
        {
            printf("%02X ", (unsigned)chk[i]);
        }
        printf("\r\n");
    }

    SPI_OLED_ColorTurn(0);
    SPI_OLED_DisplayTurn(0);
    SPI_OLED_Clear();

    /* 主屏初始化完成立刻写一行测试文字：
     * 这是"诊断手段"，输出不依赖任何任务刷新（规范第四节）——
     * 只要屏亮且能看到这行字，就说明 SPI 线序和屏本身是好的。 */
    SPI_OLED_Display_GB2312_string(0, 0, (u8 *)"ALARM BOOT OK");
    printf("[SPI] main screen init done\r\n");

    /* ---- 副屏 ---- */
    I2C_Lock();
    I2C_OLED_Init();
    I2C_OLED_ColorTurn(0);
    I2C_OLED_DisplayTurn(0);
    I2C_Unlock();

    /* 不在这里单独 Clear/写字：紧接着的第一次 Display_Poll 会走整屏重画，
     * 由 sub_full_redraw() 把 4 行完整刷一遍（每一行都会清掉自己在的 2 页）。 */

    s_fullRedraw = 1;
}

/* 主屏（SPI）整屏重画 + 同步"上一帧"记录。
 *
 * 抽成函数是因为现在有**两条**路径会重画主屏：
 *   ① 菜单挪光标（s_mainDirty）—— 只影响主屏
 *   ② 切页面（s_fullRedraw）  —— 主屏和副屏都要重画
 * 两条都必须同步 s_last*，否则紧接着的增量刷新会白画一遍。
 *
 * 【为什么要单开 ① 这条路 —— 真机 bug】
 * 现象：进任务清单后按 KEY3，有时"没反应"，再按一下会 skip 掉中间那一项
 *       （0 时钟 -> 按键 -> 还是 0 时钟 -> 再按 -> 直接到 2 番茄钟），
 *       刚插 USB 时特别容易触发。
 * 根因：原来 L1 里挪一次光标就调 Display_RequestFull()，而 s_fullRedraw 的执行点
 *       带 800ms 限流（那个限流是为了保护 I2C 总线）。主屏明明只要软件 SPI 画 4 行
 *       （约 1ms），却被 I2C 的限流一起挡住 —— 屏幕没动，用户以为没生效，
 *       再按一次 cursor 已经加了两格，于是"跳项"。
 * 现在主屏重画走 s_mainDirty，**不受 I2C 限流约束**。 */
static void main_repaint(void)
{
    u8 i;
    u8 alarmOn = 0;

    if (!s_mainOn)
    {
        return;
    }

    main_full_redraw();

    for (i = 0; i < ALARM_MAX; i++)
    {
        if (g_alarms[i].enable)
        {
            alarmOn++;
        }
    }

    s_lastHour    = g_clock.hour;
    s_lastMinute  = g_clock.minute;
    s_lastDay     = g_clock.day;
    s_lastTemp    = g_tempX10;
    s_lastHumi    = g_humi;
    s_lastAlarmOn = alarmOn;
    s_lastValid   = Sensor_HumiValid();
}

void Display_RequestMain(void)
{
    s_mainDirty = 1;
}

void Display_Poll(void)
{
    u8 needBig    = 0;
    u8 needDate   = 0;
    u8 needEnv    = 0;
    u8 needSub    = 0;
    u8 i;
    u8 alarmOn = 0;

    /* ====================================================================
     * 【2026-09-19 方案 B · 核心改动】
     * 全工程所有 I2C 访问都在这一个任务（TASK_RENDER）里串行执行。
     *
     * 依据：参考项目 demo30 的 I2C 显示任务里连 RTC 读取都在
     *       （`case 3: RTC_show(); break;`），所有 I2C 天然串行，所以它不需要总线锁。
     * 本工程此前是 TASK_RENDER 刷屏 + TASK_LOGIC 读时钟，两个任务共用一个总线，
     * 靠 I2C_Lock 交替 —— 一旦锁竞争到超时强夺，两个任务就会同时往总线发字节，
     * 字节流错位，屏幕就花屏/黑屏（真机现象：左 1/3 正常、中间黑、右边光点 = 列地址错位）。
     *
     * 现在 TASK_LOGIC / 按键 / 串口协议 都只置下面的标志，由这里统一执行。
     * ==================================================================== */
    if (g_reqClockRefresh)
    {
        g_reqClockRefresh = 0;
        Clock_Refresh();                /* 每秒读一次时间（I2C 读） */

        /* 【2026-09-19 修"数码管慢一秒"】
         * 真机现象：数码管比副屏的秒数**整慢一秒**，但两者是同一时刻跳到下一秒的。
         *
         * 根因：数码管的内容原来由 TASK_LOGIC 的每秒节拍写（Menu_Tick1s 里调 Nixie_SetTime），
         * 而那一刻 g_clock 还是**上一秒**的值 —— 因为 TASK_LOGIC 只登记了
         * g_reqClockRefresh，真正读 PCF8563 是**本任务稍后**才做的。
         * 所以数码管拿到的是旧值，副屏拿到的是新值，正好差一秒。
         *
         * 现在改成：谁读到新时间，谁立刻把三块屏的内容统一起来 ——
         * 数码管、副屏、主屏用的都是同一个刚读到的 g_clock。 */
        Nixie_SetTime(g_clock.hour, g_clock.minute, g_clock.second);
    }
    if (g_reqClockSet)
    {
        g_reqClockSet = 0;
        Clock_SetNow(&g_clock);         /* 校时写芯片（I2C 写） */
        Alarm_ApplyHardwareNow();       /* 时间变了，硬件闹钟跟着重算 */
    }
    if (g_reqClockRecover)
    {
        g_reqClockRecover = 0;
        Clock_TryRecoverNow();          /* 时钟自救 */
    }
    if (g_reqAlarmApply)
    {
        g_reqAlarmApply = 0;
        Alarm_ApplyHardwareNow();       /* 重写硬件闹钟（I2C 写） */
    }
    if (g_reqRtcIrq)
    {
        g_reqRtcIrq = 0;
        Alarm_OnRtcIrq();               /* 清时钟芯片中断标志（I2C 读写） */
    }

    s_dbgPoll++;            /* 诊断：TASK_RENDER 活性 */

    /* 【2026-09-19 删除周期整屏重画】
     * demo30 运行期**没有任何周期性整屏刷新** —— 它只在切页时 Clear 一次，
     * 之后只对"内容变了的那一行"写一次 ShowString（32 字节）。
     * 我原来每 10 秒强制整屏重画（Clear 1024 字节 + 4 行），
     * 那个"清屏与重画之间的窗口"正是"某一行闪乱码"的机会。
     * 所以删掉。整屏重画只在 s_fullRedraw（切页面）时发生。 */

    /* 统计开着的闹钟组数，用于判断环境行要不要重画 */
    for (i = 0; i < ALARM_MAX; i++)
    {
        if (g_alarms[i].enable)
        {
            alarmOn++;
        }
    }

    /* 【2026-09-18 修正 · 关键】限流改到"执行点"。
     *
     * 原来限流放在 Display_RequestFull()（请求点），但 Display_SetPage() 也会
     * 直接置 s_fullRedraw —— 切页面绕过了限流。真机数据：
     *     [DISP] poll=1000 sub=10 full=18
     * 5 秒内做了 18 次整屏重画 = 18 x 350ms = 6.3 秒 I2C 挤进 5 秒窗口，
     * 总线又被打满。放在执行点才真正限得住：不管谁请求，1.2 秒内只做一次。 */
    /* ---- 任务图标：只重画那块 64x48 ---- */
    if (s_iconDirty)
    {
        s_iconDirty = 0;
        sub_draw_icon();
    }

    /* ---- 云台仪表盘：只重画"半圆 + 粗针 + 角度行"那一小块 ---- */
    if (s_gaugeDirty)
    {
        s_gaugeDirty = 0;
        sub_draw_gauge();
    }

    /* ---- 主屏单独的整屏重画请求（只影响主屏的变化，例如挪菜单光标）----
     * 放在限流判断**之前**：主屏是软件 SPI，画一次约 1ms，
     * 既不需要限流，更不能被 I2C 的 800ms 限流挡在后面 ——
     * 那正是"按 KEY3 没反应、再按跳一格"的根因。 */
    if (s_mainDirty)
    {
        s_mainDirty = 0;
        main_repaint();
    }

    if (s_fullRedraw)
    {
        /* 【点 2b 之后放宽】原来 800ms 是为"一次整屏重画约 350ms"的时代设的。
         * 现在按页批量写，一次整屏只要约 5ms，连按 100 次也才半秒，
         * 所以放宽到 100ms：既挡得住程序性洪流，又不拖慢"挪选择 / 切页面"。 */
        if (s_lastFullExecMs != 0 && SysTick_Elapsed(s_lastFullExecMs) < 100U)
        {
            return;             /* 还没到点：保留 s_fullRedraw，下一拍再做 */
        }
        s_lastFullExecMs = SysTick_Get();

        main_repaint();         /* 主屏（内部自己判断 s_mainOn 并同步 s_last*） */

        sub_full_redraw();      /* 内部按页加锁，外部不再整段持锁 */
        s_dbgFull++;

        s_fullRedraw = 0;
        s_envTimer   = 0;

        return;
    }

    /* ---- 判断主屏哪几行需要重画 ---- */
    if (g_clock.hour != s_lastHour || g_clock.minute != s_lastMinute)
    {
        needBig = 1;
    }
    if (g_clock.day != s_lastDay)
    {
        needDate = 1;
    }
    if (g_tempX10 != s_lastTemp || g_humi != s_lastHumi ||
        alarmOn != s_lastAlarmOn || Sensor_HumiValid() != s_lastValid)
    {
        needEnv = 1;
    }

    /* 环境值偶尔抖动，最多每 2 秒重画一次，避免刷屏占满带宽 */
    s_envTimer++;
    if (s_envTimer >= 400)          /* 400 x 5ms = 2s */
    {
        s_envTimer = 0;
        needEnv = 1;
    }

    /* 【2026-09-19 用户要求 · 删除】副屏原来每秒刷"时间行/日期行/VOL·SONG 行"，
     * 现在这套界面整体去掉了（见 sub_full_redraw 的说明），所以这里不再有任何周期性写屏。
     * 副屏只在"内容形态变化"时整块重画：切菜单 / 换任务 / 云台转针。 */

    /* ---- 主屏（SPI）增量刷新 【2026-09-19 修"只有切菜单才刷新"】
     *
     * 真机现象：开机时主屏显示 T0.0C H--%（温湿度还没读出来就画了），
     * 用矩阵键盘校时后主屏的大字时钟也不动 —— **只有按 KEY2/KEY4 切一次菜单才会变**。
     *
     * 根因：needBig / needDate / needEnv 这三个判据一直在算，
     * 但**从来没有被消费**（判据是死代码）—— 主屏只在 s_fullRedraw 时才重画，
     * 而 s_fullRedraw 只在切页面时置位。
     *
     * 现在把消费端接上：和副屏一样，"内容真变了才重画"。
     * 主屏是软件 SPI，整屏约 1.3ms，重画一行很便宜，不需要限流。
     * 只在第 0 级（大字时钟）才做 —— 菜单和其它任务页由切页时的整屏重画负责。 */
    if (s_page == PAGE_HOME && !Menu_IsOpen())
    {
        if (needBig)
        {
            main_draw_bigtime();        /* 16x32 定宽覆盖写，不用先擦 */
        }
        if (needDate)
        {
            main_draw_date();           /* 长度固定，不用先擦 */
        }
        if (needEnv)
        {
            /* 环境行的长度会变（湿度 1 位 -> 2 位时 H7% 变 H57%），
             * 若不管，行尾会留下上一帧的尾巴 —— 和副屏那次"残影"是同一类问题。
             *
             * 解法用和副屏 sub_draw_line 一样的那一招：**在 main_draw_env 里
             * 把字符串补空格到满行 16 个字符**，每次写入正好覆盖整行。
             * 这样就不用"先擦整行再画"—— 那样每 2 秒会闪一下。 */
            main_draw_env();
        }
    }

    s_lastHour    = g_clock.hour;
    s_lastMinute  = g_clock.minute;
    s_lastDay     = g_clock.day;
    s_lastTemp    = g_tempX10;
    s_lastHumi    = g_humi;
    s_lastAlarmOn = alarmOn;
    s_lastValid   = Sensor_HumiValid();

}

/*========================================================================
 *                    TASK_RENDER：5ms 一拍
 *========================================================================*/

void task_render(void) _task_ TASK_RENDER
{
    /* 等一会儿再动屏幕：主时钟、I2C、SPI 的 IO 由 TASK_MAIN 配，
     * 这里多等 100ms 保证配置已完成（v3.1 的 App_RTC.c 也这么处理）。 */
    os_wait2(K_TMO, 20);

    Display_Init();

    /* 【方案 B】时钟初始化放在这里 —— Clock_Init() 内部会读 PCF8563（I2C），
     * 必须和屏幕刷新在同一个任务里，才能保证全工程 I2C 串行、不需要总线锁。 */
    Clock_Init();

    while (1)
    {
        Display_Poll();

        os_wait2(K_TMO, 1);     /* 1 x 5ms */
    }
}
