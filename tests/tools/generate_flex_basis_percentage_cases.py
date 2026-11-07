"""Rational percentage-basis scenes; reads neither Core nor browser output."""
from fractions import Fraction as F
from pathlib import Path
import json
import sys

def quantize(v):
    return F((v.numerator*2000+v.denominator)//(2*v.denominator),1000)
def pixel(v):
    return (v.numerator*2+v.denominator)//(2*v.denominator)
def rect(x,y,w,h):
    return [pixel(x),pixel(y),pixel(x+w)-pixel(x),pixel(y+h)-pixel(y)]
rows=[]
for column in (False,True):
    for main_text in ('100.25','137.75','201.125'):
        for coefficient in ('12.5','33.333','66.667','150'):
            for border_box in (False,True):
                main='height' if column else 'width'
                cross='width' if column else 'height'
                preferred=quantize(F(main_text)*F(coefficient)/100)
                content=preferred-4 if border_box else preferred
                outer=content+4
                cross_content=F('16.25') if border_box else F('20.25')
                cross_outer=cross_content+4
                css='*{margin:0;padding:0;border:0}#p{display:flex;'
                css+='flex-direction:'+('column' if column else 'row')+';align-items:flex-start;gap:2px;'
                css+=f'{main}:{main_text}px;{cross}:50.25px;padding:2px;border:1px solid black;margin-left:1px;margin-top:2px}}'
                css+=f'#a{{{main}:999.25px;{cross}:20.25px;min-{main}:0;flex:0 0 {coefficient}%;'
                css+='box-sizing:'+('border-box' if border_box else 'content-box')+';padding:1px;border:1px solid black}'
                css+='#g{width:50%;height:50%}'
                css+=f'#b{{{main}:5.25px;{cross}:20.25px;flex-shrink:0}}'
                if column:
                    rectangles=[rect(F(4),F(5),cross_outer,outer),rect(F(6),F(7),quantize(cross_content/2),quantize(content/2)),rect(F(4),F(7)+outer,F('20.25'),F('5.25'))]
                else:
                    rectangles=[rect(F(4),F(5),outer,cross_outer),rect(F(6),F(7),quantize(content/2),quantize(cross_content/2)),rect(F(6)+outer,F(5),F('5.25'),F('20.25'))]
                name=f'{column}-{main_text}-{coefficient}-{border_box}'
                rows.append('{'+json.dumps(name)+','+json.dumps(css)+',{' + ','.join('{'+','.join(map(str,r))+'}' for r in rectangles)+'}},')
header='#pragma once\nnamespace flex_basis_cases { struct Case {const char* id;const char* css;int rects[3][4];};\ninline constexpr Case cases[]={\n'
Path(sys.argv[1]).write_text(header+'\n'.join(rows)+'\n};\n}\n',encoding='utf8')
print(f'authored {len(rows)} rational percentage-basis scenes')
