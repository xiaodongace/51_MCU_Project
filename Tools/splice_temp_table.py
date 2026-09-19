"""把 v3.1 Driver/NTC.c 里的 temp_table[] 原样搬进本工程 NTC.c 的占位处。

逐字搬运，不重打任何一个数字——规范第三条要求"寄存器/配置序列照抄"，
查表数据同理。
"""
import re

src = r'D:\TooL\Share_SyncHome\嵌入式3阶段\day17\Code\02_STC8_扩展板自检程序v3.1_基于RTX51\Driver\NTC.c'
dst = r'D:\Develop\embedded-project\51_MCU_Alarm\Driver\NTC.c'

with open(src, 'rb') as f:
    raw = f.read()
for enc in ('utf-8', 'gb18030', 'latin-1'):
    try:
        s = raw.decode(enc)
        break
    except UnicodeDecodeError:
        continue

m = re.search(r'u16\s+code\s+temp_table\s*\[\s*\]\s*=\s*\{.*?\n\};', s, re.S)
if not m:
    raise SystemExit('FAIL: temp_table not found in source')

table = m.group(0)

with open(dst, 'rb') as f:
    draw = f.read()
for enc in ('utf-8', 'gb18030', 'latin-1'):
    try:
        d = draw.decode(enc)
        break
    except UnicodeDecodeError:
        continue

if '/*__TEMP_TABLE__*/' not in d:
    raise SystemExit('FAIL: placeholder not found in target')

d = d.replace('/*__TEMP_TABLE__*/', table)

with open(dst, 'w', encoding='utf-8', newline='\n') as f:
    f.write(d)

n = len(re.findall(r'\d+\s*,', table))
print('OK: spliced, numeric entries =', n)
print('first line:', table.splitlines()[0])
print('last line :', table.splitlines()[-1])
