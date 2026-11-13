#!/usr/bin/env python3
"""Encode the admitted Noto shaping lookup closure as a deterministic binary spike."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

from fontTools.ttLib import TTFont

import noto_shaping_closure_prototype as closure


MAGIC = b"JFSHAP1\0"
VERSION = 1
HEADER = struct.Struct("<8sHHIIIIII")
SELECTION = struct.Struct("<4s4sHHI")
REFERENCE = struct.Struct("<4sH2x")
LOOKUP = struct.Struct("<BBHHII")
SUBTABLE = struct.Struct("<BBI")
VALUE_FIELDS = ("XPlacement", "YPlacement", "XAdvance", "YAdvance")
TABLE_CODES = {"GSUB": 1, "GPOS": 2}


def _u16(value: int, name: str) -> bytes:
    if not 0 <= value <= 0xFFFF:
        raise ValueError(f"{name} is outside uint16")
    return struct.pack("<H", value)


def _i16(value: int, name: str) -> bytes:
    if not -0x8000 <= value <= 0x7FFF:
        raise ValueError(f"{name} is outside int16")
    return struct.pack("<h", value)


def _gid(font: TTFont, glyph_name: str) -> int:
    gid = font.getGlyphID(glyph_name)
    _u16(gid, "glyph ID")
    return gid


def _value(value) -> bytes:
    mask = 0
    fields = bytearray()
    for bit, field in enumerate(VALUE_FIELDS):
        component = closure._value_component(value, field)
        if component:
            mask |= 1 << bit
            fields.extend(_i16(component, f"GPOS {field}"))
    return bytes((mask,)) + fields


def _anchor(anchor) -> bytes:
    if anchor is None:
        return b"\0"
    if getattr(anchor, "Format", 1) != 1:
        raise ValueError("only coordinate-only GPOS anchors (Anchor format 1) are encodable")
    if getattr(anchor, "XDeviceTable", None) is not None or getattr(anchor, "YDeviceTable", None) is not None:
        raise ValueError("unsupported GPOS anchor device adjustment")
    return b"\1" + _i16(int(anchor.XCoordinate), "anchor X") + _i16(int(anchor.YCoordinate), "anchor Y")


def _encode_single(font: TTFont, subtable) -> bytes:
    mapping = sorted((_gid(font, source), _gid(font, target))
                     for source, target in subtable.mapping.items())
    if len({source for source, _ in mapping}) != len(mapping):
        raise ValueError("duplicate SingleSubst source glyph")
    return struct.pack("<I", len(mapping)) + b"".join(
        _u16(source, "source glyph") + _u16(target, "target glyph") for source, target in mapping)


def _encode_ligature(font: TTFont, subtable) -> bytes:
    rows = []
    for first_name in sorted(subtable.ligatures, key=lambda name: _gid(font, name)):
        # Preserve each LigatureSet's order: it carries match precedence for shared prefixes.
        for record in subtable.ligatures[first_name]:
            components = [_gid(font, first_name)] + [_gid(font, name) for name in record.Component]
            if len(components) > 0xFF:
                raise ValueError("LigatureSubst component count exceeds uint8")
            rows.append((components, _gid(font, record.LigGlyph)))
    out = bytearray(struct.pack("<I", len(rows)))
    for components, output in rows:
        out.extend(struct.pack("<B", len(components)))
        out.extend(_u16(output, "ligature output"))
        out.extend(b"".join(_u16(gid, "ligature component") for gid in components))
    return bytes(out)


def _encode_pair1(font: TTFont, subtable) -> bytes:
    if len(subtable.Coverage.glyphs) != len(subtable.PairSet):
        raise ValueError("PairPos format 1 coverage and PairSet counts differ")
    rows = []
    for first_name, pair_set in zip(subtable.Coverage.glyphs, subtable.PairSet):
        first_gid = _gid(font, first_name)
        for pair in pair_set.PairValueRecord:
            rows.append((first_gid, _gid(font, pair.SecondGlyph), pair.Value1, pair.Value2))
    rows.sort(key=lambda row: (row[0], row[1]))
    out = bytearray(struct.pack("<I", len(rows)))
    for first, second, value1, value2 in rows:
        out.extend(_u16(first, "PairPos first glyph"))
        out.extend(_u16(second, "PairPos second glyph"))
        out.extend(_value(value1))
        out.extend(_value(value2))
    return bytes(out)


def _class_assignments(font: TTFont, class_def) -> list[tuple[int, int]]:
    return sorted((_gid(font, name), int(class_id)) for name, class_id in class_def.classDefs.items())


def _encode_pair2(font: TTFont, subtable) -> bytes:
    class1_count, class2_count = int(subtable.Class1Count), int(subtable.Class2Count)
    if not 1 <= class1_count <= 0xFFFF or not 1 <= class2_count <= 0xFFFF:
        raise ValueError("PairPos class count is outside uint16")
    out = bytearray()
    coverage = sorted(_gid(font, name) for name in subtable.Coverage.glyphs)
    out.extend(_u16(len(coverage), "PairPos coverage count"))
    out.extend(b"".join(_u16(gid, "PairPos coverage glyph") for gid in coverage))
    out.extend(_u16(class1_count, "PairPos Class1Count"))
    out.extend(_u16(class2_count, "PairPos Class2Count"))
    for assignments, class_count in ((_class_assignments(font, subtable.ClassDef1), class1_count),
                                    (_class_assignments(font, subtable.ClassDef2), class2_count)):
        out.extend(_u16(len(assignments), "PairPos class assignment count"))
        for gid, class_id in assignments:
            if class_id >= class_count:
                raise ValueError("PairPos class assignment exceeds declared class count")
            out.extend(_u16(gid, "PairPos class glyph"))
            out.extend(_u16(class_id, "PairPos class ID"))
    for class1 in subtable.Class1Record:
        for pair in class1.Class2Record:
            out.extend(_value(pair.Value1))
            out.extend(_value(pair.Value2))
    return bytes(out)


def _encode_mark_base(font: TTFont, subtable) -> bytes:
    class_count = int(subtable.ClassCount)
    if not 0 <= class_count <= 0xFFFF:
        raise ValueError("MarkBasePos ClassCount is outside uint16")
    if len(subtable.MarkArray.MarkRecord) != len(subtable.MarkCoverage.glyphs):
        raise ValueError("MarkBasePos MarkCoverage and MarkArray counts differ")
    if len(subtable.BaseArray.BaseRecord) != len(subtable.BaseCoverage.glyphs):
        raise ValueError("MarkBasePos BaseCoverage and BaseArray counts differ")
    marks = []
    for record in subtable.MarkArray.MarkRecord:
        if not 0 <= int(record.Class) < class_count:
            raise ValueError("MarkBasePos mark class is outside ClassCount")
        marks.append((_gid(font, subtable.MarkCoverage.glyphs[len(marks)]),
                      int(record.Class), _anchor(record.MarkAnchor)))
    bases = []
    for index, record in enumerate(subtable.BaseArray.BaseRecord):
        anchors = [_anchor(anchor) for anchor in record.BaseAnchor]
        bases.append((_gid(font, subtable.BaseCoverage.glyphs[index]), anchors))
    out = bytearray(_u16(class_count, "MarkBasePos ClassCount"))
    out.extend(_u16(len(marks), "MarkArray count"))
    for gid, mark_class, anchor in marks:
        out.extend(_u16(gid, "mark glyph"))
        out.extend(_u16(mark_class, "mark class"))
        out.extend(anchor)
    out.extend(_u16(len(bases), "BaseArray count"))
    for gid, anchors in bases:
        if len(anchors) != class_count:
            raise ValueError("MarkBasePos BaseAnchor class count mismatch")
        out.extend(_u16(gid, "base glyph"))
        for anchor in anchors:
            out.extend(anchor)
    return bytes(out)


def _encode_subtable(font: TTFont, table_tag: str, lookup_type: int, subtable) -> tuple[int, bytes]:
    if table_tag == "GSUB" and lookup_type == 1:
        return 1, _encode_single(font, subtable)
    if table_tag == "GSUB" and lookup_type == 4:
        return 4, _encode_ligature(font, subtable)
    if table_tag == "GPOS" and lookup_type == 2:
        if subtable.Format == 1:
            return 1, _encode_pair1(font, subtable)
        if subtable.Format == 2:
            return 2, _encode_pair2(font, subtable)
        raise ValueError(f"unsupported GPOS PairPos format {subtable.Format}")
    if table_tag == "GPOS" and lookup_type == 4:
        if subtable.Format != 1:
            raise ValueError(f"unsupported GPOS MarkBasePos format {subtable.Format}")
        return 1, _encode_mark_base(font, subtable)
    raise ValueError(f"unsupported selected {table_tag} lookup type {lookup_type}")


def build_encoded(font: TTFont) -> tuple[bytes, dict]:
    selections = []
    lookup_keys: list[tuple[str, int]] = []
    references: list[tuple[str, int]] = []
    for script, language in closure.PROFILE_SCRIPTS:
        first_reference = len(references)
        plans = (
            ("GSUB", closure.selected_lookup_plan(font, "GSUB", script, language,
                                                  ("ccmp", "locl", "liga"))),
            ("GPOS", closure.selected_lookup_plan(font, "GPOS", script, language,
                                                  ("kern", "mark"))),
        )
        for table_tag, plan in plans:
            for feature_tag, lookup_id in plan:
                key = (table_tag, lookup_id)
                if key not in lookup_keys:
                    lookup_keys.append(key)
                references.append((feature_tag, lookup_keys.index(key)))
        if len(references) - first_reference > 0xFFFF:
            raise ValueError("selection lookup references exceed uint16")
        language_tag = closure.ot_language_tag(language) or "    "
        selections.append((script.lower(), language_tag, first_reference,
                           len(references) - first_reference))

    payloads = []
    lookup_meta = []
    for table_tag, lookup_id in lookup_keys:
        lookup = closure._lookup(font, table_tag, lookup_id)
        subtables = [_encode_subtable(font, table_tag, lookup.LookupType, subtable)
                     for subtable in lookup.SubTable]
        payload = bytearray()
        for format_tag, data in subtables:
            payload.extend(SUBTABLE.pack(lookup.LookupType, format_tag, len(data)))
            payload.extend(data)
        if len(payload) > 0xFFFFFFFF:
            raise ValueError("encoded lookup exceeds uint32")
        payloads.append(bytes(payload))
        lookup_meta.append({"table": table_tag, "lookupId": lookup_id,
                            "lookupType": lookup.LookupType, "subtableCount": len(subtables),
                            "payloadBytes": len(payload)})

    selection_offset = HEADER.size
    references_offset = selection_offset + len(selections) * SELECTION.size
    lookup_dir_offset = references_offset + len(references) * REFERENCE.size
    payload_offset = lookup_dir_offset + len(lookup_keys) * LOOKUP.size
    directories = bytearray()
    payload_blob = bytearray()
    cursor = payload_offset
    for (table_tag, lookup_id), payload, meta in zip(lookup_keys, payloads, lookup_meta):
        lookup = closure._lookup(font, table_tag, lookup_id)
        directories.extend(LOOKUP.pack(TABLE_CODES[table_tag], lookup.LookupType,
                                       len(lookup.SubTable), lookup_id, cursor, len(payload)))
        payload_blob.extend(payload)
        cursor += len(payload)

    out = bytearray(HEADER.pack(MAGIC, VERSION, HEADER.size, cursor, len(selections),
                                len(lookup_keys), selection_offset, references_offset,
                                lookup_dir_offset))
    for script, language_tag, first, count in selections:
        out.extend(SELECTION.pack(script.encode("ascii"), language_tag.encode("ascii"),
                                  first, count, 0))
    for feature_tag, lookup_index in references:
        if not 0 <= lookup_index <= 0xFFFF:
            raise ValueError("lookup reference index exceeds uint16")
        out.extend(REFERENCE.pack(feature_tag.encode("ascii"), lookup_index))
    out.extend(directories)
    out.extend(payload_blob)
    if len(out) != cursor:
        raise AssertionError("encoded byte count does not match header")
    return bytes(out), {
        "format": "JFSHAP1",
        "version": VERSION,
        "encodedBytes": len(out),
        "encodedUpperBoundBytes": len(out),
        "headerBytes": HEADER.size,
        "selectionCount": len(selections),
        "lookupReferenceCount": len(references),
        "uniqueLookupCount": len(lookup_keys),
        "selectionDirectoryBytes": len(selections) * SELECTION.size,
        "lookupReferenceBytes": len(references) * REFERENCE.size,
        "lookupDirectoryBytes": len(lookup_keys) * LOOKUP.size,
        "payloadBytes": len(payload_blob),
        "sha256": hashlib.sha256(out).hexdigest(),
        "lookups": lookup_meta,
    }


def generate(font_dir: Path, output_dir: Path) -> dict:
    output_dir.mkdir(parents=True, exist_ok=False)
    report = {"profile": list(closure.PROFILE_FEATURES),
              "scriptLanguageSelections": [{"script": s, "language": l}
                                            for s, l in closure.PROFILE_SCRIPTS],
              "faces": {}}
    for face, expected_hash in closure.FACES.items():
        path = font_dir / f"NotoSansSC-{face}.otf"
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if digest != expected_hash:
            raise ValueError(f"{path.name} SHA-256 does not match the pinned product face")
        with TTFont(path, lazy=False) as font:
            encoded, stats = build_encoded(font)
        resource = f"noto-sans-sc-{face.lower()}-shaping.jfshape"
        (output_dir / resource).write_bytes(encoded)
        report["faces"][face] = {"fontBytes": len(data), "fontSha256": digest,
                                 "encodedResource": resource, **stats}
    (output_dir / "encoded-profile-report.json").write_text(
        json.dumps(report, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    try:
        report = generate(args.font_dir, args.output_dir)
    except (OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 2
    print(json.dumps({face: {"encodedBytes": item["encodedBytes"],
                             "encodedUpperBoundBytes": item["encodedUpperBoundBytes"],
                             "uniqueLookupCount": item["uniqueLookupCount"]}
                      for face, item in report["faces"].items()}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
