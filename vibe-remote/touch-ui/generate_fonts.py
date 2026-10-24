"""Generate the shared LVGL 7 fonts from OFL-licensed Noto Sans CJK JP."""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--node', required=True)
p.add_argument('--converter', required=True)
p.add_argument('--font', required=True)
a = p.parse_args()
root = Path(__file__).resolve().parent
chars = set(chr(c) for c in range(32, 127))
for lead in range(0x81, 0xf0):
    for trail in range(0x40, 0xfd):
        try:
            value = bytes([lead, trail]).decode('shift_jis')
            if len(value) == 1:
                chars.add(value)
        except UnicodeDecodeError:
            pass
for name, size, symbols in (
    ('vibe_jp_16', 16, ''.join(sorted(chars))),
    ('vibe_status_24', 24, '確認待ち実行中完了エラー機'),
):
    subprocess.run([a.node, a.converter, '--font', a.font, '--symbols', symbols,
                    '--size', str(size), '--bpp', '2', '--format', 'lvgl', '--no-kerning',
                    '--lv-include', 'lvgl/lvgl.h', '--lv-font-name', name,
                    '-o', str(root / (name + '.c'))], check=True)
