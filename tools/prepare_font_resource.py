"""Prepare canonical JFFONT2 scalar bitmaps with original hhea/hmtx metrics.

Offline preparation only. The runtime parser/scale policy is in Render Core.
Requires the versions in font_resource_requirements.txt; no host text callbacks.
"""
import argparse
import hashlib
import io
import json
import struct
import zlib
from pathlib import Path

import PIL
from PIL import Image, ImageDraw, ImageFont, features
import fontTools
from fontTools.ttLib import TTFont

HEADER_BYTES = 128
GLYPH_BYTES = 40

def prepare(font_bytes, characters, size, bits=4, max_bytes=8*1024*1024):
    if not 1 <= size <= 4096 or bits not in (1,2,4,8):
        raise ValueError('unsupported raster size/coverage depth')
    cps = sorted(set(ord(c) for c in characters if c not in '\r\n\t'))
    if not cps or len(cps) > 65535:
        raise ValueError('empty or oversized coverage')
    source_sha = hashlib.sha256(font_bytes).digest()
    with TTFont(io.BytesIO(font_bytes), lazy=False) as source:
        if 'fvar' in source:
            raise ValueError('instantiate variable fonts before preparing this profile')
        units = int(source['head'].unitsPerEm)
        hhea = source['hhea']
        ascent, descent, gap = int(hhea.ascent), -int(hhea.descent), int(hhea.lineGap)
        weight = int(source['OS/2'].usWeightClass)
        # This first profile admits upright static faces only. Do not silently
        # drop style information before common face selection is implemented.
        if source['OS/2'].fsSelection & ((1 << 0) | (1 << 9)) or source['head'].macStyle & 2:
            raise ValueError('italic/oblique faces are not supported by this profile')
        if not 1 <= units <= 16384 or not 1 <= weight <= 1000:
            raise ValueError('invalid font units/weight')
        if ascent < 0 or descent < 0 or max(ascent,descent,abs(gap)) > 1<<20 or not 0 <= ascent+descent+gap <= 1<<20:
            raise ValueError('unsupported original vertical metrics')
        cmap = source.getBestCmap() or {}
        font = ImageFont.truetype(io.BytesIO(font_bytes), size=size, layout_engine=ImageFont.Layout.BASIC)
        records, coverage, report_glyphs = [], bytearray(), []
        for cp in cps:
            name = cmap.get(cp)
            if name is None or not (0 <= cp <= 0x10ffff and not 0xd800 <= cp <= 0xdfff):
                raise ValueError(f'font has no declared glyph for U+{cp:04X}')
            glyph_id = source.getGlyphID(name)
            advance = int(source['hmtx'].metrics[name][0])
            if not 1 <= glyph_id <= 65535 or not 0 <= advance <= 1<<20:
                raise ValueError('unsupported glyph identity/advance')
            # Baseline-left anchor preserves negative X and above-baseline Y.
            left,top,right,bottom = font.getbbox(chr(cp), anchor='ls')
            width,height = right-left,bottom-top
            if width < 0 or height < 0 or width > 4096 or height > 4096:
                raise ValueError('glyph raster exceeds profile bounds')
            if HEADER_BYTES + len(cps)*GLYPH_BYTES + len(coverage) + ((width*bits+7)//8)*height > max_bytes:
                raise ValueError('prepared resource exceeds byte budget')
            image = None
            if width and height:
                image = Image.new('L',(width,height),0)
                ImageDraw.Draw(image).text((-left,-top),chr(cp),font=font,fill=255,anchor='ls')
                ink = image.getbbox()
                if ink:
                    left += ink[0]; top += ink[1]
                    image = image.crop(ink)
                else:
                    image = None
            if image is None:
                width=height=left=top=0
            else:
                width,height=image.size
            if abs(left)>1<<20 or abs(top)>1<<20:
                raise ValueError('glyph bearing exceeds profile bounds')
            stride=(width*bits+7)//8
            packed=bytearray(stride*height)
            if image is not None:
                pixels=image.load(); levels=(1<<bits)-1
                for y in range(height):
                    for x in range(width):
                        value=(pixels[x,y]*levels+127)//255
                        bit=x*bits
                        packed[y*stride+bit//8] |= value << (8-bits-bit%8)
            record=struct.pack('<IIiiiHHIIII',cp,glyph_id,advance,left,top,width,height,
                stride,len(coverage),len(packed),0)
            assert len(record)==GLYPH_BYTES
            records.append(record);coverage.extend(packed)
            if HEADER_BYTES+len(records)*GLYPH_BYTES+len(coverage)>max_bytes:
                raise ValueError('prepared resource exceeds byte budget')
            report_glyphs.append(dict(codepoint=cp,glyphId=glyph_id,advance=advance,
                bearingX=left,bearingY=top,width=width,height=height))
    bitmap_offset=HEADER_BYTES+len(records)*GLYPH_BYTES
    total=bitmap_offset+len(coverage)
    header=bytearray(HEADER_BYTES);header[:8]=b'JFFONT2\0'
    struct.pack_into('<HHHHIIIIIHHHBBiiiII',header,8,
        2,HEADER_BYTES,GLYPH_BYTES,1,total,len(records),HEADER_BYTES,bitmap_offset,len(coverage),
        units,size,weight,bits,0,ascent,descent,gap,0,0)
    header[64:96]=source_sha
    struct.pack_into('<IH',header,100,1,1) # Raster profile, original hhea metrics.
    blob=header+b''.join(records)+coverage
    struct.pack_into('<I',blob,96,zlib.crc32(blob))
    report=dict(format='JFFONT2',profile='scalar-bitmap-v1',sourceSha256=source_sha.hex(),
        resourceSha256=hashlib.sha256(blob).hexdigest(),resourceBytes=len(blob),
        unitsPerEm=units,rasterPixelSize=size,weight=weight,coverageBits=bits,
        metricsSource='hhea',ascent=ascent,descent=descent,lineGap=gap,
        advanceSource='original hmtx units, not hinted/rounded Pillow string width',
        raster=dict(pillow=PIL.__version__,fontTools=fontTools.__version__,
            freeType=features.version_module('freetype2'),layout='BASIC',anchor='ls'),glyphs=report_glyphs)
    return bytes(blob),report

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--font',type=Path,required=True)
    p.add_argument('--characters',type=Path,required=True,help='UTF-8 coverage text; layout TAB/CR/LF are ignored')
    p.add_argument('--size',type=int,required=True)
    p.add_argument('--bits',type=int,default=4,choices=(1,2,4,8))
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True)
    a=p.parse_args()
    if a.output.exists() or a.report.exists() or a.output.resolve()==a.report.resolve():
        p.error('refuse to overwrite outputs or alias resource and report')
    blob,report=prepare(a.font.read_bytes(),a.characters.read_text(encoding='utf8'),a.size,a.bits)
    with a.output.open('xb') as f:f.write(blob)
    with a.report.open('x',encoding='utf8') as f:f.write(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('resourceSha256','resourceBytes','ascent','descent','lineGap')}))

if __name__=='__main__':
    main()
