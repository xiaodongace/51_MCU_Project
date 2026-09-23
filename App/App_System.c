/*
 * App_System.c - 系统初始化
 *
 * 本文件只做三件事：
 *   1) sys_init()：按固定顺序把全系统拉起来（见函数里的分步说明）；
 *   2) 各外设的 IO 模式由各自的驱动 init 负责（江文聪老师封装驱动的做法），
 *      App_System 只补一个旋钮 ADC 输入口 P0.5；
 *   3) Boot_Crumb()：上电进度指示（BOOT_CRUMB_ENABLE 控制的诊断手段）。
 *
 * ── 本次真机联调的两处关键调整 ──────────────────────────────────────
 *
 *   A. Clock_Init() 从本函数移出，改到 TASK_LOGIC 里调用。
 *      原因：Clock_Init() 内部会走 I2C 读 PCF8563。放在这里意味着
 *      "任何 I2C 异常都会让 sys_init 永不返回 -> 一个任务都建不出来 -> 三块屏全黑"。
 *      移到任务里之后，最坏情况只是时间读不到（界面显示占位值），系统照常活着。
 *      这与《04》B 角色"时钟芯片不要乱动，改动前和 A 说"的精神一致：先保证系统能起来。
 *
 *   B. 加入 BOOT_CRUMB 上电进度指示（详见 App_Public.h）。
 *      同时把 Led_Init() 和小灯总开关提到最前面 —— 诊断手段必须自身先能用。
 */
#include "App_System.h"

#include "GPIO.h"
#include "UART.h"
#include "NVIC.h"
#include "I2C.h"
#include "ADC.h"
#include "Switch.h"

#include "LED.h"
#include "Motor.h"
#include "Servo.h"
#include "Buzzer.h"
#include "NixieScan.h"
#include "Timers.h"

#include "App_Storage.h"

#include "SPI_OLED.h"

/*========================================================================
 *                        上电进度指示（诊断手段）
 *========================================================================*/

void Boot_Crumb(u8 step)
{
#if BOOT_CRUMB_ENABLE
    /* step = 1..8：点亮第 1..step 颗灯；step = 0：全灭。
     * Led_SetMask 直接写 IO 口，不排队、不依赖任务。 */
    if (step == 0)
    {
        Led_SetMask(0x00);
    }
    else if (step >= LED_COUNT)
    {
        Led_SetMask(0xFF);
    }
    else
    {
        Led_SetMask((u8)((1u << step) - 1u));
    }
#else
    step = step;
#endif
}

/*========================================================================
 *                          各个外设的配置
 *========================================================================*/

void GPIO_config(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    /* 旋钮（电位器）接 P0.5，走 ADC 通道 13，模拟输入必须高阻 */
    GPIO_InitStructure.Pin  = GPIO_Pin_5;
    GPIO_InitStructure.Mode = GPIO_HighZ;
    GPIO_Inilize(GPIO_P0, &GPIO_InitStructure);
}

/* 串口1（照抄 v3.1 App_System.c，只改注释） */
void UART_config(void)
{
    COMx_InitDefine COMx_InitStructure;

    COMx_InitStructure.UART_Mode      = UART_8bit_BRTx;
    COMx_InitStructure.UART_BRT_Use   = BRT_Timer1;
    COMx_InitStructure.UART_BaudRate  = 115200ul;
    /* 【同类 bug 补漏】COMx_InitDefine 有 6 个字段，原来只赋了 5 个，
     * Morecommunicate（多机通讯允许）没赋值 —— 它是局部变量里的内存垃圾值，
     * 会被 UART_Configuration() 写进模式寄存器。同 Timer3 漏 TIM_PS 是同一类问题。 */
    COMx_InitStructure.Morecommunicate = DISABLE;
    COMx_InitStructure.UART_RxEnable  = ENABLE;
    COMx_InitStructure.BaudRateDouble = DISABLE;
    UART_Configuration(UART1, &COMx_InitStructure);

    NVIC_UART1_Init(ENABLE, Priority_1);
    UART1_SW(UART1_SW_P30_P31);         /* P3.0 RxD / P3.1 TxD，经 CH340 到 USB */
}

/*
 * I2C 总线解锁（bus recovery）。
 *
 * 现象：USB 冷插入时数码管与副屏时间都不走，但按一下重启按钮就正常。
 *
 * 原因：I2C 的经典故障 —— 从机（副屏 SSD1306 或时钟芯片 PCF8563）
 * 在电源爬坡过程中被"半个时钟"卡住，把 SDA 拉低不放。
 * 这时主机无论发多少次 START 都不成立（START 的定义是 SCL 高时 SDA 由高变低，
 * SDA 已经一直是低就构不成边沿），于是所有 I2C 事务全部失败。
 * 按重启按钮之所以有效：那时两个从机的电源早已稳定，不会再被卡住。
 *
 * 解法是标准的 9 个时钟脉冲：主机把 SCL 手动打 9 个周期，
 * 让从机把没发完的那一字节走完、释放 SDA，再补一个 STOP 复位总线状态。
 * 之后一切恢复正常。
 */
#define I2C_SCL_PIN     P32     /* 与 I2C_SW(I2C_P33_P32) 一致：SCL=P3.2, SDA=P3.3 */
#define I2C_SDA_PIN     P33

static void I2C_BusRecover(void)
{
    u8 i;

    /* 先按普通 IO 控制这两根线：SCL 推挽输出，SDA 先留作输入观察 */
    P3_MODE_OUT_PP(GPIO_Pin_2);
    P3_MODE_IO_PU(GPIO_Pin_3);

    I2C_SDA_PIN = 1;        /* 先放开 SDA（准双向口写 1 = 弱上拉） */
    I2C_SCL_PIN = 1;

    /* 打 9 个时钟：足够把从机可能残留的 1 个字节走完 */
    for (i = 0; i < 9; i++)
    {
        I2C_SCL_PIN = 0;
        NOP2();
        I2C_SCL_PIN = 1;
        NOP2();
    }

    /* 补一个 STOP：SCL 高时，SDA 由低变高 */
    P3_MODE_OUT_PP(GPIO_Pin_3);
    I2C_SDA_PIN = 0;
    NOP2();
    I2C_SCL_PIN = 1;
    NOP2();
    I2C_SDA_PIN = 1;
    NOP2();

    /* 交还给 I2C：两根线都配成开漏（板上有外部上拉） */
    P3_MODE_OUT_OD(GPIO_Pin_2 | GPIO_Pin_3);
}

/*
 * I2C 总线速度档位。
 * 实际 SCL 频率的算式来自 Lib/I2C.h 第 27 行的 I2C_SetSpeed 宏注释原文：
 *     总线速度 = Fosc / 2 / (Speed*2 + 4)
 * 本芯片 Fosc = 24MHz，代入：
 *     N = 13 -> 24e6 / 60  = 400 kHz   （v3.1 原值）
 *     N = 22 -> 24e6 / 96  = 250 kHz
 *     N = 58 -> 24e6 / 240 = 100 kHz
 *
 * 2026-09-18 先用 400 kHz 跑，原因如下：
 *   原本按方案 A 降到 100 kHz 做验证，实机结果是 —— **降速后 `[I2C] lock timeout`
 *   照旧出现**，说明问题与总线速率无关，方案 A 的假设被证伪。
 *   既然降速既不能解决问题、又要多花 4 倍 CPU（每个字节 90us -> 360us），
 *   那就退回 400 kHz，把 CPU 留给真正的问题排查。
 * 要再试其它档位，只改 I2C_SPEED_SEL 一行即可。
 */
#define I2C_SPEED_400K   13
#define I2C_SPEED_250K   22
#define I2C_SPEED_100K   58
#define I2C_SPEED_SEL    I2C_SPEED_400K

/* I2C（照抄 v3.1 App_System.c）
 * 副屏 OLED 与 PCF8563 共用这一对线，两者都不使能 I2C 中断（查询方式） */
void I2C_config(void)
{
    /* 【方案 B 修正 · 2026-09-19】恢复硬件 I2C。
     *
     * 真相：demo30 的 OLED 驱动（Driver/I2C_OLED/I2C_OLED.c）内部调的是
     *       I2C_Init() 与 I2C_WriteNbyte() —— **硬件 I2C**。
     * 我第 S0 轮"改成软件 I2C"这个决定是错的：
     *   1) demo30 用的就是硬件 I2C，而且真机跑得好好的；
     *   2) 改成软件 I2C 还要把我驱动里的接线全改一遍，反而把原代码拆散了。
     * 现在装回硬件 I2C，并把 OLED 驱动换回 demo30 原样版本。 */
    I2C_BusRecover();

    {
        I2C_InitTypeDef I2C_InitStructure;

        I2C_InitStructure.I2C_Mode    = I2C_Mode_Master;
        I2C_InitStructure.I2C_Enable  = ENABLE;
        I2C_InitStructure.I2C_MS_WDTA = DISABLE;
        I2C_InitStructure.I2C_Speed   = I2C_SPEED_SEL;
        I2C_InitStructure.I2C_SL_ADR  = 0;
        I2C_InitStructure.I2C_SL_MA   = DISABLE;
        I2C_Init(&I2C_InitStructure);

        NVIC_I2C_Init(I2C_Mode_Master, DISABLE, Priority_0);
        I2C_SW(I2C_P33_P32);
    }

}

/* ADC（照抄 v3.1 App_System.c，参数一个没改） */
void ADC_config(void)
{
    ADC_InitTypeDef ADC_InitStructure;

    /* 采样时间不能小于 10（厂家原文注释） */
    ADC_InitStructure.ADC_SMPduty   = 31;
    ADC_InitStructure.ADC_CsSetup   = 0;
    ADC_InitStructure.ADC_CsHold    = 1;
    ADC_InitStructure.ADC_Speed     = ADC_SPEED_2X16T;
    ADC_InitStructure.ADC_AdjResult = ADC_RIGHT_JUSTIFIED;
    ADC_Inilize(&ADC_InitStructure);

    ADC_PowerControl(ENABLE);

    NVIC_ADC_Init(DISABLE, Priority_0);
}



/*========================================================================
 *                          开机动画
 *
 * 【2026-09-22 用户要求】"动画换回原样子吧，现在的动画又快又丑"
 *   -> **观感参数换回原始值**：半径步长 1（每圈都画）+ 每帧 delay_ms(15)。
 *      上次为了提速把步长改成 6，代价就是"一跳一跳"的台阶感 —— 是我改错了。
 *
 * 【保留 1】代码结构仍然是**一个函数**（原来 Expand / Contract 两个合并成一个，
 *   去掉重复的"算最大半径"和中间状态）—— 这是纯粹的代码简化，观感与原来一致。
 *
 * 【保留 2】"动画期间其他设备一律不许动"是靠 sys_init 的调用位置保证的，
 *   那是独立需求，继续有效（见下面那段说明）。
 *
 * 【代价 vs 观感：留两个旋钮，以后想折中只改这两个数】
 *   step  = 半径步长。1 = 每圈都画（**原始观感，平滑**）；调大 -> 更快但台阶越明显
 *   delay = 每帧延时（ms），越小越快
 *   当前 129 帧 × 15ms + 每帧一次整屏刷新（软件 SPI 约 8.5ms）≈ 3.0 秒。
 *   **想折中请先动 delay（8~10ms），再考虑 step = 2** ——
 *   因为 step 直接毁掉平滑度，而 delay 只影响快慢。
 *
 * 【"动画期间其他设备一律不许动" —— 靠 sys_init 的调用位置保证】
 *   本函数被安排在**全系统最干净的窗口**里（见 sys_init 第 3.5 步）：
 *     · EA = 0                → 一个中断都不会来（数码管扫描 / 1ms 时基 / 时钟中断全不发生）
 *     · LED 总开关还关着       → 8 颗灯**物理上**不可能亮（Led_Power(1) 排在动画之后）
 *     · 蜂鸣器 / 马达还没 Init → PWM5/PWM6 输出使能没开，引脚已被 *_SafeLevel 按低
 *     · 数码管还没 Init        → Timer2 没配，位选/段选都没被驱动
 *     · 舵机信号脚已 Servo_SafeLevel 拉低
 *     · I2C 副屏还没 Init      → SSD1306 上电默认显示关（0xAE），全黑
 *   -> 所以这里**不要**再往里加任何"点亮别的东西"的动作。
 *========================================================================*/
void boot_animation(void)
{
    u8 i;
    u8 j;
    u8 cx   = 64;               /* 屏幕中心 */
    u8 cy   = 32;
    /* 【观感参数 · 已换回原始值】step = 1 表示"每圈都画"，看上去是平滑扩张。
     * 上次改成 6 想提速，结果是一跳一跳的台阶感 —— 用户："又快又丑"。
     * 想调快慢就改这两个数：step（台阶感）和下面的 delay_ms（帧率）。 */
    u8 step = 1;

    /* ---- 由中心向外扩散 ---- */
    SPI_OLED_GClear();

    for (i = 0; i <= 64; i = (u8)(i + step))
    {
        for (j = 0; j <= i; j++)
        {
            SPI_OLED_DrawPoint((u8)(cx + j), (u8)(cy + i - j));
            SPI_OLED_DrawPoint((u8)(cx - j), (u8)(cy + i - j));
            SPI_OLED_DrawPoint((u8)(cx + j), (u8)(cy - i + j));
            SPI_OLED_DrawPoint((u8)(cx - j), (u8)(cy - i + j));
        }

        SPI_OLED_Refresh();
        delay_ms(15);
    }

    /* ---- 再由外向内收缩 ---- */
    for (i = 64; i > 0; i = (u8)(i - step))
    {
        for (j = 0; j <= i; j++)
        {
            SPI_OLED_ClearPoint((u8)(cx + j), (u8)(cy + i - j));
            SPI_OLED_ClearPoint((u8)(cx - j), (u8)(cy + i - j));
            SPI_OLED_ClearPoint((u8)(cx + j), (u8)(cy - i + j));
            SPI_OLED_ClearPoint((u8)(cx - j), (u8)(cy - i + j));
        }

        SPI_OLED_Refresh();
        delay_ms(15);
    }

    /* ---- 收尾：整屏清干净，交给后面的正常刷屏 ---- */
    SPI_OLED_GClear();
    SPI_OLED_Refresh();
}

/*========================================================================
 *                          系统初始化
 *========================================================================*/

void sys_init(void)
{
    /* ---------- 1) 上电安全电平 + 把诊断手段准备好 ---------- */
    /* 顺序要求：先写 IO 口按到安全电平（此时端口还是准双向口，写 0 能可靠拉低），
     * 再配端口模式。 */
    Motor_SafeLevel();      /* P0.1 = 0：高电平震，先按灭 */
    Buzzer_SafeLevel();     /* P0.0 = 0：无源蜂鸣器上电静音 */

    /* 【2026-09-19 补】舵机信号脚 P2.5 也是"默认电平危险"的一类：
     * 复位后它是准双向口 + 输出锁存器 1 = 高，而舵机信号线"一直是高"
     * 相当于一个超长脉宽，舵机会直接甩到极限位置。
     * 用户报的正是"没进过云台任务时舵机偶尔自己转"。
     * P2.5 不在 Led_Init() 配的那批脚里（那批是 P2.7/6/3/2/1/0），所以必须在这里显式按低。 */
    Servo_SafeLevel();

    Led_Init();             /* 配 8 颗灯与总开关的端口模式，并全灭 */
    /* 【2026-09-21 用户要求】开机动画期间"其他所有设备不能亮或者有反应"。
     * 这里**故意只配端口 + 全灭，先不开总开关** ——
     * 总开关关着，8 颗灯在物理上就不可能亮。
     * Led_Power(1) 挪到开机动画之后（见下面第 3.5 步）。 */
    
    /* ---------- 2) 扩展寄存器访问使能 + 全项目 1ms 系统时钟 ---------- */
    EAXSFR();

    /* 【本次修复 · 关键】Timer3 1ms 系统时钟。
     * 第 1 版漏了这一步（v3.1 里没有 Timers.c，它是用户自己写的文件，搬库时没带过来），
     * 导致 g_sysTick 恒为 0、SysTick_Elapsed() 恒为 0 ——
     * 按键消抖判据永不成立（按键全无反应）、每秒节拍永不成立（时间/闹钟静止）。
     * 必须放在各外设之前：后面一切"看钟算差值"的逻辑都依赖它。 */
    Timers_Init();
    
    /* ---------- 3) 通信与模拟外设 ---------- */
    GPIO_config();
    UART_config();
    I2C_config();
    ADC_config();
    
    /* ---------- 3.5) 开机动画：全系统最干净的窗口 ----------
     * 【2026-09-21 用户要求】"开机动画期间，其他所有设备不能亮或者有反应"。
     *
     * 把动画卡在这一步是有意的，此刻：
     *   · EA = 0（还没开总中断）→ 数码管扫描(Timer2)、1ms 时基(Timer3)、
     *     时钟芯片中断(INT3) **一个都不会来**
     *   · LED 总开关关着 → 8 颗灯物理上不可能亮
     *   · 蜂鸣器(PWM5)/马达(PWM6) 还没 Init，且引脚已被 *_SafeLevel 按低
     *   · 数码管还没 Init（Timer2 没配、位选/段选没驱动）
     *   · 舵机信号脚已被 Servo_SafeLevel 拉低
     *   · I2C 副屏还没 Init，SSD1306 上电默认显示关（0xAE）→ 全黑
     * -> 动画**结束之前**，任何别的设备都不会有反应。
     *   （原有的 BOOT_CRUMB(1..3) 排在这一段之后，所以也不受影响。） */
    SPI_OLED_Init();
    SPI_OLED_ColorTurn(0);    // 0正常显示，1 反色显示
    SPI_OLED_DisplayTurn(0); // 0正常显示 1 屏幕翻转显示
    boot_animation();
    //=====================================开机动画

    /* 动画结束，现在才允许点灯（上面第 1 步里故意没开总开关） */
    Led_Power(1);
    
    BOOT_CRUMB(1);
    BOOT_CRUMB(2);
    BOOT_CRUMB(3);
    
    /* ---------- 4) PWM 相关：蜂鸣器（PWM5）与马达（PWM6），两者同属 PWMB ---------- */
    Buzzer_Init();
    Motor_Init();
    BOOT_CRUMB(4);

    /* ---------- 5) 数码管扫描（Timer2 1kHz 中断 + NIXIE_init） ----------
     * 这一步之前漏了：Nixie_ScanInit() 定义了但全工程无人调用，
     * 所以 Timer2 没配、NIXIE_init() 没跑、8 位数码管一位都不亮。
     * 放在这里而不是任务里，是为了让数码管和其它外设同时上电就绪。 */
    Nixie_ScanInit();
    Nixie_SetTime(0, 0, 0);
    BOOT_CRUMB(5);

    /* ---------- 6) 装载掉电数据 ----------
     * 只读不写：读出来无效就填默认值（Storage_Load 内部处理）。
     * 这里不做任何 I2C 访问 —— 见本文件头部"A"的说明。 */
    Storage_Load();
    /* 【2026-09-22】原来这里还有一句 Game_Init()（M1 占位：只清状态）。
     * 掌机功能改到 App_Menu.c 之后，游戏状态由 menu_enter(PAGE_GAME_HALL)
     * 和 game_start() 负责初始化，这里不需要了。
     * （App_Game.c 的 4 个占位函数已整体废弃 —— 它们白占 Flash，见该文件说明。） */
    BOOT_CRUMB(6);

    /* ---------- 7) 开全局中断 ---------- */
    EA = 1;
    BOOT_CRUMB(7);
    
    BOOT_CRUMB(8);
}
