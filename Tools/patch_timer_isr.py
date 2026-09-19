"""修补 Lib/Timer_Isr.c（厂家给的定时器中断模板，扩展点就是那两个 TODO 注释）。

改动：
  Timer2 中断 -> 调 Nixie_Scan1ms()，做数码管 1ms 扫描（v3.1 原来是调 Timer_callback()）
  Timer3 中断 -> g_sysTick++，做全项目统一的 1ms 系统时钟
                 （v3.1 原来是 P64 = ~P64，翻一个没用的 IO，等于什么都没做）

同时清掉上一次补丁里留下的一个多余 extern 声明。
保持文件原编码。
"""
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

def patch(path, pairs):
    text, enc = read(path)
    for old, new in pairs:
        n = text.count(old)
        if n != 1:
            raise SystemExit('FAIL %s : count=%d for %r' % (path, n, old[:60]))
        text = text.replace(old, new)
    write(path, text, enc)
    print('OK patched', os.path.basename(path), '(enc=%s)' % enc)


ROOT = r'D:\Develop\embedded-project\51_MCU_Alarm'

# ---------- Lib/Timer_Isr.c ----------
patch(os.path.join(ROOT, r'Lib\Timer_Isr.c'), [
    # 1) extern 声明：Timer_callback -> Nixie_Scan1ms，并加上 g_sysTick
    ("extern void Timer_callback();",
     "/* 本项目接入的两个回调 */\r\n"
     "extern void Nixie_Scan1ms(void);           /* Driver/NixieScan.c：每次刷一位数码管 */\r\n"
     "extern volatile u32 g_sysTick;             /* App/App_Public.c：1ms 系统时钟 */"),

    # 2) Timer2 -> 数码管扫描
    ("\t// TODO: 在此处添加用户代码\r\n//\tP65 = ~P65;\r\n    Timer_callback();",
     "\t/* 数码管扫描：每 1ms 刷一位，8 位轮一圈 8ms = 125Hz。\r\n"
     "\t * 中断里只做这一件事，代码极短（约 20us）。 */\r\n"
     "\tNixie_Scan1ms();"),

    # 3) Timer3 -> 系统时钟
    ("\t// TODO: 在此处添加用户代码\r\n\tP64 = ~P64;",
     "\t/* 全项目统一的 1ms 系统时钟（《02》4.1 的\"墙上的钟\"）。\r\n"
     "\t * 中断里只自增一次，其它模块用 SysTick_Get() 读差值算时间，\r\n"
     "\t * 时间的推进与读取都不依赖任何等待。 */\r\n"
     "\tg_sysTick++;"),
])

# ---------- 清掉多余 extern ----------
p = os.path.join(ROOT, r'Lib\Exti_Isr.c')
text, enc = read(p)
old = "extern volatile bit g_rtcIrqFlag;\r\nextern void PCF8563_on_exti_tick(void);"
if old in text:
    text = text.replace(old, "extern volatile bit g_rtcIrqFlag;")
    write(p, text, enc)
    print('OK cleaned Exti_Isr.c stray extern')
else:
    print('SKIP Exti_Isr.c clean (pattern not found)')

print('DONE')
