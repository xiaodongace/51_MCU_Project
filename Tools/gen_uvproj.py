"""生成 51_MCU_Alarm/Project.uvproj。

做法：直接拿 v3.1 的 Project.uvproj 当模板，只替换
  1) IncludePath（把 .\Driver\SPI_OLED 和 .\Driver\I2C_OLED 也加进去，
     否则 spi_oled.h / oled.h 找不到）
  2) 整个 <Groups> 块（文件清单）

这样编译器/链接器的其它设置（设备 STC8H8K64U、CPU 内存布局、Compact 内存模型、
优化级别、中断向量、CreateHexFile 等）全部原样继承 v3.1 这套真机跑通的配置，
不去手写 .uvproj 里那些容易搞错的反斜杠与编码 —— 规范第二节点名过这个坑。
"""
import os
import re

SRC = r'D:\TooL\Share_SyncHome\嵌入式3阶段\day17\Code\02_STC8_扩展板自检程序v3.1_基于RTX51\Project.uvproj'
DST = r'D:\Develop\embedded-project\51_MCU_Alarm\Project.uvproj'

FT_C = 1
FT_ASM = 2
FT_LIB = 4
FT_H = 5

GROUPS = [
    ('User', [
        (r'.\User\main.c', FT_C),
    ]),
    ('App', [
        (r'.\App\App_Public.h', FT_H),
        (r'.\App\App_Public.c', FT_C),
        (r'.\App\App_System.h', FT_H),
        (r'.\App\App_System.c', FT_C),
        (r'.\App\App_Font.h', FT_H),
        (r'.\App\App_Font.c', FT_C),
        (r'.\App\App_Input.h', FT_H),
        (r'.\App\App_Input.c', FT_C),
        (r'.\App\App_Clock.h', FT_H),
        (r'.\App\App_Clock.c', FT_C),
        (r'.\App\App_Alarm.h', FT_H),
        (r'.\App\App_Alarm.c', FT_C),
        (r'.\App\App_Storage.h', FT_H),
        (r'.\App\App_Storage.c', FT_C),
        (r'.\App\App_Sensor.h', FT_H),
        (r'.\App\App_Sensor.c', FT_C),
        (r'.\App\App_Music.h', FT_H),
        (r'.\App\App_Music.c', FT_C),
        (r'.\App\App_Songs.h', FT_H),
        (r'.\App\App_Songs.c', FT_C),
        (r'.\App\App_Display.h', FT_H),
        (r'.\App\App_Display.c', FT_C),
        (r'.\App\App_Icons.c', FT_C),
        (r'.\App\App_Menu.h', FT_H),
        (r'.\App\App_Menu.c', FT_C),
        (r'.\App\App_Uart.h', FT_H),
        (r'.\App\App_Uart.c', FT_C),
        (r'.\App\App_Game.h', FT_H),
        (r'.\App\App_Game.c', FT_C),
    ]),
    ('Driver', [
        (r'.\Driver\SPI_OLED\spi_oled.h', FT_H),
        (r'.\Driver\SPI_OLED\spi_oled.c', FT_C),
        (r'.\Driver\I2C_OLED\I2C_OLED.h', FT_H),
        (r'.\Driver\I2C_OLED\I2C_OLED.c', FT_C),
        (r'.\Driver\I2C_Lock.h', FT_H),
        (r'.\Driver\I2C_Lock.c', FT_C),
        (r'.\Driver\Keys.h', FT_H),
        (r'.\Driver\Keys.c', FT_C),
        (r'.\Driver\MatrixKey.h', FT_H),
        (r'.\Driver\MatrixKey.c', FT_C),
        (r'.\Driver\NIXIE.h', FT_H),
        (r'.\Driver\NIXIE.c', FT_C),
        (r'.\Driver\NixieScan.h', FT_H),
        (r'.\Driver\NixieScan.c', FT_C),
        (r'.\Driver\Timers.h', FT_H),
        (r'.\Driver\Timers.c', FT_C),
        (r'.\Driver\NTC.h', FT_H),
        (r'.\Driver\NTC.c', FT_C),
        (r'.\Driver\DHT11.h', FT_H),
        (r'.\Driver\DHT11.c', FT_C),
        (r'.\Driver\PCF8563.h', FT_H),
        (r'.\Driver\PCF8563.c', FT_C),
        (r'.\Driver\Buzzer.h', FT_H),
        (r'.\Driver\Buzzer.c', FT_C),
        (r'.\Driver\LED.h', FT_H),
        (r'.\Driver\LED.c', FT_C),
        (r'.\Driver\Motor.h', FT_H),
        (r'.\Driver\Motor.c', FT_C),
        (r'.\Driver\Servo.c', FT_C),
    ]),
    ('Lib', [
        (r'.\Lib\ADC.c', FT_C),
        (r'.\Lib\ADC_Isr.c', FT_C),
        (r'.\Lib\EEPROM.c', FT_C),
        (r'.\Lib\Exti.c', FT_C),
        (r'.\Lib\Exti_Isr.c', FT_C),
        (r'.\Lib\GPIO.c', FT_C),
        (r'.\Lib\NVIC.c', FT_C),
        (r'.\Lib\I2C.c', FT_C),
        (r'.\Lib\I2C_Isr.c', FT_C),
        (r'.\Lib\Delay.c', FT_C),
        (r'.\Lib\Soft_I2C.c', FT_C),
        (r'.\Lib\STC8H_PWM.c', FT_C),
        (r'.\Lib\Timer.c', FT_C),
        (r'.\Lib\Timer_Isr.c', FT_C),
        (r'.\Lib\UART.c', FT_C),
        (r'.\Lib\UART_Isr.c', FT_C),
    ]),
    ('OS', [
        (r'.\OS\Conf_tny.A51', FT_ASM),
        (r'.\OS\RTX51TNY.LIB', FT_LIB),
    ]),
]


def read(path):
    raw = open(path, 'rb').read()
    for enc in ('utf-8', 'gb18030'):
        try:
            return raw.decode(enc), enc
        except UnicodeDecodeError:
            continue
    raise SystemExit('cannot decode ' + path)


def build_groups():
    out = ['      <Groups>']
    for gname, files in GROUPS:
        out.append('        <Group>')
        out.append('          <GroupName>%s</GroupName>' % gname)
        out.append('          <Files>')
        for path, ft in files:
            fn = os.path.basename(path)
            out.append('            <File>')
            out.append('              <FileName>%s</FileName>' % fn)
            out.append('              <FileType>%d</FileType>' % ft)
            out.append('              <FilePath>%s</FilePath>' % path)
            out.append('            </File>')
        out.append('          </Files>')
        out.append('        </Group>')
    out.append('      </Groups>')
    return '\n'.join(out)


text, enc = read(SRC)

# 1) IncludePath
old_ip = '<IncludePath>.\\Lib;.\\User;.\\Driver;.\\App</IncludePath>'
new_ip = ('<IncludePath>.\\Lib;.\\User;.\\Driver;.\\App;'
          '.\\Driver\\SPI_OLED;.\\Driver\\I2C_OLED</IncludePath>')
if old_ip not in text:
    raise SystemExit('FAIL: IncludePath not found')
text = text.replace(old_ip, new_ip, 1)

# 2) Groups
m = re.search(r'      <Groups>.*?</Groups>', text, re.S)
if not m:
    raise SystemExit('FAIL: Groups block not found')
text = text[:m.start()] + build_groups() + text[m.end():]

with open(DST, 'wb') as f:
    f.write(text.encode(enc))

# 校验：注册的文件是否都真的存在
root = r'D:\Develop\embedded-project\51_MCU_Alarm'
missing = []
for gname, files in GROUPS:
    for path, ft in files:
        rel = path.replace('.\\', '').replace('\\', os.sep)
        full = os.path.join(root, rel)
        if not os.path.exists(full):
            missing.append(path)

print('WROTE', DST)
print('groups=%d files=%d' % (len(GROUPS), sum(len(f) for _, f in GROUPS)))
print('MISSING:', missing if missing else 'none')
