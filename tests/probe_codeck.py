"""Read-only public snapshot probe; secrets never enter arguments or output."""
import argparse
import json
from pathlib import Path
import socket
import ssl
import urllib.error
import urllib.request

ROOT=Path(__file__).resolve().parents[1]
HOST='codeck.wangj.de'
URL='https://'+HOST+'/api/device/v1/snapshot'

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,*args,**kwargs): return None

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check-auth',action='store_true',help='Also check one invalid-credential GET')
    parser.add_argument('--ca-file',type=Path,help='Verify using only this PEM root bundle (e.g. ESP-IDF cacrt_all.pem)')
    args=parser.parse_args()
    try:
        socket.getaddrinfo(HOST,443,type=socket.SOCK_STREAM)
        print('DNS: OK')
        context=ssl.create_default_context(cafile=str(args.ca_file) if args.ca_file else None)
        with socket.create_connection((HOST,443),timeout=20) as tcp:
            with context.wrap_socket(tcp,server_hostname=HOST) as tls:
                print('TLS: verified certificate chain and hostname;',tls.version())
        key=(ROOT/'codeck.key').read_text(encoding='utf-8-sig').strip()
    except Exception:
        print('DNS, verified TLS or local key setup failed; no credential details logged')
        return 1
    opener=urllib.request.build_opener(NoRedirect,urllib.request.HTTPSHandler(context=context))
    def get(credential):
        request=urllib.request.Request(URL,headers={'Accept':'application/json','Authorization':'Bearer '+credential,
                                                    'User-Agent':'RLCD-Codeck/1'},method='GET')
        try:
            with opener.open(request,timeout=25) as response:
                body=response.read(4097)
                if len(body)>4096:
                    print('GET: response exceeds 4 KiB'); return 0,None
                return response.status,body
        except urllib.error.HTTPError as error: return error.code,None
        except Exception: return 0,None
    status,body=get(key)
    print('Credentialed snapshot GET:',status or 'transport failure')
    if status==200:
        try:
            snapshot=json.loads(body)
            print('Response bytes:',len(body),'schema:',snapshot.get('schema_version'))
            print('Quota windows:',len(snapshot.get('quota',{}).get('windows',[])),
                  'balance configs:',len(snapshot.get('balances',{}).get('items',[])))
            (ROOT/'build').mkdir(exist_ok=True)
            (ROOT/'build/codeck-live.json').write_bytes(body)
        except Exception:
            print('GET: invalid JSON'); return 1
    if args.check_auth:
        invalid,_=get('invalid-device-credential-for-probe')
        print('Invalid-credential snapshot GET:',invalid or 'transport failure')
        if invalid!=401: return 1
    return 0 if status==200 else 1

if __name__=='__main__': raise SystemExit(main())
