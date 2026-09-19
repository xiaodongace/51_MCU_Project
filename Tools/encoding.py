"""两处配置修正 + 编码转换工具。

配置修正：
  1) Driver/PCF8563.h
     `#define PCF8563_ALARM_ENABLE DISABLE` —— v3.1 是关的，
     导致 PCF8563_int_call() 整个函数体被 `#if` 裁掉，闹钟中断根本没法处理。
     本工程要真的用 P3.7 中断做"到点叫醒"，所以改成 ENABLE。
  2) Lib/EEPROM.h
     `#define MCU_Type STC8X1K08` —— 官方示例的默认值，与本芯片（64KB）不符。
     本芯片应从 64KB 末尾划 4KB 作 EEPROM，对应 STC8XxK60 -> MOVC_ShiftAddress 0xF000。
     （实测该宏只在 EEPROM.h 内定义、无 .c 引用，所以此前无功能影响，属于配置错误。）

编码转换：
   本项目对源文件编码有硬要求 —— 凡是通过 SPI_OLED_Display_GB2312_string() 显示的中文，
   字符串字面量必须是 GBK/GB18030 字节，因为驱动是拿字节直接算字库 ROM 地址的：
       Address = ((MSB-0xB0)*94 + (LSB-0xA1) + 846) * 32
   如果源码是 UTF-8，一个汉字是 3 字节，算出来的地址就是垃圾，屏上是乱码。
   所以最终交付前，把所有 .c/.h 统一转成 GB18030。
"""
import os
import sys

ROOT = r'D:\Develop\embedded-project\51_MCU_Alarm'
EXTS = ('.c', '.h', '.a51')


def decode(raw):
    for enc in ('utf-8', 'gb18030'):
        try:
            return raw.decode(enc), enc
        except UnicodeDecodeError:
            continue
    return None, None


def patch_config():
    # ---- PCF8563.h：打开闹钟中断处理 ----
    p = os.path.join(ROOT, r'Driver\PCF8563.h')
    text, enc = decode(open(p, 'rb').read())
    old = "#define PCF8563_ALARM_ENABLE     DISABLE"
    new = ("/* 本项目修正：v3.1 这里是 DISABLE，导致 PCF8563_int_call() 的函数体\r\n"
           " * 被 #if 整段裁掉，闹钟中断实际从未被处理。要真用 P3.7 到点唤醒，必须打开。 */\r\n"
           "#define PCF8563_ALARM_ENABLE     ENABLE")
    if old in text:
        text = text.replace(old, new, 1)
        open(p, 'wb').write(text.encode(enc))
        print('OK PCF8563_ALARM_ENABLE -> ENABLE')
    else:
        print('SKIP PCF8563_ALARM_ENABLE (已改过或原文不符)')

    # ---- EEPROM.h：型号选对 ----
    p = os.path.join(ROOT, r'Lib\EEPROM.h')
    text, enc = decode(open(p, 'rb').read())
    old = "#define\tMCU_Type\tSTC8X1K08"
    new = "#define\tMCU_Type\tSTC8XxK60"
    if old in text:
        text = text.replace(old, new, 1)
        open(p, 'wb').write(text.encode(enc))
        print('OK MCU_Type -> STC8XxK60 (64KB, EEPROM 0xF000)')
    else:
        print('SKIP MCU_Type (已改过或原文不符)')


def convert(target):
    changed = 0
    skipped = 0
    for dirpath, _, files in os.walk(ROOT):
        if any(x in dirpath for x in ('Listings', 'Objects', '.git')):
            continue
        for fn in files:
            if not fn.lower().endswith(EXTS):
                continue
            p = os.path.join(dirpath, fn)
            raw = open(p, 'rb').read()
            text, enc = decode(raw)
            if text is None:
                print('  !! cannot decode', p)
                continue
            try:
                out = text.encode(target)
            except UnicodeEncodeError as e:
                print('  !! cannot encode as %s: %s (%s)' % (target, p, e))
                continue
            if out == raw:
                skipped += 1
            else:
                open(p, 'wb').write(out)
                changed += 1
    print('convert -> %s : changed=%d already-ok=%d' % (target, changed, skipped))


if __name__ == '__main__':
    action = sys.argv[1] if len(sys.argv) > 1 else 'patch'
    if action == 'patch':
        patch_config()
    elif action == 'to-gbk':
        convert('gb18030')
    elif action == 'to-utf8':
        convert('utf-8')
    else:
        print('usage: encoding.py [patch|to-gbk|to-utf8]')
