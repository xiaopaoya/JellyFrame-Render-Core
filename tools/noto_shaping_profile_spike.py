#!/usr/bin/env python3
"""Offline, bounded Noto Sans SC GSUB/GPOS profile comparison spike."""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path

from fontTools.ttLib import TTFont
import uharfbuzz as hb


@dataclass
class Glyph:
    glyph_id: int
    x_advance: int
    y_advance: int = 0
    x_offset: int = 0
    y_offset: int = 0


CORPUS = (
    ("latin-kern", "AVATAR To Wa", "Latn", "en", ("kern",)),
    ("numbers-punctuation", "A1, 9.50! ABC", "Latn", "en", ("kern",)),
    ("fullwidth", "ABC 123!?", "Latn", "en", ("fwid",)),
    ("han-punctuation", "中文，。！？", "Hani", "zh-cn", ("kern",)),
)
ORACLE_BASE_FEATURES = {
    "ccmp": 0,
    "locl": 0,
    "liga": 0,
    "clig": 0,
    "calt": 0,
    "rlig": 0,
    "kern": 0,
    "fwid": 0,
}


def feature_lookup_ids(font: TTFont, table_tag: str, feature_tag: str) -> list[int]:
    table = font[table_tag].table
    return sorted(
        {
            lookup_id
            for record in table.FeatureList.FeatureRecord
            if record.FeatureTag == feature_tag
            for lookup_id in record.Feature.LookupListIndex
        }
    )


def value_component(record, field: str) -> int:
    value = getattr(record, field, None) if record else None
    return int(value or 0)


def apply_gsub(font: TTFont, glyphs: list[Glyph], lookup_ids: list[int]) -> None:
    table = font["GSUB"].table
    for lookup_id in lookup_ids:
        lookup = table.LookupList.Lookup[lookup_id]
        if lookup.LookupType != 1:
            raise ValueError(f"selected GSUB lookup {lookup_id} is type {lookup.LookupType}, expected SingleSubst")
        for subtable in lookup.SubTable:
            mapping = subtable.mapping
            for glyph in glyphs:
                name = font.getGlyphName(glyph.glyph_id)
                replacement = mapping.get(name)
                if replacement is not None:
                    glyph.glyph_id = font.getGlyphID(replacement)
                    glyph.x_advance = font["hmtx"].metrics[replacement][0]


def pair_values(font: TTFont, lookup_id: int, left: int, right: int):
    lookup = font["GPOS"].table.LookupList.Lookup[lookup_id]
    left_name = font.getGlyphName(left)
    right_name = font.getGlyphName(right)
    if lookup.LookupType != 2:
        raise ValueError(f"selected GPOS lookup {lookup_id} is type {lookup.LookupType}, expected PairPos")
    for subtable in lookup.SubTable:
        coverage_index = subtable.Coverage.glyphs.index(left_name) if left_name in subtable.Coverage.glyphs else -1
        if coverage_index < 0:
            continue
        if subtable.Format == 1:
            pair_set = subtable.PairSet[coverage_index]
            match = next((r for r in pair_set.PairValueRecord if r.SecondGlyph == right_name), None)
            if match is not None:
                return match.Value1, match.Value2
        elif subtable.Format == 2:
            class1 = subtable.ClassDef1.classDefs.get(left_name, 0)
            class2 = subtable.ClassDef2.classDefs.get(right_name, 0)
            if class1 < subtable.Class1Count and class2 < subtable.Class2Count:
                match = subtable.Class1Record[class1].Class2Record[class2]
                if match.Value1 is not None or match.Value2 is not None:
                    return match.Value1, match.Value2
        else:
            raise ValueError(f"unsupported PairPos format {subtable.Format}")
    return None, None


def apply_gpos(font: TTFont, glyphs: list[Glyph], lookup_ids: list[int]) -> None:
    for lookup_id in lookup_ids:
        for i in range(len(glyphs) - 1):
            first, second = pair_values(font, lookup_id, glyphs[i].glyph_id, glyphs[i + 1].glyph_id)
            glyphs[i].x_advance += value_component(first, "XAdvance")
            glyphs[i].y_advance += value_component(first, "YAdvance")
            glyphs[i].x_offset += value_component(first, "XPlacement")
            glyphs[i].y_offset += value_component(first, "YPlacement")
            glyphs[i + 1].x_advance += value_component(second, "XAdvance")
            glyphs[i + 1].y_advance += value_component(second, "YAdvance")
            glyphs[i + 1].x_offset += value_component(second, "XPlacement")
            glyphs[i + 1].y_offset += value_component(second, "YPlacement")


def prototype(font: TTFont, text: str, gsub_ids: list[int], gpos_ids: list[int]) -> list[tuple[int, int, int, int, int]]:
    cmap = font.getBestCmap()
    glyphs = []
    for char in text:
        name = cmap.get(ord(char), ".notdef")
        glyph_id = font.getGlyphID(name)
        glyphs.append(Glyph(glyph_id, font["hmtx"].metrics[name][0]))
    apply_gsub(font, glyphs, gsub_ids)
    apply_gpos(font, glyphs, gpos_ids)
    return [(g.glyph_id, g.x_advance, g.y_advance, g.x_offset, g.y_offset) for g in glyphs]


def harfbuzz(font_bytes: bytes, text: str, script: str, language: str, enabled_features: tuple[str, ...]):
    face = hb.Face(font_bytes)
    hb_font = hb.Font(face)
    hb.ot_font_set_funcs(hb_font)
    hb_font.scale = (face.upem, face.upem)
    buffer = hb.Buffer()
    buffer.add_str(text)
    buffer.direction = "ltr"
    buffer.script = script
    buffer.language = language
    features = dict(ORACLE_BASE_FEATURES)
    features.update({tag: 1 for tag in enabled_features})
    hb.shape(hb_font, buffer, features)
    return [
        (info.codepoint, pos.x_advance, pos.y_advance, pos.x_offset, pos.y_offset)
        for info, pos in zip(buffer.glyph_infos, buffer.glyph_positions)
    ]


def profile(font: TTFont, table_tag: str, lookup_ids: list[int]) -> list[dict]:
    out = []
    for lookup_id in lookup_ids:
        lookup = font[table_tag].table.LookupList.Lookup[lookup_id]
        item = {"lookup": lookup_id, "type": lookup.LookupType, "flags": lookup.LookupFlag, "subtables": []}
        for subtable in lookup.SubTable:
            if table_tag == "GSUB" and hasattr(subtable, "mapping"):
                item["subtables"].append({"kind": type(subtable).__name__, "mappingCount": len(subtable.mapping)})
            elif table_tag == "GPOS" and hasattr(subtable, "PairSet"):
                item["subtables"].append({"kind": type(subtable).__name__, "format": subtable.Format,
                                          "coverageCount": len(subtable.Coverage.glyphs),
                                          "pairCount": sum(len(s.PairValueRecord) for s in subtable.PairSet)})
            elif table_tag == "GPOS" and hasattr(subtable, "Class1Record"):
                item["subtables"].append({"kind": type(subtable).__name__, "format": subtable.Format,
                                          "coverageCount": len(subtable.Coverage.glyphs),
                                          "class1Count": subtable.Class1Count,
                                          "class2Count": subtable.Class2Count})
            else:
                item["subtables"].append({"kind": type(subtable).__name__, "format": getattr(subtable, "Format", None)})
        out.append(item)
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("font", type=Path)
    args = parser.parse_args()
    font_bytes = args.font.read_bytes()
    font = TTFont(args.font)
    gsub_ids = feature_lookup_ids(font, "GSUB", "fwid")
    gpos_ids = feature_lookup_ids(font, "GPOS", "kern")
    mismatches = []
    cases = []
    for name, text, script, language, enabled_features in CORPUS:
        expected = prototype(font, text, gsub_ids, gpos_ids)
        if "fwid" not in enabled_features:
            expected = prototype(font, text, [], gpos_ids)
        if "kern" not in enabled_features:
            expected = prototype(font, text, gsub_ids, [])
        actual = harfbuzz(font_bytes, text, script, language, enabled_features)
        ok = expected == actual
        cases.append({"case": name, "input": text, "glyphCount": len(expected), "match": ok,
                      "glyphs": [g[0] for g in expected],
                      "xAdvances": [g[1] for g in expected]})
        if not ok:
            mismatches.append({"case": name, "prototype": expected, "harfbuzz": actual})
    report = {
        "font": str(args.font),
        "glyphCount": font["maxp"].numGlyphs,
        "unitsPerEm": font["head"].unitsPerEm,
        "gsubFeature": "fwid",
        "gsub": profile(font, "GSUB", gsub_ids),
        "gposFeature": "kern",
        "gpos": profile(font, "GPOS", gpos_ids),
        "oracle": "HarfBuzz via uharfbuzz",
        "oracleBaseFeatures": ORACLE_BASE_FEATURES,
        "profileLimits": ["horizontal LTR", "one selected GSUB SingleSubst feature", "one selected GPOS PairPos feature",
                          "Unicode scalar-to-glyph mapping", "no clusters or source maps", "no fallback", "no wrapping"],
        "cases": cases,
        "mismatches": mismatches,
    }
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
