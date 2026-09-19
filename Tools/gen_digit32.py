"""生成 16x32 七段风格数字字模（0-9 : - 空格），输出 C51 code 数组。

坐标：宽 16（x=0..15），高 32（y=0..31）。
SSD1306 页模式：像素 (x,y) -> page=y/8, bit=y%8（LSB 为页内顶部）。
数据布局：page-major，每页 16 列 -> data[p*16 + x]
"""

W, H = 16, 32

# 笔画：厚度 4
SEGMENTS = {
    'A': ('h', 1, 14, 1, 4),      # 上横
    'B': ('v', 11, 14, 1, 17),    # 右上竖
    'C': ('v', 11, 14, 14, 30),   # 右下竖
    'D': ('h', 1, 14, 27, 30),    # 下横
    'E': ('v', 1, 4, 14, 30),     # 左下竖
    'F': ('v', 1, 4, 1, 17),      # 左上竖
    'G': ('h', 1, 14, 14, 17),    # 中横
}

GLYPHS = {
    '0': 'ABCDEF',
    '1': 'BC',
    '2': 'ABGED',
    '3': 'ABGCD',
    '4': 'FGBC',
    '5': 'AFGCD',
    '6': 'AFGECD',
    '7': 'ABC',
    '8': 'ABCDEFG',
    '9': 'ABCDFG',
}


def blank():
    return [[0] * W for _ in range(H)]


def draw_seg(px, name):
    kind, x0, x1, y0, y1 = SEGMENTS[name]
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if 0 <= x < W and 0 <= y < H:
                px[y][x] = 1


def draw_box(px, x0, x1, y0, y1):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if 0 <= x < W and 0 <= y < H:
                px[y][x] = 1


def digit(ch):
    px = blank()
    for s in GLYPHS[ch]:
        draw_seg(px, s)
    return px


def colon():
    px = blank()
    draw_box(px, 6, 9, 8, 13)
    draw_box(px, 6, 9, 20, 25)
    return px


def minus():
    px = blank()
    draw_box(px, 3, 12, 14, 17)
    return px


def to_bytes(px):
    out = []
    for p in range(H // 8):
        for x in range(W):
            b = 0
            for bit in range(8):
                if px[p * 8 + bit][x]:
                    b |= (1 << bit)
            out.append(b)
    return out


ORDER = ['0', '1', '2', '3', '4', '5', '6', '7', '8', '9']
NAMES = ['ZERO', 'ONE', 'TWO', 'THREE', 'FOUR', 'FIVE', 'SIX', 'SEVEN', 'EIGHT', 'NINE',
         'COLON', 'MINUS', 'BLANK']

images = [digit(c) for c in ORDER]
images.append(colon())
images.append(minus())
images.append(blank())

# 终端预览
for name, px in zip(NAMES, images):
    print('---', name)
    for row in px:
        print(''.join('#' if v else '.' for v in row))

lines = []
lines.append('/*')
lines.append(' * App_Font.c - 16x32 七段风格数字字模（0-9 : - 空格）')
lines.append(' *')
lines.append(' * 说明：本文件由脚本 _tools/gen_digit32.py 程序化生成，非手工拼点阵。')
lines.append(' * 布局：16 列 x 32 行；页模式 page-major，每页 16 字节 -> [p*16 + x]；')
lines.append(' *      字节内 LSB 为页内最上一行。与 SPI_OLED / I2C_OLED 的页寻址一致。')
lines.append(' * 索引：0-9 数字、10 冒号、11 减号、12 空白。')
lines.append(' */')
lines.append('')
lines.append('#include "App_Font.h"')
lines.append('')
lines.append('/* [索引][页*16+列] */')
lines.append('u8 code DIGIT32[13][64] = {')
for name, px in zip(NAMES, images):
    data = to_bytes(px)
    lines.append('    { /* %s */' % name)
    for i in range(0, 64, 16):
        lines.append('        ' + ', '.join('0x%02X' % v for v in data[i:i + 16]) + ',')
    lines.append('    },')
lines.append('};')
lines.append('')

out = r'D:\Develop\embedded-project\51_MCU_Alarm\App\App_Font.c'
with open(out, 'w', encoding='utf-8') as f:
    f.write('\n'.join(lines))
print('WROTE', out, len('\n'.join(lines)), 'bytes')
