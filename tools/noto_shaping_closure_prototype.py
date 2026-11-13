#!/usr/bin/env python3
"""Generate an offline bounded Noto shaping-closure and GID-catalog prototype."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import unicodedata
from dataclasses import dataclass
from pathlib import Path

import fontTools
import uharfbuzz as hb
from fontTools.ttLib import TTFont

from noto_gid_catalog_spike import build_catalog
from noto_sans_sc_feature_probe import FEATURES, shape as harfbuzz_shape


FONTTOOLS_VERSION = "4.60.1"
UHARFBUZZ_VERSION = "0.57.0"
FACES = {
    "Regular": "a2b93e6c2db05d6bbbf6f27d413ec73269735b7b679019c8a5aa9670ff0ffbf2",
    "Medium": "9c62ceb174d7529ae4a7f2071f6531991cfadbc2f1897910b48ba951a580ac57",
    "Bold": "d1961be1161ea1be08496c920862d06ea5c23a757628f4fd69368de1d9f51bed",
}
JFFONT2_SHA256 = "7454e5a35c99e1bfbeb0cf226c0e9c128c1d9520b40ec9037a293a2a6c43a320"
JFFONT2_MAGIC = b"JFFONT2\0"
JFFONT2_HEADER_BYTES = 128
JFFONT2_RECORD_BYTES = 40
JFFONT2_EXPECTED_GLYPHS = 4539
JFFONT2_GLYPH_ID_OFFSET = 4
PROFILE_FEATURES = ("ccmp", "locl", "liga", "kern", "mark")
PROFILE_SCRIPTS = (
    ("latn", "en"),
    ("latn", "zh-cn"),
    ("hani", "en"),
    ("hani", "zh-cn"),
    ("bopo", "zh-tw"),
)
CASES = (
    ("combining-compose", "A\u0301", "Latn", "en"),
    ("common-ligature", "office", "Latn", "en"),
    ("regional-locl-en-control", "—", "Latn", "en"),
    ("regional-locl", "—", "Latn", "zh-cn"),
    ("chinese-punctuation", "中文，。！？", "Hani", "zh-cn"),
    ("latin-kerning", "AVATAR To Wa", "Latn", "en"),
    ("mark-attachment", "ㄓ\u0300", "Bopo", "zh-tw"),
    ("mark-chain", "A\u0307\u0301", "Latn", "en"),
)


@dataclass
class Glyph:
    gid: int
    cluster: int
    x_advance: int
    y_advance: int = 0
    x_offset: int = 0
    y_offset: int = 0

    def as_record(self) -> dict:
        return {
            "gid": self.gid,
            "cluster": self.cluster,
            "xAdvance": self.x_advance,
            "yAdvance": self.y_advance,
            "xOffset": self.x_offset,
            "yOffset": self.y_offset,
        }


def ot_language_tag(language: str) -> str | None:
    normalized = language.lower().replace("_", "-")
    if normalized.startswith(("zh-cn", "zh-hans")):
        return "ZHS "
    if normalized.startswith(("zh-tw", "zh-hk", "zh-hant")):
        return "ZHT "
    return None


def selected_lookup_plan(font: TTFont, table_tag: str, script_tag: str,
                         language: str, feature_tags: tuple[str, ...]) -> list[tuple[str, int]]:
    table = font[table_tag].table
    script = next((record.Script for record in table.ScriptList.ScriptRecord
                   if record.ScriptTag.lower() == script_tag.lower()), None)
    selected_script = script_tag.lower()
    if script is None:
        script = next((record.Script for record in table.ScriptList.ScriptRecord
                       if record.ScriptTag == "DFLT"), None)
        selected_script = "DFLT"
    if script is None:
        raise ValueError(f"{table_tag} has neither {script_tag} nor DFLT Script")

    language_tag = ot_language_tag(language)
    lang_sys = None
    if language_tag is not None:
        lang_sys = next((record.LangSys for record in script.LangSysRecord
                         if record.LangSysTag == language_tag), None)
    if lang_sys is None:
        lang_sys = script.DefaultLangSys
    if lang_sys is None:
        raise ValueError(f"{table_tag}:{selected_script} has no default LangSys")

    feature_indices = ([lang_sys.ReqFeatureIndex]
                       if lang_sys.ReqFeatureIndex != 0xFFFF else [])
    feature_indices.extend(lang_sys.FeatureIndex)
    plan: list[tuple[str, int]] = []
    for feature_index in feature_indices:
        feature = table.FeatureList.FeatureRecord[feature_index]
        if feature.FeatureTag in feature_tags:
            plan.extend((feature.FeatureTag, lookup_id)
                        for lookup_id in feature.Feature.LookupListIndex)
    return plan


def _glyph_advance(font: TTFont, gid: int) -> int:
    name = font.getGlyphName(gid)
    return int(font["hmtx"].metrics[name][0])


def _lookup(font: TTFont, table_tag: str, lookup_id: int):
    lookup = font[table_tag].table.LookupList.Lookup[lookup_id]
    if lookup.LookupFlag != 0:
        raise ValueError(f"{table_tag} lookup {lookup_id} has unsupported flags {lookup.LookupFlag}")
    return lookup


def _single_substitution(font: TTFont, glyphs: list[Glyph], lookup) -> None:
    if lookup.LookupType != 1:
        raise ValueError(f"expected GSUB SingleSubst, got lookup type {lookup.LookupType}")
    for glyph in glyphs:
        name = font.getGlyphName(glyph.gid)
        for subtable in lookup.SubTable:
            replacement = subtable.mapping.get(name)
            if replacement is not None:
                glyph.gid = font.getGlyphID(replacement)
                glyph.x_advance = _glyph_advance(font, glyph.gid)
                break


def _ligature_substitution(font: TTFont, glyphs: list[Glyph], lookup) -> None:
    if lookup.LookupType != 4:
        raise ValueError(f"expected GSUB LigatureSubst, got lookup type {lookup.LookupType}")
    index = 0
    while index < len(glyphs):
        first_name = font.getGlyphName(glyphs[index].gid)
        match = None
        for subtable in lookup.SubTable:
            for ligature in subtable.ligatures.get(first_name, ()):
                component_count = len(ligature.Component)
                if index + component_count >= len(glyphs):
                    continue
                names = [font.getGlyphName(glyphs[index + offset].gid)
                         for offset in range(1, component_count + 1)]
                if names == ligature.Component:
                    match = (component_count, font.getGlyphID(ligature.LigGlyph))
                    break
            if match is not None:
                break
        if match is None:
            index += 1
            continue
        component_count, replacement_gid = match
        last_index = index + component_count
        cluster = min(glyph.cluster for glyph in glyphs[index:last_index + 1])
        glyphs[index:last_index + 1] = [
            Glyph(replacement_gid, cluster, _glyph_advance(font, replacement_gid))
        ]
        index += 1


def _value_component(value, field: str) -> int:
    if value is None:
        return 0
    device_fields = {
        "XPlacement": "XPlaDevice",
        "YPlacement": "YPlaDevice",
        "XAdvance": "XAdvDevice",
        "YAdvance": "YAdvDevice",
    }
    try:
        device_field = device_fields[field]
    except KeyError as error:
        raise ValueError(f"unsupported GPOS value field {field}") from error
    if getattr(value, device_field, None) is not None:
        raise ValueError(f"unsupported GPOS device adjustment {device_field}")
    return int(getattr(value, field, 0) or 0)


def _pair_values(font: TTFont, lookup, left_gid: int, right_gid: int):
    if lookup.LookupType != 2:
        raise ValueError(f"expected GPOS PairPos, got lookup type {lookup.LookupType}")
    left_name = font.getGlyphName(left_gid)
    right_name = font.getGlyphName(right_gid)
    for subtable in lookup.SubTable:
        coverage = subtable.Coverage.glyphs
        if left_name not in coverage:
            continue
        coverage_index = coverage.index(left_name)
        if subtable.Format == 1:
            pair_set = subtable.PairSet[coverage_index]
            record = next((item for item in pair_set.PairValueRecord
                           if item.SecondGlyph == right_name), None)
            if record is not None:
                return record.Value1, record.Value2
        elif subtable.Format == 2:
            class1 = subtable.ClassDef1.classDefs.get(left_name, 0)
            class2 = subtable.ClassDef2.classDefs.get(right_name, 0)
            if class1 < subtable.Class1Count and class2 < subtable.Class2Count:
                record = subtable.Class1Record[class1].Class2Record[class2]
                if record.Value1 is not None or record.Value2 is not None:
                    return record.Value1, record.Value2
        else:
            raise ValueError(f"unsupported GPOS PairPos format {subtable.Format}")
    return None, None


def _mark_anchor(font: TTFont, lookup, base_gid: int, mark_gid: int):
    if lookup.LookupType != 4:
        raise ValueError(f"expected GPOS MarkBasePos, got lookup type {lookup.LookupType}")
    for subtable in lookup.SubTable:
        if subtable.Format != 1:
            raise ValueError(f"unsupported GPOS MarkBasePos format {subtable.Format}")
        mark_names = subtable.MarkCoverage.glyphs
        base_names = subtable.BaseCoverage.glyphs
        mark_name = font.getGlyphName(mark_gid)
        base_name = font.getGlyphName(base_gid)
        if mark_name not in mark_names or base_name not in base_names:
            continue
        mark_index = mark_names.index(mark_name)
        base_index = base_names.index(base_name)
        mark_record = subtable.MarkArray.MarkRecord[mark_index]
        mark_anchor = mark_record.MarkAnchor
        base_anchor = subtable.BaseArray.BaseRecord[base_index].BaseAnchor[mark_record.Class]
        if mark_anchor is None or base_anchor is None:
            continue
        for anchor in (mark_anchor, base_anchor):
            if getattr(anchor, "XDeviceTable", None) is not None or getattr(anchor, "YDeviceTable", None) is not None:
                raise ValueError("unsupported GPOS anchor device adjustment")
        return base_anchor.XCoordinate, base_anchor.YCoordinate, mark_anchor.XCoordinate, mark_anchor.YCoordinate
    return None


def _apply_pair_position(font: TTFont, glyphs: list[Glyph], lookup) -> None:
    for index in range(len(glyphs) - 1):
        first, second = _pair_values(font, lookup, glyphs[index].gid, glyphs[index + 1].gid)
        glyphs[index].x_advance += _value_component(first, "XAdvance")
        glyphs[index].y_advance += _value_component(first, "YAdvance")
        glyphs[index].x_offset += _value_component(first, "XPlacement")
        glyphs[index].y_offset += _value_component(first, "YPlacement")
        glyphs[index + 1].x_advance += _value_component(second, "XAdvance")
        glyphs[index + 1].y_advance += _value_component(second, "YAdvance")
        glyphs[index + 1].x_offset += _value_component(second, "XPlacement")
        glyphs[index + 1].y_offset += _value_component(second, "YPlacement")


def _apply_mark_position(font: TTFont, glyphs: list[Glyph], lookup) -> None:
    if lookup.LookupType != 4:
        raise ValueError(f"expected GPOS MarkBasePos, got lookup type {lookup.LookupType}")
    for index in range(1, len(glyphs)):
        anchors = _mark_anchor(font, lookup, glyphs[index - 1].gid, glyphs[index].gid)
        if anchors is None:
            continue
        base_x, base_y, mark_x, mark_y = anchors
        base = glyphs[index - 1]
        glyphs[index].x_offset += base.x_offset - base.x_advance + base_x - mark_x
        glyphs[index].y_offset += base.y_offset - base.y_advance + base_y - mark_y


def _canonical_scalars(text: str, cmap: dict[int, str]) -> list[tuple[str, int]]:
    """Apply font-aware canonical composition and retain source starter clusters."""
    result: list[tuple[str, int]] = []
    sequence = ""
    cluster = 0

    def append_sequence(source: str, source_cluster: int) -> None:
        decomposed = unicodedata.normalize("NFD", source)
        composed: list[str] = []
        starter_index = -1
        last_combining_class = 0
        for character in decomposed:
            combining_class = unicodedata.combining(character)
            candidate = (unicodedata.normalize("NFC", composed[starter_index] + character)
                         if starter_index >= 0 and
                         (last_combining_class < combining_class or last_combining_class == 0)
                         else "")
            if len(candidate) == 1 and ord(candidate) in cmap:
                composed[starter_index] = candidate
                continue
            composed.append(character)
            if combining_class == 0:
                starter_index = len(composed) - 1
            last_combining_class = combining_class
        result.extend((scalar, source_cluster) for scalar in composed)

    for scalar_index, character in enumerate(text):
        if sequence and unicodedata.combining(character) == 0:
            append_sequence(sequence, cluster)
            sequence = ""
        if not sequence:
            cluster = scalar_index
        sequence += character
    if sequence:
        append_sequence(sequence, cluster)
    return result


def prototype_shape(font: TTFont, text: str, script: str, language: str) -> list[dict]:
    cmap = font.getBestCmap()
    glyphs: list[Glyph] = []
    for character, cluster in _canonical_scalars(text, cmap):
        name = cmap.get(ord(character), ".notdef")
        gid = font.getGlyphID(name)
        glyphs.append(Glyph(gid, cluster, _glyph_advance(font, gid)))

    gsub_plan = selected_lookup_plan(font, "GSUB", script, language,
                                     ("ccmp", "locl", "liga"))
    for feature_tag, lookup_id in gsub_plan:
        lookup = _lookup(font, "GSUB", lookup_id)
        if lookup.LookupType == 1:
            _single_substitution(font, glyphs, lookup)
        elif lookup.LookupType == 4:
            _ligature_substitution(font, glyphs, lookup)
        else:
            raise ValueError(f"unsupported selected GSUB lookup type {lookup.LookupType} ({feature_tag})")

    gpos_plan = selected_lookup_plan(font, "GPOS", script, language, ("kern", "mark"))
    for feature_tag, lookup_id in gpos_plan:
        lookup = _lookup(font, "GPOS", lookup_id)
        if feature_tag == "kern" and lookup.LookupType == 2:
            _apply_pair_position(font, glyphs, lookup)
        elif feature_tag == "mark" and lookup.LookupType == 4:
            _apply_mark_position(font, glyphs, lookup)
        else:
            raise ValueError(f"unsupported selected GPOS lookup type {lookup.LookupType} ({feature_tag})")
    return [glyph.as_record() for glyph in glyphs]


def _selected_output_gids(font: TTFont, table_tag: str, script: str,
                          language: str, feature_tags: tuple[str, ...]) -> set[int]:
    table = font[table_tag].table
    output: set[int] = set()
    for _, lookup_id in selected_lookup_plan(font, table_tag, script, language, feature_tags):
        lookup = _lookup(font, table_tag, lookup_id)
        if table_tag == "GSUB" and lookup.LookupType == 1:
            for subtable in lookup.SubTable:
                output.update(font.getGlyphID(name) for name in subtable.mapping.values())
        elif table_tag == "GSUB" and lookup.LookupType == 4:
            for subtable in lookup.SubTable:
                output.update(font.getGlyphID(ligature.LigGlyph)
                              for ligatures in subtable.ligatures.values()
                              for ligature in ligatures)
        elif table_tag == "GPOS" and lookup.LookupType == 4:
            for subtable in lookup.SubTable:
                if subtable.Format != 1:
                    raise ValueError(f"unsupported selected MarkBasePos format {subtable.Format}")
                output.update(font.getGlyphID(name) for name in subtable.MarkCoverage.glyphs)
        elif table_tag == "GPOS" and lookup.LookupType == 2:
            continue
        else:
            raise ValueError(f"unsupported selected {table_tag} lookup type {lookup.LookupType}")
    return output


def missing_output_gids(font: TTFont, jffont_gids: set[int]) -> list[int]:
    output: set[int] = set()
    for script, language in PROFILE_SCRIPTS:
        output.update(_selected_output_gids(font, "GSUB", script, language,
                                            ("ccmp", "locl", "liga")))
        output.update(_selected_output_gids(font, "GPOS", script, language,
                                            ("kern", "mark")))
    return sorted(output - jffont_gids)


def parse_jffont2_gids(data: bytes) -> tuple[set[int], dict]:
    if len(data) < JFFONT2_HEADER_BYTES or data[:8] != JFFONT2_MAGIC:
        raise ValueError("invalid JFFONT2 header")
    glyph_count = int.from_bytes(data[20:24], "little")
    glyph_offset = int.from_bytes(data[24:28], "little")
    bitmap_offset = int.from_bytes(data[28:32], "little")
    if (len(data) != int.from_bytes(data[16:20], "little") or
            glyph_count != JFFONT2_EXPECTED_GLYPHS or glyph_offset != JFFONT2_HEADER_BYTES or
            bitmap_offset != glyph_offset + glyph_count * JFFONT2_RECORD_BYTES):
        raise ValueError("unexpected JFFONT2 size or record layout")
    digest = hashlib.sha256(data).hexdigest()
    if digest != JFFONT2_SHA256:
        raise ValueError("JFFONT2 SHA-256 does not match the pinned product resource")
    gids = {int.from_bytes(data[glyph_offset + index * JFFONT2_RECORD_BYTES +
                                JFFONT2_GLYPH_ID_OFFSET:glyph_offset +
                                index * JFFONT2_RECORD_BYTES + JFFONT2_GLYPH_ID_OFFSET + 4], "little")
            for index in range(glyph_count)}
    if 0 in gids:
        raise ValueError("JFFONT2 contains forbidden .notdef GID zero")
    return gids, {"sha256": digest, "bytes": len(data), "glyphMappings": glyph_count,
                  "distinctGlyphIds": len(gids)}


def _case_report(font_bytes: bytes, font: TTFont, name: str, text: str,
                 script: str, language: str) -> dict:
    expected = harfbuzz_shape(font_bytes, text, script, language, PROFILE_FEATURES)
    actual = prototype_shape(font, text, script, language)
    return {
        "case": name,
        "input": text.encode("unicode_escape").decode("ascii"),
        "script": script,
        "language": language,
        "match": actual == expected["glyphs"],
        "prototype": actual,
        "harfbuzz": expected["glyphs"],
    }


def generate(font_dir: Path, jffont_path: Path, output_dir: Path,
             raster_size: int, max_catalog_bytes: int) -> dict:
    jffont_gids, jffont_report = parse_jffont2_gids(jffont_path.read_bytes())
    face_data = {}
    face_fonts = {}
    missing_by_face = {}
    for face, expected_hash in FACES.items():
        font_path = font_dir / f"NotoSansSC-{face}.otf"
        data = font_path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if digest != expected_hash:
            raise ValueError(f"{font_path.name} SHA-256 does not match the pinned product face")
        font = TTFont(font_path, lazy=False)
        face_fonts[face] = font
        missing_by_face[face] = missing_output_gids(font, jffont_gids)
        face_data[face] = {
            "file": font_path.name,
            "bytes": len(data),
            "sha256": digest,
            "glyphCount": int(font["maxp"].numGlyphs),
            "unitsPerEm": int(font["head"].unitsPerEm),
            "outputGidsMissingFromJffont2": missing_by_face[face],
        }
    if len({tuple(gids) for gids in missing_by_face.values()}) != 1:
        raise ValueError("selected output-GID closure differs across product weights")

    output_dir.mkdir(parents=True, exist_ok=False)
    report = {
        "profile": list(PROFILE_FEATURES),
        "scriptLanguageSelections": [
            {"script": script, "language": language} for script, language in PROFILE_SCRIPTS
        ],
        "prototypeLimits": [
            "horizontal LTR scalar input",
            "GSUB SingleSubst and LigatureSubst only",
            "GPOS PairPos formats 1/2 and MarkBasePos format 1 only",
            "zero lookup flags and no device adjustments",
            "adjacent-base mark attachment; no mark-to-mark, mark-to-ligature or contextual lookups",
            "no fallback, bidi, wrapping, or production Core integration",
        ],
        "dependencies": {"fontTools": fontTools.__version__, "uharfbuzz": hb.__version__},
        "jffont2": jffont_report,
        "faces": face_data,
        "comparisons": {},
    }
    if fontTools.__version__ != FONTTOOLS_VERSION or hb.__version__ != UHARFBUZZ_VERSION:
        raise ValueError(f"requires fontTools=={FONTTOOLS_VERSION} and uharfbuzz=={UHARFBUZZ_VERSION}")

    for face in FACES:
        font_path = font_dir / f"NotoSansSC-{face}.otf"
        data = font_path.read_bytes()
        font = face_fonts[face]
        cases = []
        for name, text, script, language in CASES:
            cases.append(_case_report(data, font, name, text, script, language))
        if any(not case["match"] for case in cases):
            raise ValueError(f"prototype/HarfBuzz mismatch for {face}")
        report["comparisons"][face] = cases

        glyph_ids = missing_by_face[face]
        catalog, catalog_report = build_catalog(data, glyph_ids, raster_size, max_catalog_bytes)
        catalog_path = output_dir / f"noto-sans-sc-{face.lower()}-shaping-gids.jfgid"
        catalog_path.write_bytes(catalog)
        report["faces"][face]["catalog"] = {
            "file": catalog_path.name,
            "bytes": len(catalog),
            "sha256": hashlib.sha256(catalog).hexdigest(),
            "glyphCount": len(glyph_ids),
            "bitmapBytes": catalog_report["bitmapBytes"],
            "rasterPixelSize": raster_size,
        }
    report_path = output_dir / "shaping-closure-report.json"
    report_path.write_text(json.dumps(report, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font-dir", required=True, type=Path)
    parser.add_argument("--jffont2", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--raster-size", type=int, default=16)
    parser.add_argument("--max-catalog-bytes", type=int, default=8 * 1024 * 1024)
    args = parser.parse_args()
    try:
        report = generate(args.font_dir, args.jffont2, args.output_dir,
                          args.raster_size, args.max_catalog_bytes)
    except (OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 2
    summary = {
        face: report["faces"][face]["catalog"] for face in FACES
    }
    summary["oracleComparisons"] = {
        face: len(report["comparisons"][face]) for face in FACES
    }
    print(json.dumps(summary, ensure_ascii=True, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
