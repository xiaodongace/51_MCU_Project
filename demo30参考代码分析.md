# demo30 参考代码分析（含与本工程的差异对照）

> 分析对象：`D:\Develop\embedded-learning\01_STC8\demo30_基于RTX51的扩展板自检程序`
> 目的：把这份「在同一块扩展板上真机跑通、无任何花屏黑屏」的代码读透，
> 作为本工程后续开发的准绳。
> 分析日期：2026-09-19

---

## 一、demo30 的代码结构

```
demo30/
├── main.c                     任务启动
├── App/
│   ├── App.h                  任务号宏 + 全局变量声明
│   ├── App_System.c           各外设初始化（GPIO/UART/I2C/ADC/PWM）
│   ├── App_OLED.c             ★ SPI 屏任务 + I2C 屏任务 + 刷新请求接口
│   ├── App_Keys.c             独立按键
│   ├── App_MatrixKey_Buzzer.c 矩阵键盘 + 蜂鸣器
│   ├── App_LED.c              8 颗灯 + LED_show()
│   ├── App_NTC.c              热敏电阻 + NTC_show()
│   ├── App_Motor.c            电位器 + 马达 + MOTOR_show()
│   ├── App_RTC.c              PCF8563 + RTC_show()
│   ├── App_NixieDigital.c     数码管 + NixieDigital_show()
│   └── App_DHT11.c            温湿度
├── Driver/
│   ├── I2C_OLED/              ★ I2C 屏驱动（自带软件 I2C 函数，但编译路径用的是硬件 I2C）
│   ├── SPI_OLED/              带字库 SPI 屏驱动
│   ├── Keys / MatrixKey / NIXIE / NTC / PCF8563
└── Lib/                       厂家库（含 I2C.c 硬件 I2C、Soft_I2C.c、Delay.c）
```

---

## 二、★ 核心：它的两个显示任务是怎么写的（App_OLED.c）

```c
// 菜单表：title 显示在 SPI 屏，content 显示在 I2C 屏
menu menu_arr[] = { {"8个LED闪烁","LED"}, {"热敏电阻","NTC"}, ... };
u8 menu_cnt = ...;

static u8 global_is_clear_screen = 1;

// 外部通过这个函数请求刷新；clear_screen=1 表示要整屏清一次
void APP_I2C_OLED_Refresh(u8 clear_screen) {
    if (clear_screen) global_is_clear_screen = 1;
    os_send_signal(TASK_I2C_OLED);          // ← 发信号，不是定时轮询
}

void task_3(void) _task_ TASK_I2C_OLED {
    I2C_OLED_Init();                        // 只在任务开头初始化一次
    I2C_OLED_ColorTurn(0);
    I2C_OLED_DisplayTurn(0);

    while (1) {
        if (global_is_clear_screen) {        // ← Clear 只在"切页"时做
            global_is_clear_screen = 0;
            I2C_OLED_Clear();
        }
        I2C_OLED_ShowString(0, 0, menu_arr[global_cur_pos].content, 16);  // 页面标题这一行

        switch (global_cur_pos) {
            case 0: LED_show();          break;   // ← 各模块自己的显示函数
            case 1: NTC_show();          break;
            case 2: MOTOR_show();        break;
            case 3: RTC_show();          break;   // ← RTC 读取就在这里！同一个任务里
            case 4: NixieDigital_show(); break;
            ...
        }
        os_wait1(K_SIG);                     // ← 等信号，运行期不主动轮询
    }
}
```

### 从这段能读出的四条设计（本工程曾全部违反）

| # | demo30 的做法 | 为什么重要 |
|---|---|---|
| **1** | **所有 I2C 访问都在 task_3 这一个任务里**——连 `RTC_show()` 读 PCF8563 都在里面 | **天然串行，所以它根本不需要总线锁。** 没有锁，就没有锁竞争、没有超时强夺、没有"两个任务同时发字节" |
| **2** | **`I2C_OLED_Clear()` 只在切页时做一次** | 平时不整屏重画，I2C 流量极小 |
| **3** | **页内只写"内容变了的那一行"**（`I2C_OLED_ShowString(0, 2, ...)` 一次 32 字节） | 单次写入极短，既不被打断，屏幕上也看不到"全黑再重画"的中间过程 |
| **4** | **渲染任务 `os_wait1(K_SIG)` 等信号**，运行期自己不数时间 | 屏幕任务的节拍由上游数据任务提供（详见 §2.1）。**注意：这不等于"纯事件驱动"** |

**对照本工程走过的弯路**（都写进了代码注释，作为教训记录）：

- ❌ 曾把 I2C 分给 TASK_RENDER（刷屏）+ TASK_LOGIC（读时钟），靠 `I2C_Lock` 交替
  → 锁竞争 → 4 秒超时强夺 → 两个任务同时发字节 → 字节流错位 → **花屏/黑屏，重启都救不回来**
- ❌ 曾每 10 秒强制整屏重画（自愈）→ Clear 1024 字节 + 4 行，"清屏与重画之间的窗口"被看到/被打断
  → **某一行闪乱码**
- ❌ 曾把 OLED 驱动从硬件 I2C 改成软件 I2C（S0）→ 把驱动原有的接线拆散了


### 2.1 信号是谁发的 —— demo30 **不是**"纯事件驱动"（2026-09-19 补正）

第一版分析里这一条我说轻了，容易让后续开发误以为"事件驱动"是 demo30 的架构特征，补正如下。

**demo30 一共 8 个任务，其中只有 2 个是信号驱动，而且都是屏幕任务：**

| 任务 | 唤醒方式 |
|---|---|
| `TASK_SPI_OLED` | **`os_wait1(K_SIG)`** |
| `TASK_I2C_OLED` | **`os_wait1(K_SIG)`** |
| `TASK_KEYS` | `os_wait2(K_TMO, 2)` |
| `TASK_LED` | `os_wait2(K_TMO, 25 / 255)` |
| `TASK_RTC` | `os_wait2(K_TMO, 100 / 200)` |
| `TASK_Motor` | `os_wait2(K_TMO, 40)` |
| `TASK_NIXIE` | `os_wait2(K_TMO, 1)` |
| `TASK_NTC` | `os_wait2(K_TMO, ...)` |

**而那 2 个屏幕任务的"事件源"，恰恰就是这些定时轮询的任务。**

全工程 `os_send_signal` 只有 2 个发送点，其中发给 I2C 屏的那个是 `APP_I2C_OLED_Refresh()`；
它有 8 个调用点，**6 个在数据任务里**，例如：

```c
/* App_RTC.c —— RTC 任务每 1 秒自己轮询一次，然后"叫醒"屏幕任务 */
void task_7() _task_ TASK_RTC {
    os_wait2(K_TMO, 100);
    PCF8563_init();
    while (1) {
        APP_I2C_OLED_Refresh(0);   /* ← 发信号给 TASK_I2C_OLED */
        os_wait2(K_TMO, 200);      /* ← 200 × 5ms = 1 秒 */
    }
}
```

**⇒ 正确的描述是：**

> demo30 的屏幕任务**不需要自己数时间**，因为它要显示的数据什么时候变，
> 由上游任务的轮询节拍决定；上游一变就 `os_send_signal` 叫它一声。
> **"时间"这件事仍然是靠轮询在数，只是数的人从屏幕任务换成了上游任务。**

**⇒ 所以"事件驱动"在 demo30 里不是一个架构，而是一个局部优化：
"屏幕内容不变的时候，没必要唤醒写屏任务"。**

**这条对本案的意义：** 本工程第 2 行是**秒刷新的时钟**，它的变化源是"过了 1 秒"——
**这不是事件，是时间。** 要么渲染任务自己按拍醒来数，要么上游任务替它数
（本工程由 `TASK_LOGIC` 每 10ms 轮询后置 `g_reqClockRefresh`）。
**信号不会自己产生。** 因此改成 `os_wait1(K_SIG)` 并不消灭轮询，只是把轮询从渲染任务搬到上游任务。
完整权衡见 §4.1。

---

## 三、★ 驱动层的真相：demo30 的 OLED 用的是**硬件 I2C**

`Driver/I2C_OLED/I2C_OLED.c` 里确实有一份自带的位翻转 I2C（`I2C_Start`/`Send_Byte`/`I2C_WaitAck`），
**但编译路径（`#else` 分支）调的是：**

```c
void I2C_OLED_WR_Byte(u8 dat, u8 mode) {
    if (mode == I2C_OLED_DATA) I2C_WriteNbyte(0x78, 0x40, &dat, 1);
    else                       I2C_WriteNbyte(0x78, 0x00, &dat, 1);
}
```

**`I2C_WriteNbyte` 是 `Lib/I2C.c` 的硬件 I2C 函数。**
（把该文件单独加入工程时，链接报 `_I2C_WRITENBYTE` / `_I2C_INIT` 未解析——这就是铁证。）

**⇒ 结论：demo30 用硬件 I2C + `I2C_Init()`，运行在 400 kHz（`I2C_SPEED_SEL = 13`）。**

**本工程现状：已按此改回硬件 I2C，并使用 demo30 原样的 `I2C_OLED.c/h`。**

---

## 四、本工程与 demo30 的逐项对照（当前状态）

| 项 | demo30 | 本工程当前 | 是否一致 |
|---|---|---|---|
| OLED 驱动 | `Driver/I2C_OLED/I2C_OLED.c` | **原样搬入同一份** | ✅ |
| OLED 总线 | 硬件 I2C 400 kHz | **硬件 I2C 400 kHz** | ✅ |
| I2C 任务 | 全部 I2C 在 task_3 内 | **全部 I2C 收进 TASK_RENDER** | ✅ |
| 总线锁 | **没有** | 保留 `I2C_Lock`（永不争用，作安全网） | ⚠️ 多一层，无害 |
| 清屏时机 | 只在切页 | **只在切页** | ✅ |
| 页内刷新 | 只写变化的那一行（32 字节） | **只重画变化的那一行** | ✅ |
| 周期性刷新 | **没有** | **已删除** | ✅ |
| 渲染任务唤醒 | `os_wait1(K_SIG)`，由数据任务发信号 | `os_wait2(K_TMO,1)`，每 5ms 自醒 | ⚠️ 实现不同——但**不是**"唯一差异"，也不是"事件驱动 vs 轮询"那么大的区别（见 §2.1 / §4.1） |
| 数据任务唤醒 | 全部 `os_wait2(K_TMO, n)` 定时轮询 | 全部 `os_wait2(K_TMO, n)` 定时轮询 | ✅ **一致** |
| RTC 读取 | 在 I2C 任务内 | **在 TASK_RENDER 内** | ✅ |

### 4.1 关于"唤醒方式"——为什么建议**不切**（2026-09-19 补正）

第一版把这一条写成"唯一剩下的实质差异，下一步就该改"。**这个说法不准确，会误导后续开发**，补正如下。

#### ① 先看清楚，两边其实长得一样

```
demo30： 数据任务(os_wait2 轮询) ──[ os_send_signal ]──> TASK_I2C_OLED(os_wait1 等信号)
本工程： 数据任务(os_wait2 轮询) ──[ 置 g_req* 标志 ]──> TASK_RENDER (os_wait2 每 5ms 自醒)
```

**两边都是"多个轮询任务 → 一个唯一碰 I2C 的渲染任务"。**
差别只有两点：(a) 交接用的是什么（信号位 / 请求标志）；(b) 渲染任务自己会不会按时醒。

#### ② 切过去的收益（估算，不是实测）

| 项 | 现在 | 切之后 |
|---|---|---|
| TASK_RENDER 醒的次数 | 1 tick = 5ms → **200 次/秒** | 约 1 次/秒 |
| 全工程任务切换次数 | 5 个任务合计约 **600 次/秒** | 约 400 次/秒 |

按 RTX51 Tiny 一次任务切换十来微秒估，全工程轮询开销大约 **1~2% CPU**；
切过去能省下的约 **0.2% CPU**。**这不是一个性能问题。**

#### ③ 代价：失败模式会变坏（这条最关键）

切完之后，**任何一处"改了屏幕内容却忘了发信号"，那部分显示就永久冻结。**
而这种冻结**在串口日志上完全看不出来**（日志照常打、`g_sysTick` 照常走），只能靠眼睛发现。

现在的轮询即使漏了某处判断，最坏也只是"晚 5ms"。
**收益 0.2%，代价是引入一个静默失败模式——风险不对称。**

至少要接上信号的路径有 5 条：

| 屏幕内容 | 变化源 | 切之后谁必须发信号 |
|---|---|---|
| 第 2 行 时钟 | TASK_LOGIC 每秒节拍 | TASK_LOGIC |
| 第 3 行 日期 | 同上（每天一次） | TASK_LOGIC |
| 第 4 行 音量 / 校时输入 | TASK_INPUT | TASK_INPUT |
| 第 1 行 温湿度 | TASK_SENSOR（1s） | TASK_SENSOR |
| 页面切换 / 响铃提示 | TASK_LOGIC | TASK_LOGIC |

**少接一条，就是一处静默冻结。**

#### ④ RTX51 Tiny 每个任务只有一个信号位

`os_send_signal(task_id)` **没有信号编号**，"该刷时钟"和"该刷温湿度"无法区分，
只能统一当成"去看看有什么要刷"。这本身不是问题（demo30 也这么用），
但它说明**信号不是"精确通知"，只是"催一下"** ——
替代不了"渲染任务自己比对内容有没有变"的逻辑。

#### ⑤ 结论

| 目标 | 建议 |
|---|---|
| 修掉"某行闪乱码" / "花屏黑屏" | **与本项无关**，已经修完了，不用切 |
| 省 CPU / 降功耗 | 收益约 0.2%，**不值得**引入静默冻结的失败模式 |
| 结构上和 demo30 一致（方便以后继续照抄） | 可以切，但必须把上面 5 个信号点**逐条接全**，并真机压测一遍 |
| 真做低功耗（停机模式、没事件就不唤醒 CPU） | 那才轮到这件事，且要动 RTX51 的 idle 处理与时钟源，是另一个题目 |

#### ⑥ 附：这次核实过的证据

- `LED_show()` / `NTC_show()` / `MOTOR_show()` / `RTC_show()` / `NixieDigital_show()`
  全工程**只有 1 个调用点**：`App_OLED.c` 里 `task_3` 的 `switch`。
  ⇒ **demo30 的 I2C 屏确实只有一个写者**（第一版结论成立，但当时是推断，现已核实）。
- `os_send_signal` 全工程 2 处：`App_Keys.c:49`（→ TASK_SPI_OLED）、`App_OLED.c:56`（→ TASK_I2C_OLED）。
- `APP_I2C_OLED_Refresh()` 8 个调用点：`App_Keys.c:51` `(1)` 清屏；其余 6 处在数据任务里 `(0)` 局部刷新。
- 两个工程都**没有**在任何中断服务函数里发信号（`os_*` 只出现在任务函数体内）。

---

## 五、验证记录（2026-09-19 00:57）

用户回报：**电位器旋转 + 按键按下持续 5 分钟，没有出现大量花屏和黑屏**，
最严重的现象只是"某一行闪一下乱码，下次刷新就正常"。

**⇒ "全部 I2C 收进一个任务"这个架构改造是有效的**，
症状从"永久损坏、重启都救不回来"降级为"瞬态瑕疵、自动恢复"。

---

## 六、后续开发的三条硬规矩（从这晚的弯路里提炼）

1. **说"照抄"就要照抄同一个项目的同一套实现。**
   不能从 A 项目抄驱动、从 B 项目抄总线、再自己接线——这晚最严重的一次错误就是这样来的。
2. **一块 I2C 总线，只能有一个任务碰。** 需要别的数据就发请求让那个任务去取，
   不要用锁去"轮流用"。锁只能保证互斥，保证不了"字节流不断"。
3. **热路径里不放 printf、不做整屏重画、不做周期性刷新。**
   这三样都是这晚花屏/重启的帮凶。
