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
#include "App_Font.h"
#include "I2C_Lock.h"
#include "LED.h"
#include "NixieScan.h"   /* Nixie_SetTime：数码管内容，读时钟后立刻同步 */
#include "Servo.h"       /* Servo_GetAngle：云台页显示当前角度 */
#include "App_Icons.h"   /* Icon_Get：任务清单的图标（整屏 128x64，见 App_Icons.c）*/


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
/*========================================================================
 *          刷新请求标志 —— 只有一个入口 + 一对标志（照 demo30 的模型）
 *
 * 【2026-09-22 整体重构 · 依据参考工程 demo30 的 App/App_OLED.c 第 52 行】
 *
 *   //在I2C屏幕内用来判断是否要刷新屏幕
 *   static u8 global_is_clear_screen = 1;
 *   //更新整个I2C_OLED屏幕内容需要传入参数1刷新，只更新屏幕内一部分内容传入参数0不刷新就行
 *   void APP_I2C_OLED_Refresh(u8 clear_screen) {
 *       if (clear_screen) { global_is_clear_screen = 1; }
 *       os_send_signal(TASK_I2C_OLED);
 *   }
 *
 * 收敛前本工程有 **5 个请求函数 / 5 个标志**，语义还重叠
 * （RequestMain 和 RequestFull 都在重画主屏），调用方很容易用错。
 * 现在按 demo30 收敛成：**一个入口、一个问题**——
 *    "这次是换页(1)，还是只改了个值(0)？"
 *
 *   s_needRedraw = 有刷新请求待处理
 *   s_needClear  = 这次要不要先清屏（**单向上闩**：只置位、从不降级）
 *========================================================================*/
static u8 s_needRedraw = 1;         /* 上电第一次一定画 */
static u8 s_needClear  = 1;         /* 上电第一次一定清 */
static UIPageId_t s_page = PAGE_HOME;
/* 【2026-09-22】原来这里还有一条"主屏单独的重画请求标志"——
 * 按 demo30 的模型收敛之后，主屏和副屏共用同一个刷新请求入口，
 * 这条单独的路已经删掉。请见 Display_Refresh() 的说明。 */



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
/*========================================================================
 *     主屏（SPI）写一行的**统一入口** —— 每次都补满整行
 *
 * 【2026-09-21 修"第二级菜单第三行有时候出现乱码残影"】
 *
 * SPI_OLED_Display_GB2312_string() 是"写到哪算哪"，**不会清掉本行剩下的部分**。
 * 而汉字 16px、ASCII 8px，不同字符串的像素宽度差很多：
 *     "> 1 日期和时间" = 112px        "> 2 番茄钟" = 80px
 * 先在第三行画了宽的、再换成窄的，右边那 32px 就留着**上一帧的笔画碎片**
 * —— 用户看到的就是"第三行有时候出现 #d 这种字符"。
 *
 * 这一条在 I2C 副屏上早就修过（sub_draw_line 补到 16 个字符 = 128px），
 * 但 SPI 这一侧一直只有 main_draw_env 做了补全，菜单行/设置行/日期行都没做。
 *
 * 现在收敛成一个函数：**任何要写一整行的 SPI 文字都走它**，
 * 每次写入都正好覆盖 128px 整行，从根上消掉残影这一类问题。
 *========================================================================*/
/* 主屏"补满整行"的统一行缓冲。
 * 【2026-09-21】放**文件级 static** 而不是函数内局部：
 * 本函数有 10 个调用点，局部数组会被 C51 的覆盖分析按调用路径反复分配，
 * 实测 xdata 从 2724 涨到 3830。放文件级只有这一份 48 字节。
 * 安全性：全程在 TASK_RENDER 一个任务里跑，末尾只调一次 SPI 写屏、不会重入。 */
static char s_spiLine[48];

/* 日期与时间任务（SPI 版面）的临时串缓冲，同样放文件级 */
static char s_dtLine[40];

/* 【2026-09-22】原来这里有个 static char s_rngLine[40]; —— 给测距仪
 * 版面拼 "距离 nn cm" / "RAW nnnn" 用的。用户要求去掉 RAW、
 * 且主屏不再显示距离之后，它没有使用者了，已删除（省 40 字节 xdata）。 */

/*------------------------------------------------------------------------
 *  ※【参数约定 · 必须记住】**y 是"页号"，不是"行号"**
 *
 *  SPI_OLED 的寻址单位是"页"（每页 8 行像素），而汉字字模是 **16x16**
 *  -> 一个汉字占 **2 个页**（page y 和 page y+1）。
 *
 *  所以"一行文字"= 相邻的两个页。四行文字的页号必须是：
 *      第 1 行 -> page 0      第 2 行 -> page 2
 *      第 3 行 -> page 4      第 4 行 -> page 6
 *  **奇数页只能作为某一行的下半部分，绝不能拿来当一行的起点。**
 *
 *  【真机踩过】本项目曾把"行号 0/1/2/3"当成 y 传进来，于是
 *  "第 2 行"（page 1~2）的清空写把 page 1 覆盖掉 ——
 *  而 page 1 正是"第 1 行"的下半部分 -> 屏幕上的字**只剩上半截**。
 *  （对比：副屏的 sub_draw_line() 内部做了 line*2，所以没有这个问题。）
 *------------------------------------------------------------------------*/
static void main_draw_line_fit(u8 y, char *s)
{
    char *buf = s_spiLine;
    u8   n  = 0;
    u8   px = 0;

    /* 逐字符搬内容，同时累计像素宽度：
     * GB2312 首字节 >= 0x81 算汉字（16px），否则算 ASCII（8px）。 */
    while (s[n] != '\0' && n < 40)
    {
        if ((u8)s[n] >= 0x81)
        {
            buf[n] = s[n];
            n++;
            if (s[n] == '\0')
            {
                break;                      /* 半个汉字，直接丢 */
            }
            buf[n] = s[n];
            n++;
            px = (u8)(px + 16);
        }
        else
        {
            buf[n] = s[n];
            n++;
            px = (u8)(px + 8);
        }
    }

    /* 补空格（每个 8px）直到满 128px = 整屏宽 */
    while (px < 128 && n < 46)
    {
        buf[n] = ' ';
        n++;
        px = (u8)(px + 8);
    }

    buf[n] = '\0';

    SPI_OLED_Display_GB2312_string(0, y, (u8 *)buf);
}

/* 【第 7 点】设置页主屏版面的前置声明。
 * main_redraw() 里要用它，而它的定义在文件后面（显示层的安排是
 * "先主屏的大字时钟/菜单，再各任务页版面"），所以这里先声明一下。
 * C51 不允许调用后面才定义的函数。 */
static void main_draw_settings(void);

/* 【测距仪】同理：main_redraw() 在第 500 行左右就调用它，而它的定义在文件后面。
 * **不加这行会报 C231 'redefinition'** ——
 * 根因是 C89 的隐式声明：函数先被调用时，C51 按"未知参数、返回 int、extern"
 * 临时声明一次；后面真正的定义是 `static void`，两者冲突 -> 报"重定义"。
 * （main_draw_settings 一直没事，就是因为它有上面这行前置声明。） */
static void main_draw_range(void);

/* 【掌机模式】两个版面的前置声明（定义在文件后面）。
 * 必须声明 —— C51 是先调用后定义会报 C231 "redefinition"
 * （实际是隐式声明与 static 定义冲突），本项目已踩过。 */
static void main_draw_game_list(void);
static void sub_draw_game_page(void);

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

    /* 【2026-09-21】改走统一入口：日期行只占 112px，剩下的 16px 原来一直没被覆盖，
     * 一旦别的内容（比如任务清单的第三行）用到这一页就互相留尾巴。 */
    main_draw_line_fit(MAIN_DATE_Y, buf);
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

    /* 【2026-09-19】原来在这里手工补到 16 个字符（= 128 列）来消残影。
     * 【2026-09-21】这段逻辑已经收进 main_draw_line_fit()，改走统一入口，
     * 免得"哪个函数补了、哪个没补"再次出现不一致。 */
    main_draw_line_fit(MAIN_ENV_Y, buf);
}

/*========================================================================
 *              主屏（SPI）：任务切换界面   【2026-09-19 UI 改造 S2】
 *
 * 主屏分两级（用户定的模型）：
 *   第 0 级 = 大字时钟（最初始一级）—— 在 main_redraw() 里直接画
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

        /* 格式照 demo30 的菜单行：光标 + 序号 + 名称。
         * 【2026-09-21 用户要求】**序号从 1 开始**（原来从 0 开始）——
         * 只是显示上 +1，内部下标 k 不变，导航逻辑不受影响。 */
        sprintf(buf, "%c %d %s", (row == 0) ? '>' : ' ', (int)(k + 1), Menu_TaskName(k));

        /* 走统一入口，补满 128px —— 这行原来是"第三行出现残影"的主要来源 */
        main_draw_line_fit((u8)(row * 2), buf);
    }
}

static void main_redraw(u8 doClear)
{
    /* ==============================================================
     * 【2026-09-22 关键修复】"7 游戏"的游玩/结算态必须**第一个**判断并返回，
     * 绝不能等下面那句 SPI_OLED_Clear() 之后再判。
     *
     * 原来就是那个顺序：清屏在前、判断在后，而判断命中时直接 return。
     * 于是游戏期间**任何**一次 Display_Refresh(1)（切页、闹钟响、
     * 每秒来的刷新请求…）都会"先把屏清黑、再什么都不画"。
     * 真机表现就是玩家说的："结算画面极快就没了"、
     * 而且因为没人重画，屏幕要一直黑到结算结束才恢复。
     *
     * 游戏画面只归游戏自己的 Draw() 管，显示层一个字都不能碰 ——
     * 所以这里在清屏**之前**就返回。
     * ============================================================== */
    if (s_page == PAGE_GAME_HALL && Menu_GameIsPlaying())
    {
        return;
    }

    /* 【2026-09-21】拆出 doClear：切页面必须清屏（擦掉上一页残留），
     * 页内移动一定不能清屏（一清就闪）。见 Display_Refresh 的说明。 */
    if (doClear)
    {
        SPI_OLED_Clear();
    }

    /* 【2026-09-21 用户要求】「日期和时间」任务：SPI 把两个选项都显示出来，
     * 当前项前面加 '>'（和别的菜单同款"以大于号对齐"）。
     *
     * 【2026-09-22 修"字只显示上半部分"】
     * 这里的参数是**页号**（见 main_draw_line_fit 的约定），不是行号。
     * 我上一版传了 0/1/2/3，于是 page 1 被"第 2 行"的清空写覆盖，
     * 而 page 1 正是第 1 行的下半部分 -> 字只剩上半截。
     * 现在改成 **0 / 2 / 4 / 6**（四行各占两个页，正好铺满 8 页）。 */
    if (Menu_DateIsActive())
    {
        if (Menu_DateState() == 0)
        {
            /* 选项态：两个选项分别放在第 1、3 行，中间留一行便于分辨 */
            sprintf(s_dtLine, "%c修改年月日", (Menu_DateChoice() == 0) ? '>' : ' ');
            main_draw_line_fit(0, s_dtLine);        /* 第 1 行 -> page 0~1 */

            main_draw_line_fit(2, (char *)"");      /* 第 2 行 -> page 2~3（空行也要写）*/

            sprintf(s_dtLine, "%c修改时分", (Menu_DateChoice() == 1) ? '>' : ' ');
            main_draw_line_fit(4, s_dtLine);        /* 第 3 行 -> page 4~5 */

            main_draw_line_fit(6, (char *)"");      /* 第 4 行 -> page 6~7 */
        }
        else
        {
            /* 输入态：显示在输哪一项 + 已输位数 */
            sprintf(s_dtLine, "输入%s %d/%d",
                    (Menu_DateChoice() == 0) ? "年月日" : "时分",
                    (int)Menu_DateCnt(),
                    (int)((Menu_DateChoice() == 0) ? 8 : 4));
            main_draw_line_fit(0, s_dtLine);        /* page 0~1 */
            main_draw_line_fit(2, (char *)"");      /* page 2~3 */

            main_draw_line_fit(4, (char *)"K4 删除最后一位");   /* page 4~5 */
            main_draw_line_fit(6, (char *)"");      /* page 6~7 */
        }
        return;
    }

    /* 【测距仪】主屏只显示任务名 + 一行提示（距离在副屏）。
     * 自带版面、4 行全写满 —— 见 main_draw_range 的说明。 */
    if (s_page == PAGE_RANGE)
    {
        main_draw_range();
        return;
    }

    /* 【第 7 点】设置任务：主屏显示**全部设置项**（其他页面都是"当前任务一行"）*/
    if (s_page == PAGE_SETTINGS)
    {
        main_draw_settings();
        return;
    }
    /* 【掌机模式】"7 游戏"
     *   游玩/结算态：**直接 return，一个字都不画** ——
     *     游戏画面由游戏自己的 Draw() 负责（它内部会 GClear + Refresh）。
     *     显示层一旦插手，游戏就会被擦成菜单。
     *   列表态：画 3 个游戏名。
     */
    if (s_page == PAGE_GAME_HALL)
    {
        /* 走到这里 s_gameState 一定是 0（列表态）——
         * 游玩/结算态已经在函数开头 return 了。 */
        main_draw_game_list();
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

/*------------------------------------------------------------------------
 *  画副屏一行 16 像素高的 ASCII。line 取 0..3，对应页 0/2/4/6。
 *
 *  【为什么必须固定宽度】
 *    本函数的刷新粒度是"只重画变化的那一行"，一次只写这一行、不做整行清屏。
 *    如果新字符串比上一帧短，旧内容的尾巴会留在屏幕上：
 *      真机现象 VOL 从 "VOL 10/10 SONG 1"(15字) 降到 "VOL 0/10 SONG 1"(14字)，
 *      显示成 "VOL 0/10 SONG 11" —— 多出来那个 1 就是上一帧的残留。
 *    所以统一补到 16 个字符 = 128 列满宽，超长则截断；
 *    每行写入都正好覆盖整行，不会留尾巴。
 *
 *  【为什么是逐字节写、不是批量写】
 *    2026-09-18 试过"整页一次事务批量写"，实机副屏整屏乱码、按键无响应，已回退。
 *    现在的批量写只用在两处**已验证**的地方：sub_blit()（图标）和 gauge_flush()（仪表盘），
 *    它们都是"整页全量覆盖"；文字行长度可变，走逐字节这条稳的路。
 *------------------------------------------------------------------------*/
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
    case PAGE_GAME_HALL:   return "GAME";
    /* 【2026-09-22 清理】原来还有 5 个游戏子页 ID
     * （PAGE_GAME_SNAKE / BRICK / PLANE / DAILY / OVER）。
     * 掌机模式改成"「7 游戏」页内两状态"之后（见 App_Menu.c 的 Game_Poll），
     * 这 5 个页面 ID 全工程再无引用 —— 游戏画什么由游戏自己的 Draw() 决定，
     * 不再需要按页面 ID 分派。所以连枚举带这里的 case 一起删了。 */
    default:               return "?";
    }
}

/*========================================================================
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
 *   -> 整屏只要 **8 次数据事务 + 8 次定位命令 = 16 次 ≈ 5ms，快约 70 倍。**
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
 *     page 0~1   GIMBAL              任务名（由 sub_redraw 统一画）
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
#define GAUGE_CY      46    /* 圆心 y（区域局部坐标）：46-46=0 -> 弧顶正好落在第 0 行 */
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
 * -> **靠左 = 当前选中项**；编辑态的字段用同一套规则。
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
/* 把一行写成**全空格**（16 个字符 = 128px）。
 * 【2026-09-21 为什么需要它】副屏现在有"不清屏只重画行"的路径，
 * 于是**每一页都必须把 4 行全写满**，否则没被写到的那些行会留着上一屏的内容
 * —— 用户报的"进入番茄钟修改时间，下半屏还显示上一界面的内容"就是这个。
 * 用这个函数把空行也显式写掉，就保证了"4 行全自覆盖"。 */
static void sub_blank_line(u8 line)
{
    sub_draw_line(line, "                ");     /* 16 个空格 */
}

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

/*------------------------------------------------------------------------
 * 副屏写一行"**带选中标记**"的列表项  —— 2026-09-21 用户要求
 *
 * 用户原话：「在第三级菜单中，在 I2C 屏幕中，选择当前内容时，和 SPI 屏幕一样
 *   加一个大于号 '>'，不要全部对齐在右边了，**全部以大于号对齐**」
 *
 * 所以副屏的列表不再用"靠左 = 选中"那套（点 4 的老设计），
 * 改成和 SPI 完全一致：**全部左对齐，选中行第 0 列画 '>'，其余行第 0 列留空格**。
 * 不管选没选中，正文都从第 1 列开始 -> "全部以大于号对齐"。
 *
 * 同时保持 sub_draw_line 的"补满 16 字符"（= 128px）特性：
 * 短串不补会在行尾留下上一帧的尾巴。
 *------------------------------------------------------------------------*/
static void sub_draw_row_sel(u8 line, const char *s, u8 selected)
{
    char buf[18];
    u8   n = (u8)strlen(s);
    u8   i;

    if (n > 15) { n = 15; }             /* 留出第 0 列给标记 */

    for (i = 0; i < 16; i++) { buf[i] = ' '; }
    buf[16] = '\0';

    buf[0] = selected ? '>' : ' ';

    for (i = 0; i < n; i++) { buf[1 + i] = s[i]; }

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
                    /* 【2026-09-21 用户要求】开启也显示出来，和关闭的 off 一样 */
                    g_alarms[k].enable ? " on" : " off");

            sub_draw_row_sel((u8)(i + 1), buf, (u8)(k == Menu_AlarmSel()));
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
        sub_draw_row_sel((u8)(i + 1), buf, (u8)(k == Menu_AlarmEditField()));
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
        sub_draw_row(0, buf, 1);            /* 标题行：不带标记 */

        if (sel == 0)      { sprintf(buf, "%d min", (int)g_settings.pomodoro_work); }
        else if (sel == 1) { sprintf(buf, "%d min", (int)g_settings.pomodoro_rest); }
        else               { sprintf(buf, "%s", s_pomoRunStateStr()); }
        sub_draw_row_sel(1, buf, 1);        /* 值行：带 > 标记（和别的三级菜单一致）*/

        /* 【2026-09-21 修用户报的"进去修改时间，下半屏还显示上一界面的内容"】
         * 编辑态原来只写第 0、1 行，下面两行没被覆盖 —— 而页内切换走的是
         * "不清屏只重画行"的路径，于是下半屏留着列表态的内容。
         * 现在把 4 行全写满。 */
        sub_blank_line(2);
        sub_blank_line(3);
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
        sub_draw_row_sel((u8)(i + 1), buf, (u8)(i == sel));
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
    u8   cur;
    u8   maxCnt;

    /* 【2026-09-21】加 '>' 标记 + 全部左对齐（用户要求与 SPI 一致）。
     * 正在输的那一项前面画 '>'：前 4 位（时/分前 2 位）是第 0 行，
     * 之后轮到第 1 行。 */
    if (Menu_DateChoice() == 0)
    {
        cur    = (u8)((Menu_DateCnt() < 4) ? 0 : 1);
        maxCnt = 8;

        sprintf(buf, "YEAR %04u", (unsigned)Menu_DateY());
        sub_draw_row_sel(0, buf, (u8)(cur == 0));

        sprintf(buf, "DATE %04u", (unsigned)Menu_DateMD());
        sub_draw_row_sel(1, buf, (u8)(cur == 1));

        sprintf(buf, "NOW  %04d-%02d-%02d",
                (int)g_clock.year, (int)g_clock.month, (int)g_clock.day);
        sub_draw_row_sel(2, buf, 0);
    }
    else
    {
        cur    = (u8)((Menu_DateCnt() < 2) ? 0 : 1);
        maxCnt = 4;

        sprintf(buf, "HOUR %02u", (unsigned)Menu_DateH());
        sub_draw_row_sel(0, buf, (u8)(cur == 0));

        sprintf(buf, "MIN  %02u", (unsigned)Menu_DateM());
        sub_draw_row_sel(1, buf, (u8)(cur == 1));

        sprintf(buf, "NOW  %02d:%02d:%02d",
                (int)g_clock.hour, (int)g_clock.minute, (int)g_clock.second);
        sub_draw_row_sel(2, buf, 0);
    }

    /* 第 4 行：进度 / 非法提示。
     * 【2026-09-21 用户报】"输入错误的时间这个数字就能一直输入，DATE 的值也不断增加，
     * 最下面一行已经显示 30/8" —— 位数溢出。App_Menu 那边已经加了满位拒绝，
     * 这里同时把"校验没过"这个事实显示出来，用户知道要先按 K4 删。 */
    if (Menu_DateErr())
    {
        sub_draw_row_sel(3, "INVALID K4 DEL", 0);
    }
    else
    {
        sprintf(buf, "K4 DEL  %d/%d", (int)Menu_DateCnt(), (int)maxCnt);
        sub_draw_row_sel(3, buf, 0);
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
    /* 【2026-09-21 用户要求】放曲子叫 Buzzer，震动叫 Motor */
    case 2:  sprintf(s, "%s",   g_settings.alert_mode == ALERT_RING ? "Buzzer" : "Motor"); break;
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
     * 7 项、4 行窗口 -> 回绕最多减两次。 */
    for (row = 0; row < 4; row++)
    {
        k = (u8)(sel + row);
        while (k >= SET_ITEM_COUNT)
        {
            k = (u8)(k - SET_ITEM_COUNT);
        }

        settings_value_str(k, val);
        /* 【2026-09-21】序号从 1 开始（用户要求所有菜单都以 1 开头） */
        sprintf(buf, "%c %d %s %s", (row == 0) ? '>' : ' ', (int)(k + 1),
                settings_item_cn(k), val);

        main_draw_line_fit((u8)(row * 2), buf);
    }
}

/* 副屏：只显示"当前这一项"的功能内容 */
/*========================================================================
 *                  测距仪版面（任务「4 测距仪」）
 *
 *  【2026-09-22 用户要求 · 本轮改动】**距离只在副屏（I2C）显示，
 *  主屏（SPI）不显示距离**；同时**去掉 RAW**（原始计数）那一行。
 *  改前的分工是"主屏也显示距离 + 第 4 行显示 RAW"，两屏信息重复。
 *
 *  主屏（SPI，4 行，页号 0/2/4/6）：        副屏（I2C，4 行）：
 *      测距仪                                  DISTANCE
 *      (空行)                                  123 cm
 *      距离见副屏                              [####......]
 *      (空行)                                  RANGE 2-400cm
 *
 *  两屏都写满 4 行 —— 这是"不清屏只重画行"的前提
 *  （测距值每 150ms 变一次，走的就是那种刷新路径）。
 *  主屏这 4 行是**固定内容**（不含任何随测量变化的量），
 *  所以实际每 150ms 被重画的只有副屏的第 2、3 行。
 *========================================================================*/

/* 主屏：4 行全写满，但**不含距离值**（用户要求距离只出现在副屏）。
 * main_draw_line_fit 会把每行补满到整屏宽，所以换页时不会留下残字。 */
static void main_draw_range(void)
{
    main_draw_line_fit(0, (char *)"测距仪");
    main_draw_line_fit(2, (char *)"");
    main_draw_line_fit(4, (char *)"距离见副屏");
    main_draw_line_fit(6, (char *)"");
}

/* 副屏：距离数值 + 一条 10 格的进度条 + 量程说明。
 * 【2026-09-22】第 4 行原来是 RAW（原始计数），按用户要求去掉，
 * 改放量程；标题行也从 "RANGE 2-400" 改成 "DISTANCE" ——
 * 这一屏现在的主题就是"距离"，量程挪到第 4 行，两处不再重复。 */
static void sub_draw_range_page(void)
{
    char buf[24];
    char bars[12];
    u16  cm = Menu_RangeCm();
    u8   n;
    u8   i;

    sub_draw_row_sel(0, "DISTANCE", 0);

    if (cm == 0)
    {
        sub_draw_row_sel(1, "--- cm", 0);
    }
    else
    {
        sprintf(buf, "%u cm", (unsigned)cm);
        sub_draw_row_sel(1, buf, 0);
    }

    /* 进度条：把 2~400cm 映射到 10 格。
     * 用整数算，避免浮点（本工程硬约束：浮点库 = 0）。 */
    if (cm <= 2)
    {
        n = 0;
    }
    else if (cm >= 400)
    {
        n = 10;
    }
    else
    {
        n = (u8)(((u16)(cm - 2U) * 10U) / 398U);
    }

    for (i = 0; i < 10; i++)
    {
        bars[i] = (u8)((i < n) ? '#' : '.');
    }
    bars[10] = '\0';

    sprintf(buf, "[%s]", bars);
    sub_draw_row_sel(2, buf, 0);

    /* 第 4 行：量程。原来这里是 RAW（原始计数），已按用户要求去掉 ——
     * 去掉后它也不再需要标定用途，所以不显示在屏上。 */
    sub_draw_row_sel(3, "RANGE 2-400cm", 0);
}

/*========================================================================
 *          掌机模式（"7 游戏"）的两个版面
 *
 *  列表态（选游戏）                      I2C 副屏
 *      SPI 主屏                            GAME  PICK ONE
 *      > 1 贪吃蛇                          > SNAKE
 *        2 打砖块                            BRICK
 *        3 飞机大战                          PLANE
 *      K2 开始  K4 返回
 *
 *  游玩态：
 *    · SPI 主屏**完全由游戏自己画**（游戏的 Draw() 里调
 *      SPI_OLED_GClear + DrawPoint + Refresh）——
 *      main_redraw() 必须直接 return，一旦插手就把游戏画面擦成菜单。
 *    · I2C 副屏**关掉**：sub_redraw() 的 hasContent 里刻意不含
 *      "游戏 + 游玩态"，于是走"无内容"分支发 DisplayOff（0xAE）。
 *      这就是用户要的"游玩时 I2C 屏幕直接关闭"。
 *========================================================================*/

static char *game_name_cn(u8 i)
{
    switch (i)
    {
    case 0:  return "贪吃蛇";
    case 1:  return "打砖块";
    default: return "飞机大战";
    }
}

static char *game_name_en(u8 i)
{
    switch (i)
    {
    case 0:  return "SNAKE";
    case 1:  return "BRICK";
    default: return "PLANE";
    }
}

/* 主屏：列出 3 个游戏（光标行带大于号），第 4 行给按键提示。
 * 每行都走 main_draw_line_fit()（补满 128px），所以页内移动不清屏也不留残影。 */
static void main_draw_game_list(void)
{
    char buf[24];
    u8   i;

    for (i = 0; i < (u8)GAME_COUNT; i++)
    {
        sprintf(buf, "%c %d %s", (i == Menu_GameSel()) ? '>' : ' ',
                (int)(i + 1), game_name_cn(i));
        main_draw_line_fit((u8)(i * 2), buf);
    }

    main_draw_line_fit(6, (char *)"K2 开始  K4 返回");
}

/* 副屏：游戏名列表。只在"选游戏"这一屏显示 —— 进游戏后副屏被关掉。 */
static void sub_draw_game_page(void)
{
    u8 i;

    sub_draw_row_sel(0, "GAME  PICK ONE", 0);

    for (i = 0; i < (u8)GAME_COUNT; i++)
    {
        sub_draw_row_sel((u8)(i + 1U), game_name_en(i), (u8)(i == Menu_GameSel()));
    }
}
static void sub_draw_settings_page(void)
{
    char buf[24];
    u8   sel = Menu_SetSel();

    sprintf(buf, "%s", settings_item_en(sel));
    sub_draw_row(0, buf, 1);

    settings_value_str(sel, buf);
    sub_draw_row(1, buf, 1);

    /* 操作提示。
     * 【2026-09-21 用户要求】"所有菜单去掉最后一行 'K2 OK'" ——
     * 原来这里第 3 行写的是 "K2 OK  K4 SAVE"，现在整行不再写（留空），
     * 同时 App_Menu 里那个"按 KEY2 确认 + 100ms 反馈"的功能也一并删掉了。
     * 音量和震动强度这两项**旋钮直接能调**，提示里把旋钮写在最前面。 */
    if (sel == 0 || sel == 6)
    {
        sub_draw_row_sel(2, Menu_SetIsEditing() ? "KNOB K1- K3+" : "K2 EDIT", 0);
    }
    else
    {
        sub_draw_row_sel(2, Menu_SetIsEditing() ? "K1-  K3+" : "K2 EDIT", 0);
    }

    /* 【2026-09-21】第 4 行不再写"K2 OK"，但**必须显式写空**
     * —— 否则页内移动（不清屏路径）会让这一行留着上一屏的内容。 */
    sub_blank_line(3);
}

/* 副屏面板当前是否点亮 —— 用来避免每次重画都发一遍 0xAF/0xAE */
static u8 s_subOn = 0;

static void sub_redraw(u8 doClear)
{
    u8 hasContent;

    hasContent = (u8)(Menu_DateIsActive()
                   || Menu_IsOpen()
                   || s_page == PAGE_SERVO
                   || s_page == PAGE_ALARM_LIST
                   || s_page == PAGE_POMODORO
                   || s_page == PAGE_SETTINGS
                   || s_page == PAGE_RANGE
                   /* 【掌机模式】只有"选游戏"那一屏副屏才亮；
                    * 进了游戏（Menu_GameIsPlaying() 为真）就判为无内容
                    * -> 走下面的"无内容"分支发 DisplayOff(0xAE)。
                    * 用户要求："I2C 屏幕此时直接关闭"。 */
                   || (s_page == PAGE_GAME_HALL && !Menu_GameIsPlaying()));

    /* ---- 情况 A：本状态没有内容（主界面等）→ 直接把面板关掉 ---- */
    if (!hasContent)
    {
        if (s_subOn)
        {
            I2C_Lock();
            I2C_OLED_DisplayOff();
            I2C_Unlock();
            s_subOn = 0;
        }
        return;
    }

    /* ---- 情况 B：切页面（doClear=1）----
     * 【2026-09-21 修用户报的"I2C 刷新整个屏幕时偶尔刷新不完整、下边有残留"】
     *
     * 顺序是 **熄面板 → 清屏 → 画内容 → 再点亮**。
     * 面板黑着的时候把该清的清掉、该画的画完，用户看到的就是
     * "整屏瞬间完整出现"，看不到"清了还没画完"的中间过程。
     *
     * 之所以现在敢这么做：I2C_OLED_Clear() 已改成按页批量写（约 2ms，
     * 原来一字节一次事务要 70~250ms —— 那么长的黑屏是不能接受的）。 */
    if (doClear)
    {
        I2C_Lock();
        I2C_OLED_DisplayOff();
        I2C_OLED_Clear();
        I2C_Unlock();
        s_subOn = 0;
    }

    /* ---- 画本页内容（这时面板可能还黑着）----
     * 注意：这里**不能用 return 提前退出**，否则末尾的"点亮"就漏了。
     * 另外每一页都必须把 4 行全写满（见 sub_blank_line 的说明）。 */
    if (Menu_DateIsActive())
    {
        if (Menu_DateState() == 0) { sub_draw_icon(); }
        else                       { sub_draw_date_entry(); }
    }
    else if (Menu_IsOpen())
    {
        sub_draw_icon();                /* 任务图标：整屏 128x64，天然自覆盖 */
    }
    else if (s_page == PAGE_SERVO)
    {
        sub_draw_gauge();
    }
    else if (s_page == PAGE_ALARM_LIST)
    {
        sub_draw_alarm_page();
    }
    else if (s_page == PAGE_POMODORO)
    {
        sub_draw_pomo_page();
    }
    else if (s_page == PAGE_SETTINGS)
    {
        sub_draw_settings_page();
    }
    else if (s_page == PAGE_RANGE)
    {
        sub_draw_range_page();
    }
    else if (s_page == PAGE_GAME_HALL)
    {
        /* 【掌机模式】走到这里说明是"选游戏"那一屏
         * （游玩态在 hasContent 阶段就 return 了，根本到不了这里）*/
        sub_draw_game_page();
    }

    /* ---- 画完了再点亮 ---- */
    if (!s_subOn)
    {
        I2C_Lock();
        I2C_OLED_DisplayOn();
        I2C_Unlock();
        s_subOn = 1;
    }
}

/*========================================================================
 *                      唯一的刷新请求入口
 *
 * 照 demo30 的 APP_I2C_OLED_Refresh(clear_screen)：
 * 调用方只需要回答一个问题 —— **这次是"换页"，还是"只改了个值"？**
 *   clearScreen = 1 → 换页：先清屏再画（擦掉上一页的残留）
 *   clearScreen = 0 → 值变了 / 页内移动：只重画本页内容，不清屏（不会闪）
 *
 * 两块屏共用这一个参数，因为它表达的是"**这次请求的性质**"，和是哪块屏无关。
 * 但两块屏的具体策略仍然是分开实现的（代价不同）：
 *   · 主屏 SPI：便宜（整屏约 1ms）→ 清不清屏主要影响观感
 *   · 副屏 I2C：贵（清屏 + 4 行约 20ms）→ 必须明确说要不要清
 *
 * 注意那个 **单向上闩**：只在传 1 时置位，从不把 1 降回 0 ——
 * demo30 的写法就是 `if (clear_screen) { global_is_clear_screen = 1; }`。
 * 这样"换页"这个更重的要求一旦提出，**不会被随后跟来的"值变了"降级**。
 *========================================================================*/
void Display_Refresh(u8 clearScreen)
{
    if (clearScreen)
    {
        s_needClear = 1;        /* 单向：只置 1，不降回 0 */
    }

    s_needRedraw = 1;
}

void Display_SetPage(UIPageId_t page)
{
    if (s_page != page)
    {
        s_page = page;
        Display_Refresh(1);         /* 换页 = 要清屏 */
    }
}

void Display_MainPower(u8 on)
{
    if (on)
    {
        SPI_OLED_DisPlay_On();
        Display_Refresh(1);
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


    SPI_OLED_ColorTurn(0);
    SPI_OLED_DisplayTurn(0);
    SPI_OLED_Clear();

    /* 主屏初始化完成立刻写一行测试文字：
     * 这是"诊断手段"，输出不依赖任何任务刷新（规范第四节）——
     * 只要屏亮且能看到这行字，就说明 SPI 线序和屏本身是好的。 */
    SPI_OLED_Display_GB2312_string(0, 0, (u8 *)"ALARM BOOT OK");

    /* ---- 副屏 ---- */
    I2C_Lock();
    I2C_OLED_Init();
    I2C_OLED_ColorTurn(0);
    I2C_OLED_DisplayTurn(0);
    I2C_Unlock();

    /* 不在这里单独 Clear/写字：紧接着的第一次 Display_Poll 会走整屏重画，
     * 由 sub_redraw(1) 把 4 行完整刷一遍（每一行都会清掉自己在的 2 页）。 */

    Display_Refresh(1);
}

/* 主屏重画：doClear 决定要不要先清屏（含义同 Display_Refresh 的参数）。
 * "数据快照"（s_last*）由调用方（Display_Poll）统一同步，这里不管。
 *
 * 【历史 · 为什么现在不怕"跳项"了】
 * 曾经主屏另有一条"只重画主屏"的请求路径，起因是真机 bug：
 * 进任务清单后按 KEY3 偶尔"没反应、再按跳一格"——
 * 因为挪光标请求的是"整屏重画"，而整屏重画的执行点带 100ms 限流（保护 I2C），
 * 主屏（软件 SPI 约 1ms）被连累一起挡住。
 * 收敛成统一入口之后，**限流只对"要清屏"的那种生效**（见 Display_Poll），
 * 页内移动（clearScreen=0）完全不受限流约束 —— 所以"跳项"不会回来。 */
static void main_repaint(u8 doClear)
{
    if (!s_mainOn)
    {
        return;
    }

    main_redraw(doClear);
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


    /* 【2026-09-19 删除周期整屏重画】
     * demo30 运行期**没有任何周期性整屏刷新** —— 它只在切页时 Clear 一次，
     * 之后只对"内容变了的那一行"写一次 ShowString（32 字节）。
     * 我原来每 10 秒强制整屏重画（Clear 1024 字节 + 4 行），
     * 那个"清屏与重画之间的窗口"正是"某一行闪乱码"的机会。
     * 所以删掉。整屏重画只在"换页"那种请求（Display_Refresh(1)）时发生。 */

    /* 统计开着的闹钟组数，用于判断环境行要不要重画 */
    for (i = 0; i < ALARM_MAX; i++)
    {
        if (g_alarms[i].enable)
        {
            alarmOn++;
        }
    }

    /* ==================================================================
     *       刷新请求：**只有一个入口**（照 demo30 的模型收敛）
     * ================================================================== */
    if (s_needRedraw)
    {
        u8 clearNow = s_needClear;

        /* 执行点限流：**只对"要清屏"的那种**限流（它最贵）。
         * 这里是"return 但保留 s_needRedraw" —— **延后执行，不丢请求**。
         * （请求点绝不能限流：那会把请求直接丢掉，见 §三.23 的教训。） */
        if (clearNow && s_lastFullExecMs != 0
            && SysTick_Elapsed(s_lastFullExecMs) < 100U)
        {
            return;                 /* 还没到点：保留标志，下一拍再做 */
        }

        s_needRedraw = 0;
        s_needClear  = 0;

        if (clearNow)
        {
            s_lastFullExecMs = SysTick_Get();
            s_envTimer       = 0;
        }

        /* 两块屏各画一遍。清不清屏是**同一个参数** ——
         * 因为它表达的是"这次请求的性质"，和是哪块屏无关。 */
        main_repaint(clearNow);
        sub_redraw(clearNow);

        /* 重画之后同步"数据快照"，免得紧接着又触发一次增量刷新 */
        s_lastHour    = g_clock.hour;
        s_lastMinute  = g_clock.minute;
        s_lastDay     = g_clock.day;
        s_lastTemp    = g_tempX10;
        s_lastHumi    = g_humi;
        s_lastValid   = Sensor_HumiValid();
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
     * 现在这套界面整体去掉了（副屏只在内容形态变化时整块重画），所以这里不再有任何周期性写屏。
     * 副屏只在"内容形态变化"时整块重画：切菜单 / 换任务 / 云台转针。 */

    /* ---- 主屏（SPI）增量刷新 【2026-09-19 修"只有切菜单才刷新"】
     *
     * 真机现象：开机时主屏显示 T0.0C H--%（温湿度还没读出来就画了），
     * 用矩阵键盘校时后主屏的大字时钟也不动 —— **只有按 KEY2/KEY4 切一次菜单才会变**。
     *
     * 根因：needBig / needDate / needEnv 这三个判据一直在算，
     * 但**从来没有被消费**（判据是死代码）—— 主屏只在"换页"（Display_Refresh(1)）
     * 时才重画，而那种请求只在切页面时发出。
     *
     * 现在把消费端接上：和副屏一样，"内容真变了才重画"。
     * 主屏是软件 SPI，整屏约 1.3ms，重画一行很便宜，不需要限流。
     * 只在第 0 级（大字时钟）才做 —— 菜单和其它任务页由切页时的整屏重画负责。 */
    /* 【2026-09-21 用户报第 1 条 b · 关键修复】
     *   "在'修改年月日'这个三级菜单的地方，主菜单的时间或者温湿度只要变化了
     *    就会覆盖当前的第三级菜单"
     *
     * 根因：原来这里的守卫只有 `s_page == PAGE_HOME && !Menu_IsOpen()`。
     * 而「日期和时间」任务是**不摞页面**的（menu_enter 里进来就把 s_menuOpen 清 0、
     * 只置 s_dtActive），所以那个任务里恰好满足
     *     s_page == PAGE_HOME 且 Menu_IsOpen() == 0
     * -> 大字时钟/日期/温湿度的**周期性增量刷新一直在跑**，
     *   每隔 2 秒（needEnv 的强制节拍）以及每次分钟变化就把 dt 的版面糊掉。
     *
     * 修法：把守卫收紧成"**真的停在第 0 级大字时钟**"三个条件同时成立。
     * 三个条件缺一不可：页面是主页、菜单没打开、日期时间任务也没在跑。 */
    if (s_page == PAGE_HOME && !Menu_IsOpen() && !Menu_DateIsActive())
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
