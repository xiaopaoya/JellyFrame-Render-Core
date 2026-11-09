"""Author ordinary-block geometry expectations using rational arithmetic only.

No Core/browser output is read. Q1000 intermediate quantization and final edge
projection are declared expectations, not a claim of browser raster parity.
"""
from fractions import Fraction as F
from pathlib import Path
import json
import sys

def rounded(number):
    return (number.numerator * 2 + number.denominator) // (2 * number.denominator)

def percent(basis, value):
    return F(rounded(basis * F(value) * 10), 1000)

rows = []
for parent_width in ('30.25','100.5','172.75'):
    for parent_border_box in (False,True):
        for child_border_box in (False,True):
            for child_percent in ('33.3','37.5','66.7'):
                for grand_percent in ('12.5','50'):
                    parent_content = F(parent_width) - (10 if parent_border_box else 0)
                    parent_border = parent_content + 10
                    child_declared = percent(parent_content,child_percent)
                    child_content = max(F(0),child_declared-(4 if child_border_box else 0))
                    child_border = child_content+4
                    grand_border = percent(child_content,grand_percent)
                    child_x, grand_x = F(8), F(10)  # parent decoration 5 + child margin 3, then child padding 2
                    css = ('*{margin:0;padding:0;border:0}'
                        f'#p{{width:{parent_width}px;height:101.25px;padding:3px;border:2px solid red;'
                        f'box-sizing:{"border-box" if parent_border_box else "content-box"}}}'
                        f'#c{{width:{child_percent}%;height:37.5%;padding:0 2px;margin-left:3px;'
                        f'box-sizing:{"border-box" if child_border_box else "content-box"}}}'
                        f'#g{{width:{grand_percent}%;height:50%}}')
                    parent_height_content = F('101.25')-(10 if parent_border_box else 0)
                    child_height = percent(parent_height_content,'37.5')
                    grand_height = percent(child_height,'50')
                    rects = [(0,0,rounded(parent_border),rounded(parent_height_content+10)),
                        (rounded(child_x),5,rounded(child_x+child_border)-rounded(child_x),rounded(child_height)),
                        (rounded(grand_x),5,rounded(grand_x+grand_border)-rounded(grand_x),rounded(grand_height))]
                    rows.append('{' + json.dumps(css) + ',{' + ','.join('{' + ','.join(map(str,r)) + '}' for r in rects) + '}},')
output = '#pragma once\n// Independently authored rational Q1000 geometry; generated without Core output.\n'
output += 'namespace block_precision_cases { struct Case { const char* css; int rects[3][4]; };\ninline constexpr Case cases[] = {\n'
output += '\n'.join(rows) + '\n};\n}\n'
Path(sys.argv[1]).write_text(output,encoding='utf8')
print(f'authored {len(rows)} ordinary nested geometry cases')
