"""Reframe pinned offline JFSHAP1 evidence for private JFSHR2 reader tests.
Does not qualify a resource for shaping or prove companion raster coverage.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
PROFILE = b'JFSHR2/NotoSC-2.002/LTR/latn-default,latn-ZHS,hani-default,hani-ZHS,bopo-ZHT/ccmp,locl,liga,kern,mark/flags-zero/v1'

def reframe(data, source_hash, glyph_count):
    magic, version, header, total, selections, lookups, so, ro, lo = struct.unpack_from('<8sHHIIIIII', data)
    if magic != b'JFSHAP1\0' or version != 1 or header != 36 or total != len(data):
        raise ValueError('unexpected spike envelope')
    if so != 36 or ro != so + selections * 16 or (lo-ro) % 8:
        raise ValueError('unexpected spike directories')
    references = (lo-ro)//8
    for i in range(lookups):
        old = struct.unpack_from('<BBHHII', data, lo+i*14)
        # Original spike directories are 14 bytes. Production adds explicit flags.
        if i == 0:
            payload_start = old[4]
    directory_bytes = bytearray()
    delta = 112-36+2*lookups
    for i in range(lookups):
        table, kind, count, original, offset, length = struct.unpack_from('<BBHHII', data, lo+i*14)
        directory_bytes += struct.pack('<BBHHHII',table,kind,count,original,0,offset+delta,length)
    body = data[36:lo] + directory_bytes + data[payload_start:]
    identity = bytes.fromhex(source_hash)
    profile = hashlib.sha256(PROFILE).digest()
    header = struct.pack('<8sHHI32s32sIIIIIIII', b'JFSHR2\0\0',2,112,112+len(body),identity,profile,selections,references,lookups,glyph_count,zlib.crc32(body),0,0,0)
    return header+body

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--spike-dir',type=Path,required=True)
    parser.add_argument('--gid-report',type=Path,required=True)
    parser.add_argument('--output-dir',type=Path,required=True)
    args=parser.parse_args()
    report=json.loads(args.gid_report.read_text())
    args.output_dir.mkdir(parents=True,exist_ok=True)
    manifest={'profileDescriptor':PROFILE.decode(),'profileSha256':hashlib.sha256(PROFILE).hexdigest(),'faces':{}}
    for name,item in report['faces'].items():
        source=args.spike_dir/f'noto-sans-sc-{name.lower()}-shaping.jfshape'
        output=reframe(source.read_bytes(),item['sha256'],item['glyphCount'])
        target=args.output_dir/f'{name.lower()}.jfshr2'
        target.write_bytes(output)
        manifest['faces'][name]={'sourceSha256':item['sha256'],'glyphCount':item['glyphCount'],'fixtureSha256':hashlib.sha256(output).hexdigest(),'bytes':len(output),'spikeSha256':hashlib.sha256(source.read_bytes()).hexdigest()}
    (args.output_dir/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
if __name__=='__main__': main()
