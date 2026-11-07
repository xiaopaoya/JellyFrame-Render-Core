"""Closed-form Flex allocation scenes using rational arithmetic, no Core outputs.

The scenarios have explicit zero automatic minima and unambiguous active bounds.
They do not model intrinsic content minima, wrapping or browser rasterization.
"""
from fractions import Fraction as F
from pathlib import Path
import json
import sys

def px(v):
    return (v.numerator*2+v.denominator)//(2*v.denominator)

rows=[]
for column in (False,True):
    for size_text in ('120.25','172.75'):
        for gap in (0,3):
            available=F(size_text)-gap*2
            scenarios=[
                ('grow',[F('10.25'),F('20.5'),F('30.125')],['1','2','0'],['1','1','1']),
                ('max-freeze',[F('20.5')]*3,['1']*3,['1']*3),
                ('min-freeze',[F('80.5')]*3,['0']*3,['1']*3),
                ('partial-grow',[F('10.25'),F('20.5'),F('5')],['.25','.25','0'],['1']*3),
                ('weighted-shrink',[F('40.25'),F('80.5'),F('40.25')],['0']*3,['1','1','2'])]
            # Weighted shrinking is only a shrink scene when available < 161.
            for name,bases,grows,shrinks in scenarios:
                if name=='weighted-shrink' and available>=sum(bases): continue
                if name=='grow':
                    free=available-sum(bases);targets=[bases[0]+free/3,bases[1]+free*2/3,bases[2]]
                elif name=='max-freeze': targets=[F('25.125'),(available-F('25.125'))/2,(available-F('25.125'))/2]
                elif name=='min-freeze': targets=[F('60.25'),(available-F('60.25'))/2,(available-F('60.25'))/2]
                elif name=='partial-grow':
                    free=available-sum(bases);targets=[bases[0]+free/4,bases[1]+free/4,bases[2]]
                else:
                    deficit=sum(bases)-available
                    weights=[(base-4)*F(factor) for base,factor in zip(bases,shrinks)]
                    targets=[base-deficit*weight/sum(weights) for base,weight in zip(bases,weights)]
                main='height' if column else 'width';cross='width' if column else 'height'
                css='*{margin:0;padding:0;border:0}#p{display:flex;flex-direction:'+('column' if column else 'row')+';'
                css+=f'{main}:{size_text}px;{cross}:50.25px;gap:{gap}px;align-items:center;justify-content:center}}'
                rects=[];cursor=(F(size_text)-sum(targets)-2*gap)/2
                for i in range(3):
                    bound=(f'max-{main}:25.125px;' if name=='max-freeze' and i==0 else '')
                    minimum='60.25' if name=='min-freeze' and i==0 else '0'
                    css+=f'#i{i}{{box-sizing:border-box;padding:1px;border:1px solid black;min-{main}:{minimum}px;{bound}'
                    css+=f'{main}:999.25px;{cross}:20.25px;flex-grow:{grows[i]};flex-shrink:{shrinks[i]};flex-basis:{float(bases[i]):g}px}}'
                    css+=f'#g{i}{{width:50%;height:50%}}'
                    start=cursor;end=start+targets[i]
                    child=[15,px(start),px(F('35.25'))-15,px(end)-px(start)] if column else [px(start),15,px(end)-px(start),20]
                    # Content starts two pixels inside each border-box edge.
                    grand_start=start+2;grand_end=grand_start+(targets[i]-4)/2
                    grand=[17,px(grand_start),8,px(grand_end)-px(grand_start)] if column else [px(grand_start),17,px(grand_end)-px(grand_start),8]
                    rects.extend((child,grand));cursor=end+gap
                rows.append('{'+json.dumps(name+'-'+('column' if column else 'row')+'-'+size_text+'-'+str(gap))+','+json.dumps(css)+',{' + ','.join('{'+','.join(map(str,r))+'}' for r in rects)+'}},')
text='#pragma once\n// Closed-form rational Flex scenes, authored independently of Core output.\nnamespace flex_precision_cases { struct Case {const char* id;const char* css;int rects[6][4];};\ninline constexpr Case cases[]={\n'
Path(sys.argv[1]).write_text(text+'\n'.join(rows)+'\n};\n}\n',encoding='utf8')
print(f'authored {len(rows)} Flex allocation scenes')
