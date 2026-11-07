"""Replay an externally supplied frozen HTML dimension corpus on a built Core.

Writes a diagnostic report; differences are never reclassified as passing tests.
Windows/Clang driver for the ratio-state candidate. Frozen expectations are not edited.
"""
import argparse
from decimal import Decimal, ROUND_HALF_UP
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import subprocess


def literal(value):
    # Emit UTF-8 bytes, including ASCII control characters, without C++ escape ambiguity.
    return '"' + ''.join(f'\\{b:03o}' for b in value.encode('utf8')) + '"'


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--cases', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    cases = json.loads(args.cases.read_text(encoding='utf8'))['cases']
    args.output.parent.mkdir(parents=True, exist_ok=True)
    source = args.output.parent / 'dimension_replay.cpp'
    program = ['#include "render_core/css_parser.h"', '#include "render_core/style.h"',
               '#include <iostream>', 'using namespace jellyframe;', 'int main(){']
    for case in cases:
        program += ['{auto node=make_element(' + literal(case['tag']) + ');',
                    'node->set_attribute("id","target");']
        for field in ('width', 'height', 'style'):
            if case[field] is not None:
                program.append(f'node->set_attribute("{field}",{literal(case[field])});')
        program.append('auto s=StyleResolver(CssParser{}.parse(' + literal(case['css']) + ')).resolve(*node);')
        program.append('std::cout << ' + literal(case['id']) +
                       ' << " " << s.width << " " << s.height << " " << s.width_percent'
                       ' << " " << s.height_percent << " " << s.aspect_ratio_width'
                       ' << " " << s.aspect_ratio_height << " " << s.aspect_ratio_auto << "\\n";}')
    program.append('}')
    source.write_text('\n'.join(program), encoding='utf8')
    executable = source.with_suffix('.exe')
    subprocess.run(['clang++', '-std=c++17', '-O2', '-D_DLL', '-D_MT', '-Xclang',
                    '--dependent-lib=msvcrt', '-I' + str(root / 'include'), str(source),
                    str(args.build_dir / 'jellyframe_render_core.lib'), '-o', str(executable)], check=True)
    output = subprocess.check_output([str(executable)], text=True)
    actuals = {parts[0]: list(map(int, parts[1:])) for parts in map(str.split, output.splitlines())}
    checks = []
    rounded = []
    for case in cases:
        w, h, wp, hp, rw, rh, automatic = actuals[case['id']]
        for axis, px, percent in [('Width', w, wp), ('Height', h, hp)]:
            expected = case['expected' + axis]
            kind = expected['kind']
            actual = {'kind': 'percent' if percent >= 0 else ('auto' if px < 0 else 'px'),
                      'value': percent if percent >= 0 else (0 if px < 0 else px)}
            value = Decimal(expected['value'])
            proxy = int(value.quantize(Decimal(1), rounding=ROUND_HALF_UP))
            # Same bounds as the existing scalar profile, not a new expected oracle.
            if kind == 'percent':
                proxy = min(1000, proxy)
            elif kind == 'px' and proxy > 2147483647:
                kind, proxy = 'auto', 0
            match = actual == {'kind': kind, 'value': proxy}
            checks.append(dict(id=case['id'], property=axis.lower(), match=match,
                               frozen=expected, scalarExpected={'kind': kind, 'value': proxy}, actual=actual))
            if value != Decimal(proxy) and expected['kind'] != 'auto':
                rounded.append(dict(id=case['id'], property=axis.lower(), frozen=str(value), scalar=proxy))
        expected = case['expectedRatio']
        if expected is None:
            match = rw == 0 and rh == 0
        else:
            a, b = map(Fraction, expected)
            match = ((rw == 0) == (a == 0) and (rh == 0) == (b == 0) and rw * b == rh * a)
        checks.append(dict(id=case['id'], property='ratio', match=match, frozen=expected, actual=[rw, rh]))
        checks.append(dict(id=case['id'], property='ratioAuto', match=bool(automatic) == case['ratioAuto'],
                           frozen=case['ratioAuto'], actual=bool(automatic)))
    report = dict(corpusSHA256=hashlib.sha256(args.cases.read_bytes()).hexdigest(),
                  styleSourceSHA256=hashlib.sha256((root / 'src/style.cpp').read_bytes()).hexdigest(),
                  librarySHA256=hashlib.sha256((args.build_dir / 'jellyframe_render_core.lib').read_bytes()).hexdigest(),
                  coreHead=subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip(),
                  checks=checks, total=len(checks), matches=sum(c['match'] for c in checks),
                  differences=[c for c in checks if not c['match']], scalarPrecisionDifferences=rounded,
                  unavailable=[],
                  scope='Scalar computed values; no natural image geometry, device or performance acceptance')
    args.output.write_text(json.dumps(report, ensure_ascii=True, indent=2) + '\n', encoding='utf8')
    print(json.dumps({key: report[key] for key in ('total', 'matches', 'differences', 'scalarPrecisionDifferences')}, indent=2))
    return 1 if report['differences'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
