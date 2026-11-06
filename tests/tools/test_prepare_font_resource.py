"""End-to-end prepared-font contracts; all generated files stay in --work.

Run with the pinned tools/font_resource_requirements.txt environment and a built
jellyframe_font_resource_tests executable. --noto optionally checks a real font.
"""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import zlib

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont, newTable
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('prepare_font_resource', ROOT/'tools/prepare_font_resource.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


def synthetic():
    builder = FontBuilder(1000, isTTF=True)
    order = ['.notdef', 'space', 'j', 'acute', 'A']
    builder.setupGlyphOrder(order)
    builder.setupCharacterMap({32:'space', 106:'j', 0x301:'acute', 65:'A'})
    glyphs = {}
    for name, bounds in {'.notdef':(0,0,400,600), 'space':None,
                         'j':(-100,-200,200,700), 'acute':(-80,650,80,800),
                         'A':(0,0,500,700)}.items():
        pen = TTGlyphPen(None)
        if bounds:
            l,b,r,t = bounds
            pen.moveTo((l,b));pen.lineTo((r,b));pen.lineTo((r,t));pen.lineTo((l,t));pen.closePath()
        glyphs[name] = pen.glyph()
    builder.setupGlyf(glyphs)
    builder.setupHorizontalMetrics({'.notdef':(500,0),'space':(250,0),
                                    'j':(333,-100),'acute':(0,-80),'A':(500,0)})
    builder.setupHorizontalHeader(ascent=800, descent=-200, lineGap=50)
    builder.setupNameTable({'familyName':'Canonical Fixture','styleName':'Regular',
                           'uniqueFontIdentifier':'Canonical Fixture 1',
                           'fullName':'Canonical Fixture Regular','psName':'CanonicalFixture-Regular'})
    builder.setupOS2(sTypoAscender=700,sTypoDescender=-100,sTypoLineGap=0,
                    usWinAscent=900,usWinDescent=300,usWeightClass=400)
    builder.setupPost()
    builder.font['head'].created = builder.font['head'].modified = 2082844800
    builder.font.recalcTimestamp = False
    output = io.BytesIO();builder.save(output)
    return output.getvalue()


def rounded(value, size, denominator):
    product = value*size
    return (1 if product >= 0 else -1)*((abs(product)+denominator//2)//denominator)


class PreparationTests(unittest.TestCase):
    def check_asset(self, source, chars, size, bits, stem):
        blob, report = tool.prepare(source, chars, size, bits)
        self.assertEqual(tool.prepare(source, chars, size, bits)[0], blob)
        self.assertEqual(report['sourceSha256'], hashlib.sha256(source).hexdigest())
        self.assertEqual(report['resourceSha256'], hashlib.sha256(blob).hexdigest())
        check = bytearray(blob);stored = struct.unpack_from('<I', check, 96)[0]
        struct.pack_into('<I', check, 96, 0)
        self.assertEqual(zlib.crc32(check), stored)
        path = ARGS.work/(stem+'.jffont2');path.write_bytes(blob)
        (ARGS.work/(stem+'.json')).write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
        actual = json.loads(subprocess.check_output([str(ARGS.probe),str(path)],text=True))
        with TTFont(io.BytesIO(source)) as font:
            vertical = [font['hhea'].ascent, -font['hhea'].descent, font['hhea'].lineGap]
            self.assertEqual(actual['metrics'], vertical)
            self.assertEqual(actual['units'], font['head'].unitsPerEm)
            self.assertEqual(actual['weight'], font['OS/2'].usWeightClass)
            self.assertEqual(actual['size'], size)
            self.assertEqual(actual['bits'], bits)
            self.assertEqual(actual['scaled'], [rounded(v,15500,actual['units']) for v in vertical+[sum(vertical)]])
            cmap = font.getBestCmap()
            self.assertEqual(len(actual['glyphs']), len(set(chars)-set('\t\r\n')))
            # Decode every packed glyph into a baseline-positioned canvas and
            # compare independent direct rasterization, including negative X/Y.
            pillow = ImageFont.truetype(io.BytesIO(source),size=size,layout_engine=ImageFont.Layout.BASIC)
            bitmap = struct.unpack_from('<I',blob,28)[0]
            levels = (1<<bits)-1
            for index,row in enumerate(actual['glyphs']):
                cp,gid,advance,bx,by,w,h,*scaled = row
                name = cmap[cp]
                self.assertEqual(gid,font.getGlyphID(name))
                self.assertEqual(advance,font['hmtx'].metrics[name][0])
                self.assertEqual(scaled, [rounded(advance,15500,actual['units'])]+[
                    rounded(v,15500,size) for v in (bx,by,w,h)])
                stride,offset,length = struct.unpack_from('<III',blob,128+index*40+24)
                self.assertEqual(length,stride*h)
                canvas = Image.new('L',(128,128),0)
                ImageDraw.Draw(canvas).text((64,64),chr(cp),font=pillow,fill=255,anchor='ls')
                expected = [(v*levels+127)//255 for v in canvas.get_flattened_data()]
                decoded = [0]*(128*128)
                for y in range(h):
                    for x in range(w):
                        byte = blob[bitmap+offset+y*stride+x*bits//8]
                        value = (byte>>(8-bits-(x*bits)%8))&levels
                        decoded[(64+by+y)*128+64+bx+x] = value
                self.assertEqual(decoded,expected,f'baseline/coverage U+{cp:04X}')
        RESULTS.append(dict(name=stem,resourceSha256=report['resourceSha256'],
                            bytes=len(blob),glyphs=len(actual['glyphs']),raster=report['raster']))
        return report

    def test_synthetic_depths_and_original_metrics(self):
        source = synthetic()
        for bits in (1,2,4,8):
            with self.subTest(bits=bits):
                report = self.check_asset(source,' Aj\u0301\r\n\t',20,bits,f'synthetic-{bits}')
                self.assertEqual([report[k] for k in ('ascent','descent','lineGap')],[800,200,50])
                glyphs = {g['codepoint']:g for g in report['glyphs']}
                self.assertLess(glyphs[106]['bearingX'],0)
                self.assertLess(glyphs[106]['bearingY'],0)
                self.assertEqual(glyphs[106]['advance'],333)
                self.assertEqual(glyphs[0x301]['advance'],0)
                self.assertEqual([glyphs[32][k] for k in ('width','height','bearingX','bearingY')],[0,0,0,0])
                self.assertEqual(glyphs[32]['advance'],250)

    def test_real_noto(self):
        if ARGS.noto is None:
            self.skipTest('--noto not supplied')
        self.check_asset(ARGS.noto.read_bytes(),' Ajg\u2026\u4e2d',24,4,'noto-24')

    def test_refusals(self):
        source = synthetic()
        for chars,size,bits,budget in [('',20,4,8000),('\t\n',20,4,8000),
                ('\u4e2d',20,4,8000),('\ud800',20,4,8000),('A',0,4,8000),
                ('A',4097,4,8000),('A',20,3,8000),('A',20,4,128)]:
            with self.subTest(chars=repr(chars),size=size,bits=bits,budget=budget):
                with self.assertRaises(ValueError):tool.prepare(source,chars,size,bits,budget)
        for mode in ('variable','italic','oblique','negative-ascent'):
            with TTFont(io.BytesIO(source)) as font:
                if mode=='variable':
                    table=newTable('fvar');table.axes=[];table.instances=[];font['fvar']=table
                elif mode=='italic':
                    font['OS/2'].fsSelection = (font['OS/2'].fsSelection & ~(1<<6)) | 1
                    font['head'].macStyle |= 2
                elif mode=='oblique':
                    font['OS/2'].version=4;font['OS/2'].fsSelection &= ~(1<<6)
                    font['OS/2'].fsSelection |= 1<<9
                else:font['hhea'].ascent=-1
                output=io.BytesIO();font.save(output)
            with self.assertRaises(ValueError,msg=mode):tool.prepare(output.getvalue(),'A',20)

    def test_cli_no_outputs_on_bad_coverage_or_alias(self):
        font_path=ARGS.work/'cli-font.ttf';font_path.write_bytes(synthetic())
        chars=ARGS.work/'missing.txt';chars.write_text('\u4e2d',encoding='utf8')
        output=ARGS.work/'missing.jffont2';report=ARGS.work/'missing.json'
        cmd=[sys.executable,str(ROOT/'tools/prepare_font_resource.py'),'--font',str(font_path),
             '--characters',str(chars),'--size','20','--output',str(output),'--report',str(report)]
        result=subprocess.run(cmd,capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0);self.assertFalse(output.exists());self.assertFalse(report.exists())
        chars.write_text('A',encoding='utf8')
        result=subprocess.run(cmd[:-1]+[str(output)],capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0);self.assertFalse(output.exists())
        output.write_bytes(b'preserve')
        result=subprocess.run(cmd,capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0);self.assertEqual(output.read_bytes(),b'preserve')

    def test_cli_success(self):
        source=synthetic()
        font=ARGS.work/'success.ttf';font.write_bytes(source)
        chars=ARGS.work/'success.txt';chars.write_text(' Aj\u0301',encoding='utf8')
        output=ARGS.work/'success.jffont2';report=ARGS.work/'success.json'
        subprocess.run([sys.executable,str(ROOT/'tools/prepare_font_resource.py'),
            '--font',str(font),'--characters',str(chars),'--size','20','--bits','4',
            '--output',str(output),'--report',str(report)],capture_output=True,text=True,check=True)
        expected,expected_report=tool.prepare(source,' Aj\u0301',20,4)
        self.assertEqual(output.read_bytes(),expected)
        self.assertEqual(json.loads(report.read_text(encoding='utf8')),expected_report)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe',type=Path,required=True)
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--noto',type=Path)
    ARGS=parser.parse_args()
    ARGS.work.mkdir(parents=True,exist_ok=False)
    RESULTS=[]
    suite=unittest.defaultTestLoader.loadTestsFromTestCase(PreparationTests)
    outcome=unittest.TextTestRunner(verbosity=2).run(suite)
    (ARGS.work/'results.json').write_text(json.dumps(dict(passed=outcome.wasSuccessful(),
        tests=outcome.testsRun,assets=RESULTS),indent=2)+'\n',encoding='utf8')
    sys.exit(0 if outcome.wasSuccessful() else 1)
