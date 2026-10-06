"""Production parser, exact decimal formatting, last-known state and retry tests."""
import argparse
import copy
import ctypes as C
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

class Window(C.Structure):
    _fields_ = [('valid',C.c_bool),('used',C.c_double),('reset',C.c_char*40)]
class Amount(C.Structure):
    _fields_ = [('currency',C.c_char*8),('amount',C.c_char*64)]
class Account(C.Structure):
    _fields_ = [('provider',C.c_char*32),('name',C.c_char*128),('observed_at',C.c_char*40),
                ('observed',C.c_bool),('stale',C.c_bool),('count',C.c_uint),('amounts',Amount*4)]
class Snapshot(C.Structure):
    _fields_ = [('has_snapshot',C.c_bool),('service',C.c_bool),('cli',C.c_bool),('scheduler',C.c_bool),
                ('tasks',C.c_uint),('generated',C.c_char*40),('quota_observed',C.c_char*40),
                ('quota',C.c_bool),('balances',C.c_bool),('windows',Window*2),('count',C.c_uint),
                ('accounts',Account*8),('status',C.c_int),('received',C.c_int64),('revision',C.c_uint32),
                ('retry_seconds',C.c_uint),('fetching',C.c_bool)]

FIXTURE = {
    'schema_version':1, 'generated_at':'2030-01-01T12:00:00Z',
    'service':{'available':True,'codex_cli_available':True,'scheduler_enabled':True,'running_tasks':1},
    'quota':{'available':True,'observed_at':'2030-01-01T11:59:30Z','windows':[
        {'kind':'primary','used_percent':42,'resets_at':'2030-01-01T14:00:00Z'},
        {'kind':'secondary','used_percent':18,'resets_at':'2030-01-07T12:00:00Z'}]},
    'balances':{'available':True,'items':[
        {'provider':'deepseek','name':'工作账户','amounts':[{'currency':'CNY','amount':'123.4500'},
         {'currency':'USD','amount':'2.10'},{'currency':'EUR','amount':'0.999'}],
         'observed_at':'2030-01-01T11:55:00Z','stale':False},
        {'provider':'openrouter','name':'OpenRouter','amounts':[{'currency':'USD','amount':'9.97458636'}],
         'observed_at':'2030-01-01T11:50:00Z','stale':True}]}}

def build(zig):
    out=ROOT/'build/codeck_tests'; out.mkdir(parents=True,exist_ok=True)
    cjson=ROOT/'managed_components/espressif__cjson/cJSON'
    dll=out/'state.dll'
    exports=out/'exports.def'
    exports.write_text('EXPORTS\ncodeck_state_parse\ncodeck_amount_format\ncodeck_state_failure\ncodeck_retry_delay\n',encoding='utf-8')
    env=os.environ.copy(); env['ZIG_GLOBAL_CACHE_DIR']=str(out/'zig_cache'); env['ZIG_LOCAL_CACHE_DIR']=str(out/'zig_local')
    subprocess.run([zig,'cc','-shared','-std=c11','-Wall','-Wextra','-Werror','-O1',
                    '-DCJSON_NESTING_LIMIT=8','-I'+str(cjson),str(ROOT/'main/codeck_state.c'),
                    str(cjson/'cJSON.c'),str(exports),'-o',str(dll)],env=env,check=True)
    return C.CDLL(str(dll))

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--zig',default=str(ROOT/'build/portal_toolchain/ziglang/zig.exe'))
    args=parser.parse_args(); lib=build(args.zig)
    parse=lib.codeck_state_parse; parse.argtypes=[C.c_char_p,C.c_size_t,C.POINTER(Snapshot)]; parse.restype=C.c_int
    fmt=lib.codeck_amount_format; fmt.argtypes=[C.c_char_p,C.c_char_p,C.c_size_t]; fmt.restype=C.c_bool
    retry=lib.codeck_retry_delay; retry.argtypes=[C.c_int,C.c_uint,C.c_uint32]; retry.restype=C.c_uint
    fail=lib.codeck_state_failure; fail.argtypes=[C.POINTER(Snapshot),C.c_int]
    count=0
    def check(data,expected=0):
        nonlocal count
        body=json.dumps(data,ensure_ascii=False).encode() if isinstance(data,dict) else data
        result=Snapshot(); actual=parse(body,len(body),C.byref(result))
        assert actual==expected,(actual,expected)
        count+=1; return result
    valid=check(FIXTURE)
    assert valid.has_snapshot and valid.count==2 and valid.windows[0].used==42 and valid.windows[1].used==18
    assert valid.accounts[0].count==3 and valid.accounts[0].name.decode()=='工作账户'
    assert valid.accounts[0].amounts[0].amount==b'123.4500' and valid.accounts[1].stale
    for value,expected in [('9.97458636','9.97'),('0.999','1.00'),('123.4500','123.45'),
                           ('-0.005','-0.01'),('-0.004','0.00'),('999.995','1000.00'),('0','0.00'),
                           ('0002.1','2.10'),('999999999999999999.995','1000000000000000000.00')]:
        out=C.create_string_buffer(80); assert fmt(value.encode(),out,len(out)) and out.value.decode()==expected; count+=1
    for value in ['','1e3','NaN','1.','+1','1.2x',' 2','--1']:
        out=C.create_string_buffer(80); assert not fmt(value.encode(),out,len(out)); count+=1
    assert not fmt(b'123.45',C.create_string_buffer(3),3); count+=1
    data=copy.deepcopy(FIXTURE); data['quota']['available']=False
    state=check(data); assert not any(w.valid for w in state.windows) and state.count==2
    data=copy.deepcopy(FIXTURE); data['quota']['windows']=[]
    assert not any(w.valid for w in check(data).windows)
    data=copy.deepcopy(FIXTURE); data['quota']['observed_at']=None
    assert not any(w.valid for w in check(data).windows)
    data=copy.deepcopy(FIXTURE); data['balances']['items'][0]['observed_at']=None
    state=check(data); assert not state.accounts[0].observed and state.accounts[0].amounts[0].amount==b'123.4500'
    data=copy.deepcopy(FIXTURE); data['balances']['items'][0]['amounts']=[]
    assert check(data).accounts[0].count==0
    data=copy.deepcopy(FIXTURE); data['balances']['available']=False
    assert not check(data).balances
    data=copy.deepcopy(FIXTURE); data['balances']['items'][0]['amounts'][0]['currency']='USDT'; check(data)
    data=copy.deepcopy(FIXTURE); data['balances']['items'][0]['amounts'][0]['amount']='1\0.23'; check(data,9)
    data=copy.deepcopy(FIXTURE); data['balances']['items'][0]['name']='Work\0secret'; check(data,9)
    data=copy.deepcopy(FIXTURE); data['schema_version']=2; check(data,10)
    data=copy.deepcopy(FIXTURE); data['schema_version']=1.5; check(data,9)
    for value in [-1,101,'42',None,True]:
        data=copy.deepcopy(FIXTURE); data['quota']['windows'][0]['used_percent']=value; check(data,9)
    for value in [-1,1.5,'1',None,True]:
        data=copy.deepcopy(FIXTURE); data['service']['running_tasks']=value; check(data,9)
    for value in ['2030-02-30T12:00:00Z','2030-01-01T25:00:00Z','not a date']:
        data=copy.deepcopy(FIXTURE); data['generated_at']=value; check(data,9)
    data=copy.deepcopy(FIXTURE); data['generated_at']='2030-01-01T12:00:00.123+00:00'; check(data)
    for section,key in [('balances','items'),('quota','windows')]:
        data=copy.deepcopy(FIXTURE); data[section][key]=None; check(data,9)
    data=copy.deepcopy(FIXTURE); data['balances']['items']*=5; check(data,11)
    data=copy.deepcopy(FIXTURE); data['balances']['items'][0]['amounts']*=2; check(data,11)
    data=copy.deepcopy(FIXTURE); data['balances']['items'][0]['name']='x'*128; check(data,9)
    body=json.dumps(FIXTURE).encode()
    for invalid in [b'',body[:-1],body+b'{}',body+b'x',body.replace(b'2030',b'20\x0030',1)]: check(invalid,9)
    check(b'x'*4097,11)
    check(body+b' '*(4096-len(body)))
    for status in [4,5,6,7,8,9,10,11,1]:
        last=Snapshot.from_buffer_copy(valid); last.received=123456
        old=(bytes(last.generated),bytes(last.windows),bytes(last.accounts),last.received)
        fail(C.byref(last),status)
        assert last.status==status and last.has_snapshot and old==(bytes(last.generated),bytes(last.windows),bytes(last.accounts),last.received)
        count+=1
    assert retry(0,20,123)==45000 and retry(4,20,123)==900000
    delays=[retry(7,n,0) for n in range(1,10)]
    assert delays==[10000,20000,40000,60000,60000,60000,60000,60000,60000]
    for status in [7,8,12,13]: assert 60000<=retry(status,32,0xffffffff)<=66000
    assert 300000<=retry(5,32,0xffffffff)<=330000; count+=3
    live=ROOT/'build/codeck-live.json'
    if live.exists():
        real=check(live.read_bytes()); print('Live HTTPS response parsed:',real.count,'accounts;',sum(w.valid for w in real.windows),'valid windows')
    (ROOT/'build/codeck-fixture.json').write_text(json.dumps(FIXTURE,ensure_ascii=False),encoding='utf-8')
    print('Codeck production state tests:',count,'cases passed')
    out=ROOT/'build/codeck_tests'
    (out/'freertos').mkdir(exist_ok=True)
    for name in ['esp_err.h','esp_crt_bundle.h','esp_http_client.h','esp_random.h','esp_timer.h',
                 'esp_log.h','esp_tls_errors.h','freertos/FreeRTOS.h','freertos/queue.h','freertos/task.h']:
        (out/name).write_text('#include "codeck_transport_stubs.h"\n',encoding='utf-8')
    cjson=ROOT/'managed_components/espressif__cjson/cJSON'
    env=os.environ.copy(); env['ZIG_GLOBAL_CACHE_DIR']=str(out/'zig_cache'); env['ZIG_LOCAL_CACHE_DIR']=str(out/'zig_local')
    command=[args.zig,'cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable',
             '-DCJSON_NESTING_LIMIT=8','-I'+str(out),'-I'+str(ROOT/'tests'),'-I'+str(cjson),'-I'+str(ROOT/'main'),
             str(ROOT/'tests/codeck_transport_harness.c'),str(ROOT/'main/codeck_state.c'),str(cjson/'cJSON.c'),
             '-o',str(out/'transport.exe')]
    subprocess.run(command,env=env,check=True)
    subprocess.run([str(out/'transport.exe'),str(ROOT/'build/codeck-fixture.json')],check=True)

if __name__=='__main__': main()
