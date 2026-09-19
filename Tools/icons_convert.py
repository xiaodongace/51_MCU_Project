# 图标取模转换脚本
#   输入: _icons_src/0_CLOCK.png ... 6_GAME.png（AI 生成的纯黑白矢量图）
#   输出: App/App_Icons.c（128x64 1bpp，SSD1306 页格式，每张 1024 字节）
#   流程: 灰度 -> 二值化(阈值128) -> 裁白边 -> 等比缩放到 128x64 -> 居中 -> 页打包
import sys, os
from PIL import Image
ORDER = ['CLOCK','ALARM','POMODORO','RANGE','GIMBAL','SETTINGS','GAME']
SRC = sys.argv[1] if len(sys.argv) > 1 else r'D:/Develop/WorkBuddyDATA/2026-09-17-21-52-01/_icons_src'
OUT = sys.argv[2] if len(sys.argv) > 2 else r'D:/Develop/embedded-project/51_MCU_Alarm/App/App_Icons.c'
def convert(path):
    im = Image.open(path).convert('L'); W, H = im.size; p = im.load()
    x0, y0, x1, y1 = W, H, -1, -1
    for y in range(H):
        for x in range(W):
            if p[x, y] < 128:
                x0 = min(x0, x); x1 = max(x1, x); y0 = min(y0, y); y1 = max(y1, y)
    cw, ch = x1-x0+1, y1-y0+1
    sc = min(128.0/cw, 64.0/ch); nw, nh = max(1, int(cw*sc)), max(1, int(ch*sc))
    ox, oy = (128-nw)//2, (64-nh)//2
    cp = im.crop((x0, y0, x1+1, y1+1)).resize((nw, nh), Image.LANCZOS).load()
    g = [[0]*128 for _ in range(64)]
    for y in range(nh):
        for x in range(nw):
            if cp[x, y] < 128: g[oy+y][ox+x] = 1
    return g, nw, nh
def to_pages(g):
    out = []
    for pg in range(8):
        for col in range(128):
            by = 0
            for bit in range(8):
                if g[pg*8+bit][col]: by |= (1 << bit)
            out.append(by)
    return out
L = ['/*', ' * App_Icons.c - 任务清单的 7 个图标（I2C 副屏，整屏 128x64）', ' *',
     ' * 【来源】AI 生成的纯黑白矢量图标图，经"灰度->二值化->裁白边->等比缩放到',
     ' *   128x64->居中"取模而来。每张 128*64/8 = 1024 字节，共 7168 字节（code 区）。',
     ' *   转换脚本同目录 Tools/icons_convert.py；原图在 _icons_src/0_CLOCK.png … 6_GAME.png。',
     ' *', ' * 数据按 SSD1306 页格式：每字节 8 个竖排像素，低位在上。', ' */',
     '#include "App_Icons.h"', '']
for i, name in enumerate(ORDER):
    g, nw, nh = convert(os.path.join(SRC, '%d_%s.png' % (i, name)))
    d = to_pages(g)
    print('  %-9s %3dx%2d' % (name, nw, nh))
    L.append('u8 code ICON_%s[%d] =' % (name, len(d)))
    L.append('{')
    for k in range(0, len(d), 12):
        L.append('    ' + ', '.join('0x%02X' % v for v in d[k:k+12]) + ',')
    L[-1] = L[-1].rstrip(','); L.append('};'); L.append('')
L += ['u8 code *Icon_Get(u8 idx)', '{', '    switch (idx)', '    {']
for i, name in enumerate(ORDER):
    L.append('    case %d: return ICON_%s;' % (i, name))
L += ['    default: return ICON_CLOCK;', '    }', '}', '']
open(OUT, 'wb').write('\n'.join(L).encode('gb18030'))
print('written', OUT)
