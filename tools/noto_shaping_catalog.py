#!/usr/bin/env python3
"""Package verified original-GID raster evidence as a selected-profile JFCAT1."""
import argparse
import hashlib
import json
import struct
import unicodedata
import zlib
from pathlib import Path

HEADER = struct.Struct('<8sHHI32s32s32sIIIIHHHBBiiiHHHHI12s')
assert HEADER.size == 172

def generate(evidence, fixtures, destination):
    report = json.loads((evidence / 'report.json').read_text())
    destination.mkdir(parents=True, exist_ok=True)
    manifest = {'format': 'JFCAT1', 'faces': {}}
    for face, info in report['faces'].items():
        companion = (fixtures / (face.lower() + '.jfshr2')).read_bytes()
        raster = (evidence / (face + '-product-normalization.jfgid')).read_bytes()
        expected = info['normalizationClosure']['sha256']
        if hashlib.sha256(raster).hexdigest() != expected:
            raise ValueError('raster evidence hash mismatch')
        if companion[16:48].hex() != info['sha256']:
            raise ValueError('source mismatch')
        raw = {int(cp): gid for cp, gid in json.loads((evidence / (face + '-product-mapping.json')).read_text()).items()}
        # Source font cmap is independently read, with pinned bytes verified.
        from fontTools.ttLib import TTFont
        source = Path(info['source']).read_bytes()
        if hashlib.sha256(source).hexdigest() != info['sha256']:
            raise ValueError('source hash mismatch')
        font = TTFont(info['source'])
        from noto_shaping_closure_prototype import selected_lookup_plan, PROFILE_SCRIPTS
        for script, language in PROFILE_SCRIPTS:
            for _, lookup_id in selected_lookup_plan(font, 'GPOS', script, language, ('kern', 'mark')):
                lookup = font['GPOS'].table.LookupList.Lookup[lookup_id]
                if lookup.LookupType == 2:
                    for subtable in lookup.SubTable:
                        if subtable.ValueFormat2 != 0:
                            raise ValueError('selected PairPos ValueFormat2 must be zero')
        cmap = font.getBestCmap()
        admitted_gids = set(info['normalizationClosure']['gids'])
        classes = font['GDEF'].table.GlyphClassDef.classDefs
        marks = {font.getGlyphID(name) for name, value in classes.items() if value == 3}
        for script, language in PROFILE_SCRIPTS:
            selected_marks = set()
            for _, lookup_id in selected_lookup_plan(font, 'GPOS', script, language, ('mark',)):
                for subtable in font['GPOS'].table.LookupList.Lookup[lookup_id].SubTable:
                    selected_marks.update(font.getGlyphID(name) for name in subtable.MarkCoverage.glyphs)
            if (marks & admitted_gids) != (selected_marks & admitted_gids):
                raise ValueError('selected MarkCoverage must equal admitted GDEF class3')
        scalars = set(raw) | set(report['normalizationAlphabetClosure']['extraScalars'])
        mappings = b''.join(struct.pack('<IHH', cp, font.getGlyphID(cmap[cp]), (1 if cp in raw else 0) | 2) for cp in sorted(scalars) if cp in cmap)
        count = struct.unpack_from('<I', raster, 20)[0]
        bitmap_offset = struct.unpack_from('<I', raster, 28)[0]
        payload = mappings + raster[96:]
        metadata = font['hhea']
        header = HEADER.pack(b'JFCAT1\0\0', 1, HEADER.size, HEADER.size + len(payload), companion[16:48], companion[48:80], hashlib.sha256(companion).digest(), info['glyphCount'], len(mappings)//8, count, len(raster)-bitmap_offset, info['upem'], 16, font['OS/2'].usWeightClass, 4, 0, metadata.ascent, -metadata.descent, metadata.lineGap, 14, 0, 1, 0, zlib.crc32(payload), b'\0'*12)
        data = header + payload
        path = destination / (face.lower() + '.jfcat1')
        path.write_bytes(data)
        manifest['faces'][face] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(), 'sourceSha256': info['sha256'], 'companionSha256': hashlib.sha256(companion).hexdigest(), 'profileSha256': companion[48:80].hex(), 'unicodeMappings': len(mappings)//8, 'rasterGids': count}
    (destination / 'manifest.json').write_text(json.dumps(manifest, indent=2))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--fixtures', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    generate(args.evidence, args.fixtures, args.output)
