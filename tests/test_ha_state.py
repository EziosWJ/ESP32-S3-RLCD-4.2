"""Compile the production HA parser and cJSON, then test sensor/template states.

The HTTP status mapping (401/403 -> AUTH_ERROR, 404/405/501 -> fallback,
other non-200 -> READ_ERROR) lives in home_assistant.c and is not exercised by
this ctypes harness; the cases below cover the /api/template envelope and the
inner rendered-object parsing only.
"""
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
        exports.write_text(
            'EXPORTS\nha_state_parse\n'
            'ha_state_parse_template_envelope\n'
            'ha_state_parse_template_number\n'
            'ha_state_parse_template_number_unit\n'
            'ha_state_parse_template_text\n',
            encoding='utf-8')
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
    env_parse = dll.ha_state_parse_template_envelope
    env_parse.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]
    env_parse.restype = ctypes.c_bool
    t_number = dll.ha_state_parse_template_number
    t_number.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_bool,
                         ctypes.POINTER(ctypes.c_float)]
    t_number.restype = ctypes.c_bool
    t_number_unit = dll.ha_state_parse_template_number_unit
    t_number_unit.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_float),
                             ctypes.c_char_p, ctypes.c_size_t]
    t_number_unit.restype = ctypes.c_bool
    t_text = dll.ha_state_parse_template_text
    t_text.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]
    t_text.restype = ctypes.c_bool
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
    count += 3

    # --- /api/template batch payload --------------------------------------
    inner = {
        't0': {'s': '26.5', 'u': ''},
        'h0': {'s': '48', 'u': ''},
        't1': {'s': '-4.2', 'u': ''},
        'cpu': {'s': '31.2', 'u': '%'},
        'mem': {'s': '60'},  # unit missing entirely
        'up': {'s': '2030-01-01T12:00:00Z', 'u': ''},
    }
    inner_body = json.dumps(inner).encode()
    envelope = json.dumps({'result': json.dumps(inner)}).encode()

    def check_env(body, expected):
        nonlocal count
        out = ctypes.create_string_buffer(4096)
        actual = env_parse(body.encode() if isinstance(body, str) else body, out, 4096)
        assert actual == expected, (body, actual, expected)
        if expected:
            assert out.value == inner_body, (out.value, inner_body)
        count += 1

    check_env(envelope, True)
    for bad in ['', '{', '[]', '{}', json.dumps({'result': None}),
                json.dumps({'result': 31}), json.dumps({'result': [1, 2]}),
                json.dumps({'other': 'x'}), envelope.decode() + 'x']:
        check_env(bad, False)

    def check_number(key, expected, humidity=False, result=None):
        nonlocal count
        number = ctypes.c_float(1234)
        actual = t_number(inner_body, key.encode(), humidity, ctypes.byref(number))
        assert actual == expected, (key, actual, expected)
        if result is not None:
            assert abs(number.value - result) < 0.001, number.value
        if not expected:
            assert number.value == 1234, 'Failed parse changed output'
        count += 1

    check_number('t0', True, result=26.5)
    check_number('h0', True, humidity=True, result=48)
    check_number('t1', True, result=-4.2)
    for key in ['tx', 'does_not_exist']:
        check_number(key, False)
    for bad_state in ['unknown', 'unavailable', '', ' ', '26C', 'nan', 'inf', '-101', '151']:
        fixture = json.dumps({'x': {'s': bad_state, 'u': ''}}).encode()
        number = ctypes.c_float(1234)
        assert not t_number(fixture, b'x', False, ctypes.byref(number)), bad_state
        assert number.value == 1234
        count += 1
    fixture = json.dumps({'x': {'s': '100.1', 'u': ''}}).encode()
    assert not t_number(fixture, b'x', True, ctypes.byref(number))
    count += 1
    # Entry that is not an object, and a missing "s" field.
    for fixture in [json.dumps({'x': '31'}).encode(), json.dumps({'x': {'u': '%'}}).encode()]:
        assert not t_number(fixture, b'x', False, ctypes.byref(number))
        count += 1

    def check_number_unit(key, expected, result=None, unit_expected=None):
        nonlocal count
        number = ctypes.c_float(1234)
        unit = ctypes.create_string_buffer(16)
        actual = t_number_unit(inner_body, key.encode(), ctypes.byref(number), unit, 16)
        assert actual == expected, (key, actual, expected)
        if result is not None:
            assert abs(number.value - result) < 0.001, number.value
        if unit_expected is not None:
            assert unit.value.decode() == unit_expected, unit.value
        count += 1

    check_number_unit('cpu', True, result=31.2, unit_expected='%')
    check_number_unit('mem', True, result=60.0, unit_expected='')
    check_number_unit('tx', False)

    def check_text(key, expected, result=None):
        nonlocal count
        out = ctypes.create_string_buffer(40)
        actual = t_text(inner_body, key.encode(), out, 40)
        assert actual == expected, (key, actual, expected)
        if result is not None:
            assert out.value.decode() == result, out.value
        count += 1

    check_text('up', True, result='2030-01-01T12:00:00Z')
    check_text('tx', False)
    for bad_state in ['', 'unknown']:
        fixture = json.dumps({'x': {'s': bad_state, 'u': ''}}).encode()
        out = ctypes.create_string_buffer(40)
        assert not t_text(fixture, b'x', out, 40)
        count += 1

    print(f'HA state parser: {count} cases passed')


if __name__ == '__main__':
    main()
