"""去掉驱动里唯一的浮点用法（SPI_OLED_ShowNum 的 float 形参）。

为什么必须改：C51 是**模块级链接** —— 只要一个 .c 被链接，它里面所有函数都会被链入，
哪怕没有被调用。spi_oled.c 里 SPI_OLED_ShowNum(u8,u8,float,u8) 含有 `num1*100`，
于是整个 C51FPL.LIB（?C?FPMUL / ?C?FPDIV / ?C?CASTF / ?C?FPCONVERT / ?C?FPADD）
就被拉进来了 —— 这正是《AI项目生成规范》第二节第三层验证要抓的东西。

改法：形参从 float 改成"已经乘 100 的整数"，功能等价，彻底避开浮点。
调用方（本项目没有任何地方调用它）如需使用，自己乘 100 即可。
保持文件原编码。
"""
import os

ROOT = r'D:\Develop\embedded-project\51_MCU_Alarm'


def read(p):
    raw = open(p, 'rb').read()
    for enc in ('utf-8', 'gb18030'):
        try:
            return raw.decode(enc), enc
        except UnicodeDecodeError:
            continue
    raise SystemExit('cannot decode ' + p)


def write(p, text, enc):
    open(p, 'wb').write(text.encode(enc))


p = os.path.join(ROOT, r'Driver\SPI_OLED\spi_oled.c')
text, enc = read(p)

old = ("void SPI_OLED_ShowNum(u8 x,u8 y,float num1,u8 len)\r\n"
       "{\r\n"
       "\tu8 i;\r\n"
       "\tu32 t,num;\r\n"
       "\tx=x+len*8+8;//要显示的小数最低位的横坐标\r\n"
       "\tnum=num1*100;//将小数左移两位并转化为整数\r\n")

new = ("/* 本项目修正：原厂这个函数的形参是 float。\r\n"
       " * C51 是模块级链接，只要本文件被链接，这个函数就会被整段链入，\r\n"
       " * 于是 `num1*100` 会把整个浮点库 C51FPL.LIB（?C?FPMUL / ?C?FPDIV / ?C?CASTF ...）拉进来。\r\n"
       " * 规范第二节第三层验证明确要求读 .m51 确认没有这些符号，故改为整数：\r\n"
       " * 形参 num100 = 真实数值 x 100，由调用方自己乘。功能等价。 */\r\n"
       "void SPI_OLED_ShowNum(u8 x,u8 y,u32 num100,u8 len)\r\n"
       "{\r\n"
       "\tu8 i;\r\n"
       "\tu32 t,num;\r\n"
       "\tx=x+len*8+8;//要显示的小数最低位的横坐标\r\n"
       "\tnum=num100;//调用方已经乘过 100\r\n")

if old not in text:
    raise SystemExit('FAIL: ShowNum body not found')
text = text.replace(old, new, 1)
write(p, text, enc)
print('OK patched spi_oled.c')

p = os.path.join(ROOT, r'Driver\SPI_OLED\spi_oled.h')
text, enc = read(p)
old = "void SPI_OLED_ShowNum(u8 x,u8 y,float num,u8 len);"
new = ("/* 本项目修正：形参由 float 改为 num100 = 数值 x 100，避免链入浮点库 */\r\n"
       "void SPI_OLED_ShowNum(u8 x,u8 y,u32 num100,u8 len);")
if old not in text:
    raise SystemExit('FAIL: ShowNum prototype not found')
text = text.replace(old, new, 1)
write(p, text, enc)
print('OK patched spi_oled.h')
