#!/usr/bin/env python3
"""Inspect Noto Sans SC feature closure and emit bounded HarfBuzz probes."""

from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

import fontTools
import uharfbuzz as hb
from fontTools.ttLib import TTFont


FACES = ("Regular", "Medium", "Bold")
FEATURES = ("ccmp", "locl", "liga", "kern", "mark", "mkmk")
FONTTOOLS_VERSION = "4.60.1"
UHARFBUZZ_VERSION = "0.57.0"
CASES = (
    ("combining-compose", "A\u0301", "Latn", "en", ()),
    ("common-ligature", "office", "Latn", "en", ()),
    ("regional-locl-en-control", "—", "Latn", "en", ()),
    ("regional-locl", "—", "Latn", "zh-cn", ()),
    ("chinese-punctuation", "中文，。！？", "Hani", "zh-cn", ()),
    ("latin-kerning", "AVATAR To Wa", "Latn", "en", ("kern",)),
    ("mark-attachment", "ㄓ\u0300", "Bopo", "zh-tw", ()),
    ("mark-chain", "A\u0307\u0301", "Latn", "en", ()),
)


def feature_closure(table, script_tag: str, language_tag: str | None):
    script_record = next((r for r in table.ScriptList.ScriptRecord if r.ScriptTag == script_tag), None)
    if script_record is None:
        return None
    script = script_record.Script
    lang_sys = None
    selected_language = "default"
    if language_tag:
        language_record = next((r for r in script.LangSysRecord if r.LangSysTag == language_tag), None)
        if language_record is not None:
            lang_sys = language_record.LangSys
            selected_language = language_tag
    if lang_sys is None:
        lang_sys = script.DefaultLangSys
    if lang_sys is None:
        return {"language": selected_language, "features": {}, "required": None}
    feature_indices = list(lang_sys.FeatureIndex)
    required = None
    if lang_sys.ReqFeatureIndex != 0xFFFF:
        required = lang_sys.ReqFeatureIndex
        feature_indices.append(required)
    result = {}
    for index in feature_indices:
        record = table.FeatureList.FeatureRecord[index]
        result.setdefault(record.FeatureTag, set()).update(record.Feature.LookupListIndex)
    return {
        "language": selected_language,
        "features": {tag: sorted(ids) for tag, ids in sorted(result.items())},
        "required": required,
    }


def lookup_info(table, lookup_id: int, table_tag: str):
    lookup = table.LookupList.Lookup[lookup_id]
    subtables = []
    for subtable in lookup.SubTable:
        info = {"kind": type(subtable).__name__, "format": getattr(subtable, "Format", None)}
        if table_tag == "GSUB" and hasattr(subtable, "ligatures"):
            info["firstGlyphs"] = len(subtable.ligatures)
            info["ligatures"] = sum(len(items) for items in subtable.ligatures.values())
        elif table_tag == "GSUB" and hasattr(subtable, "mapping"):
            info["mappings"] = len(subtable.mapping)
        elif table_tag == "GPOS" and hasattr(subtable, "PairSet"):
            info["coverage"] = len(subtable.Coverage.glyphs)
            info["pairs"] = sum(len(pair_set.PairValueRecord) for pair_set in subtable.PairSet)
        elif table_tag == "GPOS" and hasattr(subtable, "Class1Record"):
            info.update({"coverage": len(subtable.Coverage.glyphs),
                         "class1Assignments": len(subtable.ClassDef1.classDefs),
                         "class2Assignments": len(subtable.ClassDef2.classDefs),
                         "class1Count": subtable.Class1Count,
                         "class2Count": subtable.Class2Count})
        elif table_tag == "GPOS" and hasattr(subtable, "MarkCoverage"):
            info.update({"marks": len(subtable.MarkCoverage.glyphs),
                         "bases": len(subtable.BaseCoverage.glyphs) if hasattr(subtable, "BaseCoverage") else None})
        subtables.append(info)
    return {"id": lookup_id, "type": lookup.LookupType, "flags": lookup.LookupFlag, "subtables": subtables}


def select_closure(font: TTFont, table_tag: str, script_tag: str, language_tag: str | None):
    table = font[table_tag].table
    selected = feature_closure(table, script_tag, language_tag)
    if selected is None:
        return None
    target_features = [tag for tag in FEATURES if tag in selected["features"]]
    ids = sorted({index for tag in target_features for index in selected["features"][tag]})
    return {
        "language": selected["language"],
        "features": {tag: selected["features"][tag] for tag in target_features},
        "lookups": [lookup_info(table, lookup_id, table_tag) for lookup_id in ids],
    }


def shape(font_bytes: bytes, text: str, script: str, language: str, feature_overrides: tuple[str, ...]):
    face = hb.Face(font_bytes)
    font = hb.Font(face)
    hb.ot_font_set_funcs(font)
    font.scale = (face.upem, face.upem)
    buffer = hb.Buffer()
    buffer.add_str(text)
    buffer.direction = "ltr"
    buffer.script = script
    buffer.language = language
    if feature_overrides:
        feature_map = {tag: 0 for tag in FEATURES}
        feature_map.update({tag: 1 for tag in feature_overrides})
    else:
        feature_map = None
    hb.shape(font, buffer, feature_map)
    glyphs = [
        {"gid": info.codepoint, "cluster": info.cluster, "xAdvance": pos.x_advance,
         "yAdvance": pos.y_advance, "xOffset": pos.x_offset, "yOffset": pos.y_offset}
        for info, pos in zip(buffer.glyph_infos, buffer.glyph_positions)
    ]
    return {"glyphs": glyphs, "advance": sum(g["xAdvance"] for g in glyphs)}


def main():
    import argparse

    if fontTools.__version__ != FONTTOOLS_VERSION or hb.__version__ != UHARFBUZZ_VERSION:
        print(
            f"requires fontTools=={FONTTOOLS_VERSION} and uharfbuzz=={UHARFBUZZ_VERSION}; "
            f"found fontTools=={fontTools.__version__} and uharfbuzz=={hb.__version__}",
            file=sys.stderr,
        )
        return 2

    parser = argparse.ArgumentParser()
    parser.add_argument("font_dir", type=Path)
    args = parser.parse_args()
    result = {
        "oracle": "HarfBuzz via uharfbuzz 0.57.0",
        "fontToolsVersion": fontTools.__version__,
        "uharfbuzzVersion": hb.__version__,
        "requestedFeatures": FEATURES,
        "cases": [],
        "faces": {},
    }
    for face_name in FACES:
        path = args.font_dir / f"NotoSansSC-{face_name}.otf"
        data = path.read_bytes()
        font = TTFont(path)
        face_result = {
            "file": path.name,
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest().upper(),
            "glyphCount": font["maxp"].numGlyphs,
            "unitsPerEm": font["head"].unitsPerEm,
            "tableBytes": {tag: font.reader.tables[tag].length for tag in ("GSUB", "GPOS")},
            "scriptRecords": {
                tag: [
                    {"tag": rec.ScriptTag,
                     "languages": (["default"] if rec.Script.DefaultLangSys else []) +
                                 [lr.LangSysTag for lr in rec.Script.LangSysRecord]}
                    for rec in font[tag].table.ScriptList.ScriptRecord
                ]
                for tag in ("GSUB", "GPOS")
            },
            "profile": {},
            "oracle": [],
        }
        for table_tag in ("GSUB", "GPOS"):
            table = font[table_tag].table
            for script_record in table.ScriptList.ScriptRecord:
                script_tag = script_record.ScriptTag
                language_tags = [None] + [r.LangSysTag for r in script_record.Script.LangSysRecord]
                for lang_tag in language_tags:
                    closure = select_closure(font, table_tag, script_tag, lang_tag)
                    if closure is not None:
                        face_result["profile"][f"{table_tag}:{script_tag}:{closure['language']}"] = closure
        for case_name, text, script, language, overrides in CASES:
            face_result["oracle"].append({
                "case": case_name,
                "input": text.encode("unicode_escape").decode("ascii"),
                "script": script,
                "language": language,
                "featureMode": "only " + ",".join(overrides) if overrides else "HarfBuzz defaults",
                **shape(data, text, script, language, overrides),
            })
        result["faces"][face_name] = face_result
    print(json.dumps(result, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
