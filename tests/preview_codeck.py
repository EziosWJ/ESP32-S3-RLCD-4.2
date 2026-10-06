"""Render the historical, pre-unified Codeck demo (not current firmware)."""
import argparse
import base64
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', default=str(ROOT / 'build/portal_toolchain/ziglang/zig.exe'))
    args = parser.parse_args()
    work = ROOT / 'build/codeck_preview'
    work.mkdir(parents=True, exist_ok=True)
    output = ROOT / 'docs/codeck-preview'
    output.mkdir(parents=True, exist_ok=True)
    (work / 'esp_err.h').write_text('typedef int esp_err_t;\n', encoding='utf-8')
    source = work / 'render.c'
    source.write_text('''#include <stdio.h>
#include <stdlib.h>
#include "codeck_demo.h"
void rlcd_text(int x, int y, const char *t, unsigned s) { printf("T %d %d %u %s\\n", x,y,s,t); }
void rlcd_hline(int x, int y, int w) { printf("H %d %d %d\\n", x,y,w); }
void rlcd_rect(int x, int y, int w, int h) { printf("R %d %d %d %d\\n", x,y,w,h); }
int main(int argc, char **argv) {
    if (argc != 3) return 1;
    draw_codeck_demo((codeck_demo_state_t)atoi(argv[1]), (unsigned)atoi(argv[2]));
    return 0;
}
''', encoding='utf-8')
    env = os.environ.copy()
    env['ZIG_GLOBAL_CACHE_DIR'] = str(work / 'zig_cache')
    env['ZIG_LOCAL_CACHE_DIR'] = str(work / 'zig_local')
    executable = work / 'render.exe'
    subprocess.run([args.zig, 'cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(work), '-I' + str(ROOT / 'main'), str(source),
                    str(ROOT / 'main/codeck_demo.c'), str(ROOT / 'main/brand_icons.c'),
                    '-o', str(executable)], check=True, env=env)
    glyphs = {char: [int(x.strip()) for x in rows.split(',')]
              for char, rows in re.findall(r"\{'(.)', \{([0-9, ]+)\}\}",
                                          (ROOT / 'main/rlcd.c').read_text(encoding='utf-8'))}
    names = ['正常', '余额过期（接口示例）', '额度不可用', '服务离线 / 503（保留快照）',
             '凭据错误 / 401（保留快照）', '空数据', '余额分区不可用', '服务分区不可用']
    screens = []
    for state, name in enumerate(names):
        sheets = []
        for sheet in range(2):
            pixels = [bytearray([255] * 400) for _ in range(300)]
            text_boxes = []
            primitive_pixels = set()

            def black(x, y, primitive=False):
                assert 0 <= x < 400 and 0 <= y < 300, (state, sheet, x, y)
                pixels[y][x] = 0
                if primitive:
                    primitive_pixels.add((x, y))

            commands = subprocess.check_output([str(executable), str(state), str(sheet)], text=True)
            for line in commands.splitlines():
                parts = line.split(' ', 4)
                kind, x, y, arg = parts[:4]
                x, y, arg = int(x), int(y), int(arg)
                if kind == 'T':
                    text = parts[4]
                    box = (x, y, x + (len(text) * 6 - 1) * arg, y + 7 * arg)
                    assert box[2] <= 400 and box[3] <= 300, (state, sheet, text)
                    for old_box, old_text in text_boxes:
                        assert not (box[0] < old_box[2] and old_box[0] < box[2] and
                                    box[1] < old_box[3] and old_box[1] < box[3]), (text, old_text)
                    text_boxes.append((box, text))
                    for i, char in enumerate(text):
                        if char == ' ':
                            continue
                        assert char in glyphs, char
                        for row, bits in enumerate(glyphs[char]):
                            for col in range(5):
                                if bits & (1 << (4 - col)):
                                    for dy in range(arg):
                                        for dx in range(arg):
                                            black(x + i * 6 * arg + col * arg + dx, y + row * arg + dy)
                elif kind == 'H':
                    for dx in range(arg):
                        black(x + dx, y, True)
                elif kind == 'R':
                    height = int(parts[4])
                    for dx in range(arg):
                        black(x + dx, y, True)
                        black(x + dx, y + height - 1, True)
                    for dy in range(height):
                        black(x, y + dy, True)
                        black(x + arg - 1, y + dy, True)

            for (left, top, right, bottom), text in text_boxes:
                assert not any(left <= x < right and top <= y < bottom
                               for x, y in primitive_pixels), ('Graphic overlaps text', state, sheet, text)

            def chunk(kind, data):
                return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

            png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 400, 300, 8, 0, 0, 0, 0)) +
                   chunk(b'IDAT', zlib.compress(b''.join(b'\x00' + p for p in pixels))) + chunk(b'IEND', b''))
            (output / f'{state}-{sheet}.png').write_bytes(png)
            sheets.append('data:image/png;base64,' + base64.b64encode(png).decode())
        screens.append({'name': name, 'images': sheets})
    html = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Codeck RLCD 页面预览</title>
<style>
body{margin:0;background:#eef0ed;color:#20251f;font:16px system-ui,sans-serif}
main{max-width:1000px;margin:40px auto;padding:0 24px}h1{font-size:28px;margin-bottom:10px}
p{line-height:1.8;color:#51594f}nav{display:flex;gap:8px;flex-wrap:wrap;margin:24px 0}
button,select{font:inherit;padding:10px 14px;border:1px solid #bbc3b8;border-radius:8px;background:white;color:inherit}
button{cursor:pointer}button[aria-pressed=true]{background:#233a2b;color:white}
.screens{display:flex;gap:24px;flex-wrap:wrap}figure{margin:0;max-width:100%}
img{width:400px;max-width:100%;height:auto;image-rendering:pixelated;box-shadow:0 4px 20px #0002;border:10px solid #333c33;border-radius:6px;box-sizing:border-box}
figcaption{margin:12px 0 24px;color:#51594f}.note{border-top:1px solid #cbd0c8;padding-top:16px}
</style><main><h1>Codeck · 设备状态</h1>
<p>历史布局演示，不代表当前固件。请查看 <a href="../ui-preview/index.html">统一 UI 生产预览</a>。<br>
400 × 300 黑白横屏 · 使用旧演示代码与 5 × 7 点阵字体生成。<br>
这是固定示例数据；时间统一标为 UTC，快照生成于 2026-10-06 08:30。</p>
<nav aria-label="示例状态" id="states"></nav>
<div class="screens"><figure><img id="overview" alt="服务与额度页面"><figcaption>01 / 服务与额度</figcaption></figure>
<figure><img id="balances" alt="账户余额页面"><figcaption>02 / 账户余额</figcaption></figure></div>
<p class="note">板上交互：KEY 短按依次切换「环境 → Codeck → 网络 → 系统」。进入 Codeck 后，每 12 秒切换一屏，
依次展示正常、余额过期、额度不可用、离线和凭据错误；重新进入时从正常状态开始。长按 3 秒仍进入 Wi-Fi 配网。</p>
<p>额度区为 Codex，配官方终端 UI 图标和剩余额度条；余额分别配 DeepSeek 官方鲸鱼、OpenRouter 官方新版图标。
官方资源按比例转换成黑白点阵，可查看<a href="../brand-icons/index.html">图标图库及来源</a>。
余额逐账户、逐币种显示，不合计。主账户暂以 PRIMARY ACCOUNT 显示；OpenRouter 的平台与配置名相同。
OLD DATA 表示该余额上游获取失败后的历史值；CACHED 表示请求失败后整份上次快照。
金额或额度未知时显示 --，其他可用分区保留显示。</p>
<p>数据较多时：余额每屏最多两条配置、每条最多两种币种；更多配置顺延分页，更多币种在下一屏重复平台与配置名。
显示实际页码，可沿用每 12 秒轮播。当前只实现两条配置的演示布局。</p>
</main><script>
const screens=SCREENS;
const buttons=screens.map((screen,i)=>{const b=document.createElement('button');b.textContent=screen.name;
b.onclick=()=>show(i);document.getElementById('states').append(b);return b;});
function show(i){document.getElementById('overview').src=screens[i].images[0];
document.getElementById('balances').src=screens[i].images[1];buttons.forEach((b,j)=>b.setAttribute('aria-pressed',String(i===j)));}
show(1);
</script></html>'''
    (output / 'index.html').write_text(html.replace('SCREENS', json.dumps(screens, ensure_ascii=False)), encoding='utf-8')
    print('Rendered 16 screens; icon dimensions, glyphs, bounds and graphic/text overlaps checked.')
    print(output / 'index.html')


if __name__ == '__main__':
    main()
