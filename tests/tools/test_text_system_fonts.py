"""Prepare real static faces once and compare TextSystem across built consumers."""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess

from fontTools.ttLib import TTFont

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('prepare_font_resource',ROOT/'tools/prepare_font_resource.py')
tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(tool)
TEXT='A jg\u2026\u4e2d'


def rounded(value,size,denominator):
    return (1 if value>=0 else -1)*((abs(value)*size+denominator//2)//denominator)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--font-dir',type=Path,required=True)
    p.add_argument('--probe',type=Path,nargs='+',required=True)
    p.add_argument('--work',type=Path,required=True)
    args=p.parse_args();args.work.mkdir(parents=True,exist_ok=False)
    assets=[]
    for style in ('Regular','Medium','Bold'):
        source=(args.font_dir/f'NotoSansSC-{style}.otf').read_bytes()
        with TTFont(io.BytesIO(source)) as font:
            metrics=[font['hhea'].ascent,-font['hhea'].descent,font['hhea'].lineGap]
            cmap=font.getBestCmap()
            original={ord(c):dict(glyph_id=font.getGlyphID(cmap[ord(c)]),advance=font['hmtx'].metrics[cmap[ord(c)]][0]) for c in TEXT}
            units=font['head'].unitsPerEm;weight=font['OS/2'].usWeightClass
        for size in (16,24,32):
            blob,report=tool.prepare(source,TEXT,size,4)
            path=args.work/f'{style}-{size}.jffont2';path.write_bytes(blob)
            (args.work/f'{style}-{size}.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
            assets.append(dict(path=path,weight=weight,size=size,units=units,metrics=metrics,
                               source=original,report=report))
    outputs=[];reference=None
    for probe in args.probe:
        raw=subprocess.check_output([str(probe),*[str(a['path']) for a in assets]])
        rows=json.loads(raw)
        assert len(rows)==21
        for row in rows:
            # Independent CSS search result for the three actual registered weights.
            selected_weight={350:400,400:400,450:500,500:500,550:700,700:700,900:700}[row['weight']]
            pool=[(i,a) for i,a in enumerate(assets) if a['weight']==selected_weight]
            index,asset=min(pool,key=lambda ia:(abs(ia[1]['size']*1000-row['size']),-ia[1]['size'],ia[0]))
            assert row['primary']==index+1
            vertical=asset['metrics'];size=row['size'];units=asset['units']
            assert row['font']==[rounded(v,size,units) for v in vertical+[sum(vertical)]]
            expected=[];position=0;byte=0
            glyphs={g['codepoint']:g for g in asset['report']['glyphs']}
            for c in TEXT:
                cp=ord(c);original=asset['source'][cp];g=glyphs[cp];end=byte+len(c.encode('utf8'))
                advance=rounded(original['advance'],size,units)
                expected.append([index+1,cp,original['glyph_id'],byte,end,position,advance,
                    *[rounded(g[k],size,asset['size']) for k in ('bearingX','bearingY','width','height')]])
                position+=advance;byte=end
            assert row['advance']==position and row['glyphs']==expected,row
        (args.work/(probe.parent.parent.name+'-'+probe.parent.name+'-'+probe.stem+'-results.json')).write_bytes(raw)
        if reference is None:reference=raw
        else:assert raw==reference,'source/cropped/installed result mismatch'
        outputs.append(dict(probe=str(probe),rows=len(rows),glyphs=sum(len(r['glyphs']) for r in rows),sha256=hashlib.sha256(raw).hexdigest()))
    result=dict(passed=True,faces=len(assets),outputs=outputs,assets=[dict(
        path=str(a['path']),sha256=a['report']['resourceSha256'],sourceSha256=a['report']['sourceSha256'],
        bytes=a['report']['resourceBytes'],weight=a['weight'],size=a['size'],raster=a['report']['raster']) for a in assets])
    (args.work/'results.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print(json.dumps(dict(passed=True,faces=len(assets),outputs=outputs),indent=2))


if __name__=='__main__':main()
