"""Test production UI with simulated inputs and render the same code for review."""
import argparse
import copy
import html
import json
import os
from pathlib import Path
import subprocess
from test_codeck import FIXTURE
from render_rlcd import render_commands

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', default=str(ROOT / 'build/portal_toolchain/ziglang/zig.exe'))
    args = parser.parse_args()
    work = ROOT / 'build/ui_tests'
    work.mkdir(parents=True, exist_ok=True)
    (work / 'driver').mkdir(exist_ok=True)
    (work / 'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n', encoding='utf-8')
    (work / 'esp_timer.h').write_text('#include <stdint.h>\nint64_t esp_timer_get_time(void);\n', encoding='utf-8')
    (work / 'esp_system.h').write_text('const char *esp_get_idf_version(void);\n', encoding='utf-8')
    (work / 'driver/gpio.h').write_text('''#include <stdint.h>
#include "esp_err.h"
typedef struct {uint64_t pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type;} gpio_config_t;
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
esp_err_t gpio_config(const gpio_config_t *config);
int gpio_get_level(int pin);
''', encoding='utf-8')
    env = os.environ.copy()
    env['ZIG_GLOBAL_CACHE_DIR'] = str(work / 'zig_cache')
    env['ZIG_LOCAL_CACHE_DIR'] = str(work / 'zig_local')
    cjson = ROOT / 'managed_components/espressif__cjson/cJSON'
    exe = work / 'ui.exe'
    modules = ['ui', 'ui_pages', 'ui_chrome', 'display_time', 'button', 'codeck_page', 'codeck_state', 'codeck_label', 'brand_icons']
    subprocess.run([args.zig, 'cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-DCODECK_FONT_HOST', '-DCJSON_NESTING_LIMIT=8',
                    '-I' + str(work), '-I' + str(ROOT / 'main'), '-I' + str(cjson), str(ROOT / 'tests/ui_harness.c'),
                    *[str(ROOT / f'main/{name}.c') for name in modules], str(cjson / 'cJSON.c'), '-o', str(exe)], check=True, env=env)
    data = copy.deepcopy(FIXTURE)
    data['balances']['items'].append(copy.deepcopy(data['balances']['items'][1]))
    fixture = work / 'fixture.json'
    fixture.write_text(json.dumps(data, ensure_ascii=False), encoding='utf-8')
    font = ROOT / 'assets/fonts/codeck_labels.bin'
    subprocess.run([str(exe), str(fixture), str(font), 'test'], check=True)

    output = ROOT / 'docs/ui-preview'
    output.mkdir(parents=True, exist_ok=True)
    cases = [('sensor',0,0,0,0), ('codex',1,0,0,0), ('balances',1,1,0,0), ('balances-last',1,2,0,0),
             ('network',2,0,0,0), ('system',2,1,0,0), ('offline',2,0,1,0), ('system-unknown',2,1,1,0),
             ('weak-wifi',1,0,2,0), ('unknown-signal',1,0,3,0), ('no-snapshot',1,0,4,1),
             ('setup',0,0,5,0), ('cached',1,1,0,4), ('connecting',1,0,6,0), ('wifi-connecting',2,0,7,0)]
    extreme = copy.deepcopy(data)
    extreme['balances']['items'] = [copy.deepcopy(data['balances']['items'][0]) for _ in range(8)]
    for account in extreme['balances']['items']:
        account['name'] = '很长的中文账户名称用于检查两行自动换行与末尾省略' * 2
        # Contract: UTF-8 names are limited to 127 bytes.
        account['name'] = account['name'][:40]
        account['amounts'] = [{'currency': code, 'amount': '9' * 58 + '.995'} for code in ['LONGCUR', 'CNY', 'USD', 'EUR']]
    # Eight maximum-size accounts exceed the existing 4 KiB transport limit;
    # render two extreme cards while the eight-account navigation uses smaller fixtures.
    extreme['balances']['items'] = extreme['balances']['items'][:2]
    extreme_path = work / 'extreme.json'
    extreme_path.write_text(json.dumps(extreme, ensure_ascii=False), encoding='utf-8')
    cases.append(('long-names-and-amounts',1,1,0,4))
    fixtures = {'long-names-and-amounts': extreme_path}
    for count in [0,1,2,8]:
        variant = copy.deepcopy(data)
        variant['balances']['items'] = [copy.deepcopy(data['balances']['items'][1]) for _ in range(count)]
        for i, account in enumerate(variant['balances']['items']):
            account['name'] = f'ACCOUNT {i+1}'
        path = work / f'accounts-{count}.json'
        path.write_text(json.dumps(variant, ensure_ascii=False), encoding='utf-8')
        for screen in range(1, 1 + (max(count,1)+1)//2):
            name = f'accounts-{count}-screen-{screen}'
            fixtures[name] = path
            cases.append((name,1,screen,0,0))
    variant = copy.deepcopy(data)
    variant['service']['running_tasks'] = 4294967295
    variant['quota']['windows'][0]['used_percent'] = 0
    variant['quota']['windows'][1]['used_percent'] = 100
    path = work / 'quota-limits.json'
    path.write_text(json.dumps(variant, ensure_ascii=False), encoding='utf-8')
    fixtures['quota-limits'] = path
    cases.append(('quota-limits',1,0,0,4))
    variant = copy.deepcopy(data)
    variant['balances']['items'][0]['name'] = 'Mixed 中文 name ' + 'LONG' * 26
    variant['balances']['items'][0]['amounts'] = [{'currency':'CNY','amount':'-12345.995'},{'currency':'USD','amount':'0.00'}]
    variant['balances']['items'][1]['amounts'] = []
    path = work / 'two-currencies.json'
    path.write_text(json.dumps(variant, ensure_ascii=False), encoding='utf-8')
    fixtures['two-currencies'] = path
    cases.append(('two-currencies',1,1,0,0))
    figures = []
    for name, page, screen, scenario, status in cases:
        source = fixtures.get(name, fixture)
        commands = subprocess.check_output([str(exe), str(source), str(font), str(page), str(screen), str(scenario), str(status)], text=True)
        visible = [line.split(' ',4)[4] for line in commands.splitlines() if line.startswith('T ')]
        assert 'UTC+8' in visible and not any('12S AUTO' in line for line in visible)
        if name == 'quota-limits':
            assert '100%' in visible and '0%' in visible and 'LAST RUN 4294967295' in visible
        if name == 'long-names-and-amounts':
            assert visible.count('...') == 2
        if name == 'two-currencies':
            assert 'CNY -12346.00' in visible and 'USD 0.00' in visible
        if name == 'offline':
            assert '192.168.31.123' not in visible and '--.--.--.--' in visible
        (output / f'{name}.png').write_bytes(render_commands(commands))
        figures.append(f'<figure><img src="{name}.png" alt="{html.escape(name)}"><figcaption>{html.escape(name)}</figcaption></figure>')
    (output / 'index.html').write_text('''<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>RLCD 统一 UI 生产预览</title>
<style>body{background:#eef0ed;color:#243028;font:16px system-ui;margin:24px}main{max-width:1300px;margin:auto}.screens{display:flex;flex-wrap:wrap;gap:24px}figure{margin:0}img{width:400px;max-width:100%;image-rendering:pixelated;border:8px solid #344334;box-sizing:border-box}figcaption{padding:10px 0 24px}p{line-height:1.7}</style>
<main><h1>统一 UI · 生产代码预览</h1><p>400 × 300 单色屏，时间统一 UTC+8。KEY 单击切页；BOOT 单击翻屏，末屏回首屏，保留各页位置。
仅网络/系统页长按 KEY 3 秒配网。以下使用合成数据与实际 UI、按键、解析和绘图代码，不包含设备凭据。</p><div class="screens">''' + ''.join(figures) + '</div></main></html>', encoding='utf-8')
    print(f'{len(cases)} production UI previews passed glyph, bounds and overlap checks: {output / "index.html"}')


if __name__ == '__main__':
    main()
