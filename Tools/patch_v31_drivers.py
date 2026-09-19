"""对 v3.1 原样搬入的驱动做最小必要修补（保持原文件编码，避免整文件被改写）。

修补项：
 1) Driver/PCF8563.c
    a) PCF8563_set_alarm() 的"周"分支判据写错：用了 alarm.day，应为 alarm.week。
       原文：
           // 周:  M 1 1 1 - 0 0 0 0  Enable -> 0x00, Disable -> 0x80 (1 << 7)
           if(alarm.day < 0){ a[3] = 0x80; } else { a[3] = alarm.week + 0x00; }
       影响：只要"日"有效而"周"想禁用（或反之），写进硬件的闹钟周字段就是错的。
    b) PCF8563_init() 把 GPIO_config() 注释掉了，导致 P3.2/P3.3 没被配成开漏、
       P3.7 没被配成准双向口 → I2C 根本没配好。江文聪封装驱动的 PCF8563_init()
       是打开这两句的，本工程按江文聪版打开。
    c) 增加闹钟中断标志 g_rtcIrqFlag 的定义（ISR 只置位，I2C 由任务做）。

 2) Lib/Exti_Isr.c
    INT3_ISR_Handler 原来只写 WakeUpSource = 4，PCF8563_int_call() 从头到尾
    没有任何人调用过（v3.1 的漏洞）。本工程改成只置中断标志。
"""
import io
import os

def read(p):
    raw = open(p, 'rb').read()
    for enc in ('utf-8', 'gb18030'):
        try:
            return raw.decode(enc), enc
        except UnicodeDecodeError:
            continue
    raise SystemExit('cannot decode ' + p)

def write(p, text, enc):
    with open(p, 'wb') as f:
        f.write(text.encode(enc))

def patch(path, pairs, expect=1):
    text, enc = read(path)
    for old, new in pairs:
        n = text.count(old)
        if n != expect:
            raise SystemExit('FAIL %s : pattern count=%d (expect %d)\n%r' % (path, n, expect, old[:80]))
        text = text.replace(old, new)
    write(path, text, enc)
    print('OK patched', os.path.basename(path), '(enc=%s)' % enc)


ROOT = r'D:\Develop\embedded-project\51_MCU_Alarm'

# ---------- 1) PCF8563.c ----------
p = os.path.join(ROOT, r'Driver\PCF8563.c')
patch(p, [
    # a) 周的判据
    ("    if(alarm.day < 0){\r\n        a[3] = 0x80;",
     "    if(alarm.week < 0){\r\n        a[3] = 0x80;"),
    # b) 打开 GPIO 配置
    ("void PCF8563_init(void){\r\n//    GPIO_config();\r\n//    I2C_config();\r\n    Exti_config();\r\n}",
     "void PCF8563_init(void){\r\n"
     "    /* 修补：v3.1 把这两句注释掉了，导致 P3.2/P3.3 没配成开漏（I2C 直接不通）、\r\n"
     "     * P3.7 没配成准双向口。江文聪老师封装驱动的 PCF8563_init() 是打开的，按它来。\r\n"
     "     * I2C_config() 仍由 App_System.c 统一配置（本项目所有 I2C 从设备共用一套配置），\r\n"
     "     * 所以这里只补 GPIO_config()。 */\r\n"
     "    GPIO_config();\r\n"
     "    Exti_config();\r\n}"),
    # c) 中断标志定义
    ("#define I2C_SOFT    0",
     "/* 闹钟/定时器中断标志。INT3 中断服务里只置这一位，\r\n"
     " * 真正的 I2C 清标志动作（读 CS2、写回 AF=0）放到 TASK_LOGIC 里做，\r\n"
     " * 因为 I2C 总线要和副屏共用，不能在中断里抢。 */\r\n"
     "volatile bit g_rtcIrqFlag = 0;\r\n\r\n"
     "#define I2C_SOFT    0"),
])

# ---------- 2) PCF8563.h ----------
p = os.path.join(ROOT, r'Driver\PCF8563.h')
patch(p, [
    ("// 初始化PCF8563 (引脚和I2C)",
     "// 闹钟/定时器中断标志（定义在 PCF8563.c）。INT3 中断里置 1，由任务清零并处理。\r\n"
     "extern volatile bit g_rtcIrqFlag;\r\n\r\n"
     "// 初始化PCF8563 (引脚和I2C)"),
])

# ---------- 3) Lib/Exti_Isr.c ----------
p = os.path.join(ROOT, r'Lib\Exti_Isr.c')
patch(p, [
    ("u8 WakeUpSource;",
     "u8 WakeUpSource;\r\n\r\n"
     "/* 修补：v3.1 的 INT3 中断只写 WakeUpSource，PCF8563_int_call() 无人调用，\r\n"
     " * 闹钟中断实际上从来没被处理过。本工程在中断里只置标志位（中断必须极短），\r\n"
     " * I2C 清标志与业务派发放到 TASK_LOGIC 任务里做。 */\r\n"
     "extern volatile bit g_rtcIrqFlag;\r\n"
     "extern void PCF8563_on_exti_tick(void);"),
    ("\t// TODO: 在此处添加用户代码\r\n//\tP03 = ~P03;\r\n\tWakeUpSource = 4;",
     "\t// TODO: 在此处添加用户代码\r\n"
     "\t// 时钟芯片到点：只置标志，马上退出中断\r\n"
     "\tg_rtcIrqFlag = 1;\r\n"
     "\tWakeUpSource = 4;"),
])

print('ALL PATCHES DONE')
