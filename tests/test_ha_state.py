"""Compile the production HA parser and cJSON, then test invalid sensor states."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--zig', required=True, help='Path to zig.exe / zig')
    args = parser.parse_args()
    output = ROOT / 'build' / 'ha_tests'
    output.mkdir(parents=True, exist_ok=True)
    library = output / ('ha_state.dll' if sys.platform == 'win32' else 'ha_state.so')
    cjson = ROOT / 'managed_components' / 'espressif__cjson' / 'cJSON'
    command = [args.zig, 'cc', '-shared', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
               '-DCJSON_NESTING_LIMIT=8', '-I' + str(cjson),
               str(ROOT / 'main' / 'ha_state.c'), str(cjson / 'cJSON.c'), '-o', str(library)]
    if sys.platform == 'win32':
        exports = output / 'exports.def'
        exports.write_text('EXPORTS\nha_state_parse\n', encoding='utf-8')
        command.append(str(exports))
    else:
        command.extend(['-fPIC', '-lm'])
    env = os.environ.copy()
    env['ZIG_GLOBAL_CACHE_DIR'] = str(output / 'zig_cache')
    env['ZIG_LOCAL_CACHE_DIR'] = str(output / 'zig_local')
    subprocess.run(command, check=True, env=env)
    dll = ctypes.CDLL(str(library))
    parse = dll.ha_state_parse
    parse.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_bool, ctypes.POINTER(ctypes.c_float)]
    parse.restype = ctypes.c_bool
    entity = 'sensor.test_temperature'
    count = 0

    def check(body, expected, humidity=False, result=None):
        nonlocal count
        value = ctypes.c_float(1234)
        actual = parse(body.encode(), entity.encode(), humidity, ctypes.byref(value))
        assert actual == expected, (body, actual, expected)
        if result is not None:
            assert abs(value.value - result) < 0.001, value.value
        if not expected:
            assert value.value == 1234, 'Failed parse changed output'
        count += 1

    def state(value):
        return json.dumps({'entity_id': entity, 'state': value})

    for text, number in [('26.5', 26.5), ('-4.2', -4.2), ('0', 0), (' 28.7 ', 28.7)]:
        check(state(text), True, result=number)
    for text in ['0', '100', '31.0']:
        check(state(text), True, humidity=True, result=float(text))
    for text in ['unavailable', 'unknown', '', ' ', '26C', '26.5x', 'nan', 'inf', '-inf', '1e999', '1e-999', '-101', '151']:
        check(state(text), False)
    for text in ['-0.1', '100.1']:
        check(state(text), False, humidity=True)
    for value in [None, True, 26.5, [], {}]:
        check(state(value), False)
    for body in ['', '{', '[]', '{}', '{"state":"26"}',
                 '{"entity_id":"other","state":"26"}', state('26') + 'garbage',
                 state('26') + '{}', state('26')[:-1]]:
        check(body, False)
    check(state('26') + '\n', True, result=26)
    value = ctypes.c_float()
    assert not parse(None, entity.encode(), False, ctypes.byref(value))
    assert not parse(state('26').encode(), None, False, ctypes.byref(value))
    assert not parse(state('26').encode(), entity.encode(), False, None)
    print(f'HA state parser: {count + 3} cases passed')


if __name__ == '__main__':
    main()
