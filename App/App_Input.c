/*
 * App_Input.c - 输入层实现（事件队列 + TASK_INPUT）
 *
 * 关键点：
 *   1) 独立按键的"短按"在抬手时才上报（Driver/Keys.c 决定），长按在按满 2 秒时上报；
 *      这样短按和长按不会互相污染。
 *   2) 矩阵键盘只在按下时产生事件（抬起不上报），够用且省队列。
 *   3) 旋钮防跳：连读 8 次取平均，变化小于 2 档就不理它（《04》E 角色第 4 条）。
 *   4) 所有消抖/计时都用 g_sysTick 算差值，不用 delay 硬等。
 */
#include "App_Input.h"

#include "Keys.h"
#include "MatrixKey.h"
#include "ADC.h"

/*========================================================================
 *                              事件队列
 *========================================================================*/

static Event_t xdata s_queue[EVT_QUEUE_SIZE];
static u8 s_head = 0;       /* 取的位置 */
static u8 s_tail = 0;       /* 放的位置 */
static u8 s_count = 0;

static void evt_push(u8 type, u8 param)
{
    if (s_count >= EVT_QUEUE_SIZE)
    {
        /* 队列满：丢最旧的一条，保证最新的操作不丢（比丢新的更友好） */
        s_head++;
        if (s_head >= EVT_QUEUE_SIZE)
        {
            s_head = 0;
        }
        s_count--;
    }

    s_queue[s_tail].type  = type;
    s_queue[s_tail].param = param;

    s_tail++;
    if (s_tail >= EVT_QUEUE_SIZE)
    {
        s_tail = 0;
    }
    s_count++;
}

u8 Input_GetEvent(Event_t *evt)
{
    if (s_count == 0)
    {
        return 0;
    }

    evt->type  = s_queue[s_head].type;
    evt->param = s_queue[s_head].param;

    s_head++;
    if (s_head >= EVT_QUEUE_SIZE)
    {
        s_head = 0;
    }
    s_count--;

    return 1;
}

u8 Input_PendingCount(void)
{
    return s_count;
}

void Input_Flush(void)
{
    s_head  = 0;
    s_tail  = 0;
    s_count = 0;
}

/*========================================================================
 *                    Driver 层回调（由按键扫描调用）
 *========================================================================*/

/* 按下：不产生事件，短按统一在抬手时上报 */
void Keys_on_keydown(u8 key)
{
    key = key;      /* Keil C51 常见做法：避免 unreferenced 告警 */
}

/* 短按：抬手且没报过长按 */
void Keys_on_keyup(u8 key)
{
    if (key < 4)
    {
        evt_push((u8)(EVT_KEY1 + key), 0);
    }
}

/* 长按：按满 2 秒，只报一次 */
void Keys_on_longpress(u8 key)
{
    if (key < 4)
    {
        evt_push((u8)(EVT_KEY1_LONG + key), 0);
    }
}

/* 矩阵键盘：只在按下时上报，param = row * 4 + col */
void MK_on_keydown(u8 row, u8 col)
{
    evt_push(EVT_MATRIX, (u8)(row * 4 + col));
}

void MK_on_keyup(u8 row, u8 col)
{
    row = row;
    col = col;
}

/*========================================================================
 *                              旋钮（电位器）
 *
 * 【真机修正】原来的映射是"raw 0..4095 -> 档位 0..10"，直接假设电位器的 ADC
 * 读数能覆盖满量程。实机回报：旋到顶只到 6~7 档、旋到底只到 0~1 档，
 * 说明这只电位器的实际 ADC 跨度远小于 4095。
 *
 * 改法：不再假设量程，改为**运行期自学习**：
 *   记录观测到的最小 / 最大原始值，把 [min, max] 线性映射到 0..10 档。
 *   用户把旋钮来回拧到底一次，量程就学好了，之后到底稳定显示 0、到顶稳定显示 10。
 *   这是电位器标定的通用做法，不依赖任何硬件常数（Vref、分压比都不用知道）。
 *
 * 为什么不能靠"把公式调一调"解决：Vref、分压电阻、电位器阻值都是硬件事实，
 * 凭空改常数只是把错误挪个位置。自学习直接绕开这些未知量。
 *========================================================================*/

#define POT_AVG_TIMES   8           /* 连读 8 次取平均 */
/* 【第 7 点】电位器输出改成 0..100（百分比），步进 1% */
#define POT_LEVELS      101         /* 0..100 共 101 档 */

/* 【2026-09-18 修正 · 依据用户参考代码】
 * 电位器的 ADC 满量程不是 4095，而是约 2740。
 * 依据：用户自己的 demo30（App/App_Motor.c）里是这么写的：
 *         adc = Get_ADCResult(ADC_CH13);          // 读P0.5上的电位器电压
 *         pwm_duty.PWM6_Duty = PERIOD * adc / 2740;
 *       他拿 2740 当满量程，说明实测旋到顶就在 2740 附近。
 *
 * 原来我写的是"运行期自学习量程"，问题是要等用户把旋钮来回拧过才认量程，
 * 所以刚开机旋到底也不起作用（真机反馈：开机总是 VOL 6/10）。
 * 现在改成固定量程 —— 和参考代码一致，开机即生效，行为可预期。 */
#define POT_ADC_FULL    2740U
#define POT_RAW_STEP    60U         /* 原始值变化超过这个数才认一次拧动（防抖） */

static u16 s_potMark  = 0;
static u8  s_potLevel = 50;
static u8  s_potReady = 0;
static u32 s_potLastEvtMs = 0;   /* 上一次电位器事件的时刻，用于限流 */

/* 读一次 P0.5 / ADC 通道 13 的原始值（0..4095） */
static u16 pot_read_raw(void)
{
    u16 sum = 0;
    u8  i;

    /* 第 1 次转换丢掉：ADC 刚切换通道时采样保持电容还没充到新电压
     * （TASK_INPUT 读 ADC13，TASK_SENSOR 读 ADC12，通道来回切）。 */
    (void)Get_ADCResult(ADC_CH13);

    for (i = 0; i < POT_AVG_TIMES; i++)
    {
        sum += Get_ADCResult(ADC_CH13);
    }

    return (u16)(sum / POT_AVG_TIMES);
}

u8 Input_GetPotLevel(void)
{
    return s_potLevel;
}

static void pot_scan(void)
{
    u16 raw;
    u8  lvl;

    raw = pot_read_raw();

    if (!s_potReady)
    {
        s_potReady = 1;
        s_potMark  = raw;
        /* 首次也把档位算出来，避免开机显示默认值而不是真实位置 */
        lvl = (u8)(((u32)raw * POT_LEVELS) / POT_ADC_FULL);
        if (lvl >= POT_LEVELS) lvl = POT_LEVELS - 1;
        s_potLevel = lvl;
        return;
    }

    lvl = (u8)(((u32)raw * POT_LEVELS) / POT_ADC_FULL);
    if (lvl >= POT_LEVELS)
    {
        lvl = POT_LEVELS - 1;
    }

    /* 【2026-09-18 新增 · 时间限流】两次电位器事件之间至少隔 200ms。
     *
     * 为什么必须加：每一条 EVT_POT 在 App_Menu 里会触发
     *   1) Storage_SaveAll()  —— 一次 EEPROM 扇区擦写（几十毫秒，还带 EA=0）
     *   2) Display_Refresh(1) —— 一次约 350ms 的整屏重画
     * 而电位器的 ADC 读数天然有抖动。真机数据表明：5 秒内做了 16 次整屏重画，
     * 16 x 350ms = 5.6 秒的 I2C 传输挤进 5 秒窗口 => 总线 100% 饱和
     * => I2C_Lock 抢不到 => 4 秒超时强夺 => 两个任务同时发字节 => 控制器被写坏（黑屏）。
     * 所以从源头把事件频率限住。 */
    if (SysTick_Elapsed(s_potLastEvtMs) < 200U)
    {
        return;
    }

    /* 原始值要真正动过一截才认，避免抖动误触发 */
    if (raw > s_potMark)
    {
        if ((u16)(raw - s_potMark) >= POT_RAW_STEP)
        {
            s_potMark = raw;
            if (lvl != s_potLevel)
            {
                s_potLevel = lvl;
                s_potLastEvtMs = SysTick_Get();
                evt_push(EVT_POT, s_potLevel);
            }
        }
    }
    else if (s_potMark > raw)
    {
        if ((u16)(s_potMark - raw) >= POT_RAW_STEP)
        {
            s_potMark = raw;
            if (lvl != s_potLevel)
            {
                s_potLevel = lvl;
                s_potLastEvtMs = SysTick_Get();
                evt_push(EVT_POT, s_potLevel);
            }
        }
    }
}

/*========================================================================
 *                              初始化
 *========================================================================*/

void Input_Init(void)
{
    Input_Flush();

    /* 旋钮状态复位 */
    s_potMark  = 0;
    s_potLevel = 50;
    s_potReady = 0;

    Keys_Init();
    MK_Init();

    /* 先读一次建立基准（这一次不产生事件） */
    pot_scan();
    Input_Flush();
}

void Input_Scan10ms(void)
{
    Keys_Scan();
    MK_Scan();
    pot_scan();
}

/*========================================================================
 *                        TASK_INPUT：每 10ms 一拍
 *========================================================================*/

void task_input(void) _task_ TASK_INPUT
{
    Input_Init();

    while (1)
    {
        Input_Scan10ms();

        /* 2 x 5ms = 10ms */
        os_wait2(K_TMO, 2);
    }
}
