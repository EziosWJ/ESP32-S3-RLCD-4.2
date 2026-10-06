from pathlib import Path
import urllib.request,gzip,ssl
from pip._vendor import certifi
context=ssl.create_default_context(cafile=certifi.where())
root=Path('assets/fonts')
url='https://unifoundry.com/pub/unifont/unifont-18.0.01/font-builds/unifont-18.0.01.hex.gz'
data=urllib.request.urlopen(url,timeout=40,context=context).read()
Path('build/unifont.hex.gz').write_bytes(data)
font={int(line.split(':')[0],16):bytes.fromhex(line.split(':')[1]) for line in gzip.decompress(data).decode().splitlines() if ':' in line}
result=bytearray()
for cp in list(range(128))+list(range(0x3000,0xa000)):
 glyph=font.get(cp,font[0x3f])
 if len(glyph)==16: glyph=b''.join(bytes([v,0]) for v in glyph)
 assert len(glyph)==32
 result.extend(glyph)
(root/'codeck_labels.bin').write_bytes(result)
(root/'OFL-1.1.txt').write_bytes(urllib.request.urlopen('https://unifoundry.com/OFL-1.1.txt',timeout=20,context=context).read())
print('Font subset generated:',len(result),'bytes')
