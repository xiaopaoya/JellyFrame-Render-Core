"""Create structurally valid late VF2 adversary from the pinned companion.
The test trusts its synthetic identities only to exercise executor refusal.
"""
from pathlib import Path
import struct,zlib,hashlib
root=Path(__file__).resolve().parents[1]/'fixtures'
b=bytearray((root/'shaping_resource/regular.jfshr2').read_bytes())
def u(p,n=4):return int.from_bytes(b[p:p+n],'little')
def put(p,v,n=4):b[p:p+n]=v.to_bytes(n,'little')
directory=112+u(80)*16+u(84)*8
changed=False
for i in range(u(88)):
 d=directory+i*16
 if u(d,1)!=2 or u(d+1,1)!=2:continue
 p=u(d+8)
 for _ in range(u(d+2,2)):
  size=u(p+2);q=p+6
  if u(p+1,1)==1:
   count=u(q);q+=4
   for row in range(count):
    q+=4
    mask=u(q,1);q+=1+mask.bit_count()*2
    v2=q;mask=u(q,1);q+=1+mask.bit_count()*2
   assert b[v2]==0
   b[v2]=4;b[v2+1:v2+1]=b'\x01\x00'
   put(p+2,size+2);put(d+12,u(d+12)+2)
   for j in range(i+1,u(88)):
    pos=directory+j*16+8;put(pos,u(pos)+2)
   put(12,len(b));put(96,zlib.crc32(b[112:]));changed=True;break
  p+=6+size
 if changed:break
assert changed
out=root/'shaping_executor';(out/'late-vf2.jfshr2').write_bytes(b)
c=bytearray((root/'shaping_executor_catalog/regular.jfcat1').read_bytes());c[80:112]=hashlib.sha256(b).digest()
(out/'late-vf2.jfcat1').write_bytes(c)
# Two ordered substitutions consume a previous ligature result.
original=(root/'shaping_resource/regular.jfshr2').read_bytes()
selections=b''.join(original[112+i*16:120+i*16]+struct.pack('<HHI',i*2,2,0) for i in range(5))
refs=(b'liga'+struct.pack('<HH',0,0)+b'liga'+struct.pack('<HH',1,0))*5
payloads=[]
for components,target in [([34,35],36),([36,37],38)]:
 body=struct.pack('<IBH',1,len(components),target)+struct.pack('<'+'H'*len(components),*components)
 payloads.append(struct.pack('<BBI',4,4,len(body))+body)
cursor=112+len(selections)+len(refs)+32
directory=b''
for i,payload in enumerate(payloads):
 directory+=struct.pack('<BBHHHII',1,4,1,i,0,cursor,len(payload));cursor+=len(payload)
body=selections+refs+directory+b''.join(payloads)
header=bytearray(original[:112]);struct.pack_into('<I',header,12,112+len(body));struct.pack_into('<IIII',header,80,5,10,2,30900);struct.pack_into('<I',header,96,zlib.crc32(body))
(out/'nested-ligature.jfshr2').write_bytes(header+body)


nested=(out/'nested-ligature.jfshr2').read_bytes()
cat=bytearray((root/'shaping_executor_catalog/regular.jfcat1').read_bytes());cat[80:112]=hashlib.sha256(nested).digest()
(out/'nested-ligature.jfcat1').write_bytes(cat)
