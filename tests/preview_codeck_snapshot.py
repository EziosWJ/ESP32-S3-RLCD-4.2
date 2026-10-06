"""Render the actual Codeck C drawing code with the device's bitmap font."""
import argparse
import base64
import json
import copy
from test_codeck import FIXTURE
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
    parser.add_argument('--json',type=Path,help='Render a captured snapshot; output stays under ignored build/')
    args = parser.parse_args()
    work = ROOT / 'build/codeck_snapshot_preview'
    work.mkdir(parents=True, exist_ok=True)
    output = ROOT / ('build/codeck-current-preview' if args.json else 'docs/codeck-live-preview')
    output.mkdir(parents=True, exist_ok=True)
    (work / 'esp_err.h').write_text('typedef int esp_err_t;\n', encoding='utf-8')
    source = work / 'render.c'
    source.write_text(r'''#include <stdio.h>
#include <stdlib.h>
#include "codeck_page.h"
#include "codeck_label.h"
void rlcd_text(int x,int y,const char *t,unsigned s) { printf("T %d %d %u %s\n",x,y,s,t); }
void rlcd_hline(int x,int y,int w) { printf("H %d %d %d\n",x,y,w); }
void rlcd_rect(int x,int y,int w,int h) { printf("R %d %d %d %d\n",x,y,w,h); }
int main(int argc,char **argv) {
    if(argc!=5) return 1;
    FILE *file=fopen(argv[1],"rb"); if(!file) return 2;
    char body[4097]; size_t n=fread(body,1,4096,file); fclose(file); body[n]=0;
    static codeck_snapshot_t snapshot;
    if(codeck_state_parse(body,n,&snapshot)!=CODECK_OK) return 3;
    snapshot.status=(codeck_status_t)atoi(argv[2]);
    file=fopen(argv[4],"rb"); if(!file) return 4;
    unsigned char *font=malloc(921600); if(!font) return 5;
    if(fread(font,1,921600,file)!=921600) return 6;
    fclose(file); codeck_font_use(font);
    draw_codeck_page(&snapshot,(unsigned)atoi(argv[3]));
    free(font); return 0;
}
''', encoding='utf-8')
    env = os.environ.copy()
    env['ZIG_GLOBAL_CACHE_DIR'] = str(work / 'zig_cache')
    env['ZIG_LOCAL_CACHE_DIR'] = str(work / 'zig_local')
    executable = work / 'render.exe'
    cjson=ROOT/'managed_components/espressif__cjson/cJSON'
    subprocess.run([args.zig,'cc','-std=c11','-Wall','-Wextra','-Werror','-DCODECK_FONT_HOST',
                    '-DCJSON_NESTING_LIMIT=8','-I'+str(work),'-I'+str(ROOT/'main'),'-I'+str(cjson),str(source),
                    str(ROOT/'main/codeck_page.c'),str(ROOT/'main/codeck_state.c'),str(ROOT/'main/codeck_label.c'),
                    str(ROOT/'main/brand_icons.c'),str(cjson/'cJSON.c'),'-o',str(executable)],check=True,env=env)
    glyphs = {char: [int(x.strip()) for x in rows.split(',')]
              for char, rows in re.findall(r"\{'(.)', \{([0-9, ]+)\}\}",
                                          (ROOT / 'main/rlcd.c').read_text(encoding='utf-8'))}
    names = ['LIVE', '401 AUTH ERROR', '503 UNAVAILABLE', 'TLS FAILURE', 'OFFLINE', 'NO QUOTA', 'NO OBSERVATIONS', 'UNSUPPORTED VERSION', 'CAPACITY ERROR']
    statuses=[0,4,5,8,1,0,0,10,11]
    if args.json:
        names=['LIVE']; statuses=[0]
    screens = []
    for state, name in enumerate(names):
        data=json.loads(args.json.read_bytes()) if args.json else copy.deepcopy(FIXTURE)
        if state==5: data['quota']['available']=False
        if state==6:
            for account in data['balances']['items']: account['observed_at']=None
        fixture=work/f'fixture-{state}.json'
        fixture.write_text(json.dumps(data,ensure_ascii=False),encoding='utf-8')
        items=data['balances'].get('items',[]) if data['balances']['available'] else []
        page_count=1+(sum((len(a['amounts'])+1)//2 if a['observed_at'] and a['amounts'] else 1 for a in items) if items else 1)
        sheets = []
        for sheet in range(page_count):
            pixels = [bytearray([255] * 400) for _ in range(300)]
            text_boxes = []
            primitive_pixels = set()

            def black(x, y, primitive=False):
                assert 0 <= x < 400 and 0 <= y < 300, (state, sheet, x, y)
                pixels[y][x] = 0
                if primitive:
                    primitive_pixels.add((x, y))

            commands = subprocess.check_output([str(executable),str(fixture),str(statuses[state]),str(sheet),str(ROOT/'assets/fonts/codeck_labels.bin')], text=True)
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
    html = r'''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Codeck 真实页面绘图预览</title><style>body{background:#eef0ed;font:16px system-ui;color:#243028}main{max-width:1000px;margin:40px auto;padding:0 24px}p{line-height:1.8}nav{display:flex;gap:8px;flex-wrap:wrap;margin:24px 0}button{padding:10px;border:1px solid #b9c5b7;background:white;border-radius:8px;cursor:pointer}button[aria-pressed=true]{background:#24412e;color:white}#pages{display:flex;gap:24px;flex-wrap:wrap}figure{margin:0}img{width:400px;max-width:100%;image-rendering:pixelated;border:8px solid #344334;box-sizing:border-box}figcaption{padding:12px 0}</style>
<main><h1>Codeck · 真实页面绘图</h1><p>使用真实解析器和页面绘图代码，输入为快照数据，不包含设备凭据。所有时间为 UTC。
服务与额度为第一页，随后按账户和币种分页；每个账户每页最多两种币种，逐项独立显示。中文配置名使用 16 px 点阵字形。</p>
<nav id="states"></nav><div id="pages"></div><p>请求失败时保留最后成功快照，顶部显示原因及 LAST KNOWN，底部标记 CACHED 和最后快照生成时间。
401 每 15 分钟最多重试一次，正常轮询间隔 45 秒；普通错误指数退避并加入抖动。</p></main><script>const screens=SCREENS;const buttons=screens.map((s,i)=>{const b=document.createElement('button');b.textContent=s.name;b.onclick=()=>show(i);document.getElementById('states').append(b);return b;});function show(i){const pages=document.getElementById('pages');pages.replaceChildren();screens[i].images.forEach((src,j)=>{const f=document.createElement('figure');const img=document.createElement('img');img.src=src;img.alt='Codeck page '+(j+1);const c=document.createElement('figcaption');c.textContent=(j+1)+' / '+screens[i].images.length;f.append(img,c);pages.append(f);});buttons.forEach((b,j)=>b.setAttribute('aria-pressed',String(i===j)));}show(0);</script></html>'''
    (output / 'index.html').write_text(html.replace('SCREENS', json.dumps(screens, ensure_ascii=False)), encoding='utf-8')
    print('Rendered snapshot pages; glyphs, bounds and graphic/text overlaps checked.')
    print(output / 'index.html')


if __name__ == '__main__':
    main()
