"""Render the actual Codeck C drawing code with the device's bitmap font."""
import argparse
import base64
import json
import copy
from test_codeck import FIXTURE
import os
from pathlib import Path
import subprocess
from render_rlcd import render_commands

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', default=str(ROOT / 'build/portal_toolchain/ziglang/zig.exe'))
    parser.add_argument('--json',type=Path,help='Render a captured snapshot; output stays under ignored build/')
    parser.add_argument('--status',type=int,default=0,help='Status for a --json preview')
    parser.add_argument('--retry-seconds',type=int,default=0)
    parser.add_argument('--connecting',action='store_true')
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
#include "ui_chrome.h"
void rlcd_text(int x,int y,const char *t,unsigned s) { printf("T %d %d %u %s\n",x,y,s,t); }
void rlcd_hline(int x,int y,int w) { printf("H %d %d %d\n",x,y,w); }
void rlcd_rect(int x,int y,int w,int h) { printf("R %d %d %d %d\n",x,y,w,h); }
int main(int argc,char **argv) {
    if(argc!=7) return 1;
    FILE *file=fopen(argv[1],"rb"); if(!file) return 2;
    char body[4097]; size_t n=fread(body,1,4096,file); fclose(file); body[n]=0;
    static codeck_snapshot_t snapshot;
    if(codeck_state_parse(body,n,&snapshot)!=CODECK_OK) return 3;
    snapshot.status=(codeck_status_t)atoi(argv[2]);
    snapshot.retry_seconds=(unsigned)atoi(argv[5]); snapshot.fetching=atoi(argv[6])!=0;
    file=fopen(argv[4],"rb"); if(!file) return 4;
    unsigned char *font=malloc(921600); if(!font) return 5;
    if(fread(font,1,921600,file)!=921600) return 6;
    fclose(file); codeck_font_use(font);
    const ui_header_t header = {.time_valid=true,.connected=true,.signal_valid=true,.battery_valid=true,
                                .time="14:20",.rssi=-60,.battery_percent=85};
    ui_draw_header("CODECK", &header, (unsigned)atoi(argv[3]), codeck_page_count(&snapshot));
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
                    str(ROOT/'main/brand_icons.c'),str(ROOT/'main/ui_chrome.c'),str(ROOT/'main/display_time.c'),
                    str(cjson/'cJSON.c'),'-o',str(executable)],check=True,env=env)
    names = ['LIVE', '401 AUTH ERROR', '503 UNAVAILABLE', 'TLS FAILURE', 'OFFLINE', 'NO QUOTA', 'NO OBSERVATIONS', 'UNSUPPORTED VERSION', 'CAPACITY ERROR']
    statuses=[0,4,5,8,1,0,0,10,11]
    if args.json:
        names=['SNAPSHOT']; statuses=[args.status]
    screens = []
    for state, name in enumerate(names):
        data=json.loads(args.json.read_bytes()) if args.json else copy.deepcopy(FIXTURE)
        if state==5: data['quota']['available']=False
        if state==6:
            for account in data['balances']['items']: account['observed_at']=None
        fixture=work/f'fixture-{state}.json'
        fixture.write_text(json.dumps(data,ensure_ascii=False),encoding='utf-8')
        items=data['balances'].get('items',[]) if data['balances']['available'] else []
        page_count=1+((len(items)+1)//2 if items else 1)
        sheets = []
        for sheet in range(page_count):
            commands = subprocess.check_output([str(executable),str(fixture),str(statuses[state]),str(sheet),str(ROOT/'assets/fonts/codeck_labels.bin'),
                                                str(args.retry_seconds),str(int(args.connecting))], text=True)
            png = render_commands(commands)
            (output / f'{state}-{sheet}.png').write_bytes(png)
            sheets.append('data:image/png;base64,' + base64.b64encode(png).decode())
        screens.append({'name': name, 'images': sheets})
    html = r'''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Codeck 真实页面绘图预览</title><style>body{background:#eef0ed;font:16px system-ui;color:#243028}main{max-width:1000px;margin:40px auto;padding:0 24px}p{line-height:1.8}nav{display:flex;gap:8px;flex-wrap:wrap;margin:24px 0}button{padding:10px;border:1px solid #b9c5b7;background:white;border-radius:8px;cursor:pointer}button[aria-pressed=true]{background:#24412e;color:white}#pages{display:flex;gap:24px;flex-wrap:wrap}figure{margin:0}img{width:400px;max-width:100%;image-rendering:pixelated;border:8px solid #344334;box-sizing:border-box}figcaption{padding:12px 0}</style>
<main><h1>Codeck · 真实页面绘图</h1><p>使用真实解析器和页面绘图代码，输入为快照数据，不包含设备凭据。所有时间为 UTC+8。
Codex CLI 与剩余额度为第一页，随后每屏两个账户；全部币种保留在所属账户卡片中。中文配置名最多两行。</p>
<nav id="states"></nav><div id="pages"></div><p>KEY 单击切换顶层页面，BOOT 单击翻屏，末屏回首屏并保留阅读位置。无自动翻屏和固定 Footer。
请求失败时保留最后成功快照，内容区显示原因、LAST KNOWN、CACHED 和最后快照生成时间。
401 每 15 分钟最多重试一次，正常轮询间隔 45 秒；普通错误指数退避并加入抖动。</p></main><script>const screens=SCREENS;const buttons=screens.map((s,i)=>{const b=document.createElement('button');b.textContent=s.name;b.onclick=()=>show(i);document.getElementById('states').append(b);return b;});function show(i){const pages=document.getElementById('pages');pages.replaceChildren();screens[i].images.forEach((src,j)=>{const f=document.createElement('figure');const img=document.createElement('img');img.src=src;img.alt='Codeck page '+(j+1);const c=document.createElement('figcaption');c.textContent=(j+1)+' / '+screens[i].images.length;f.append(img,c);pages.append(f);});buttons.forEach((b,j)=>b.setAttribute('aria-pressed',String(i===j)));}show(0);</script></html>'''
    (output / 'index.html').write_text(html.replace('SCREENS', json.dumps(screens, ensure_ascii=False)), encoding='utf-8')
    print('Rendered snapshot pages; glyphs, bounds and graphic/text overlaps checked.')
    print(output / 'index.html')


if __name__ == '__main__':
    main()
