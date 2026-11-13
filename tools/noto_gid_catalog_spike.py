#!/usr/bin/env python3
"""Build an experimental, offline GID-keyed bitmap catalog from an OTF.

This spike format is not JFFONT and is not a runtime contract. The temporary
PUA cmap exists only to let Pillow/FreeType rasterize explicit glyph IDs.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import struct
import sys
import zlib
from pathlib import Path

import PIL
from PIL import Image, ImageDraw, ImageFont, features
import fontTools
from fontTools.ttLib import TTFont, newTable
from fontTools.ttLib.tables._c_m_a_p import CmapSubtable


MAGIC = b"JFGID1\0\0"
VERSION = 1
HEADER_BYTES = 96
RECORD_BYTES = 32
COVERAGE_BITS = 4
DEFAULT_MAX_BYTES = 8 * 1024 * 1024
PRODUCT_REGULAR_SHA256 = "a2b93e6c2db05d6bbbf6f27d413ec73269735b7b679019c8a5aa9670ff0ffbf2"
PUA_BASE = 0xF0000
PUA_LIMIT = 0xFFFFD
# 96-byte header: identity, dimensions/counts, source metadata, SHA256, CRC32.
HEADER = struct.Struct("<8sHHHBBIIIIIHHHHiii32sI4s")
# GID, reserved, original advance, signed bearings, dimensions, coverage extent.
RECORD = struct.Struct("<HHIiiHHIII")


def _pua_font_bytes(source_bytes: bytes, glyph_ids: list[int]) -> tuple[bytes, dict[int, int]]:
    if len(glyph_ids) > PUA_LIMIT - PUA_BASE + 1:
        raise ValueError("too many glyph IDs for the temporary PUA cmap")
    font = TTFont(io.BytesIO(source_bytes), lazy=False)
    names = font.getGlyphOrder()
    mapping = {PUA_BASE + index: names[gid] for index, gid in enumerate(glyph_ids)}
    cmap = newTable("cmap")
    cmap.tableVersion = 0
    subtable = CmapSubtable.newSubtable(12)
    subtable.platformID = 3
    subtable.platEncID = 10
    subtable.language = 0
    subtable.cmap = mapping
    cmap.tables = [table for table in font["cmap"].tables if table.format != 12]
    cmap.tables.append(subtable)
    font["cmap"] = cmap
    output = io.BytesIO()
    font.save(output, reorderTables=True)
    return output.getvalue(), {gid: cp for cp, gid in zip(mapping, glyph_ids)}


def _pack_coverage(image: Image.Image) -> tuple[int, int, bytes]:
    width, height = image.size
    stride = (width * COVERAGE_BITS + 7) // 8
    packed = bytearray(stride * height)
    pixels = image.load()
    for y in range(height):
        for x in range(width):
            value = (pixels[x, y] * 15 + 127) // 255
            bit = x * COVERAGE_BITS
            packed[y * stride + bit // 8] |= value << (8 - COVERAGE_BITS - bit % 8)
    return stride, height, bytes(packed)


def build_catalog(source_bytes: bytes, glyph_ids, size: int = 16,
                  max_bytes: int = DEFAULT_MAX_BYTES) -> tuple[bytes, dict]:
    """Return deterministic prototype bytes and a JSON-compatible report.

    glyph_ids must be a strictly increasing, non-empty iterable of font GIDs.
    Rasterization uses Pillow BASIC/FreeType through a temporary PUA cmap.
    """
    ids = list(glyph_ids)
    if not source_bytes or not 1 <= size <= 4096 or max_bytes < HEADER_BYTES:
        raise ValueError("invalid source, raster size, or byte budget")
    if not ids or len(ids) > 65535 or any(type(gid) is not int for gid in ids):
        raise ValueError("glyph IDs must be a non-empty list of at most 65535 integers")
    if any(gid < 0 or gid > 65535 for gid in ids):
        raise ValueError("glyph ID is outside the supported u16 range")
    if any(left >= right for left, right in zip(ids, ids[1:])):
        raise ValueError("glyph IDs must be strictly increasing and unique")

    with TTFont(io.BytesIO(source_bytes), lazy=False) as source:
        if "fvar" in source:
            raise ValueError("variable fonts are not supported by this spike")
        glyph_count = int(source["maxp"].numGlyphs)
        units = int(source["head"].unitsPerEm)
        weight = int(source["OS/2"].usWeightClass)
        ascent = int(source["hhea"].ascent)
        descent = -int(source["hhea"].descent)
        line_gap = int(source["hhea"].lineGap)
        order = source.getGlyphOrder()
        advances = [int(source["hmtx"].metrics[order[gid]][0])
                    if 0 <= gid < glyph_count else -1 for gid in ids]
    if not 1 <= units <= 16384 or not 1 <= weight <= 1000:
        raise ValueError("unsupported font units or weight")
    if ascent < 0 or descent < 0 or any(not 0 <= advance <= 1 << 20 for advance in advances):
        raise ValueError("unsupported font metrics")
    if ids[-1] >= glyph_count:
        raise ValueError("glyph ID is outside the font glyph order")

    bridged_bytes, pua_for_gid = _pua_font_bytes(source_bytes, ids)
    raster_font = ImageFont.truetype(io.BytesIO(bridged_bytes), size=size,
                                     layout_engine=ImageFont.Layout.BASIC)
    records: list[bytes] = []
    coverage = bytearray()
    glyph_reports = []
    for gid, advance in zip(ids, advances):
        character = chr(pua_for_gid[gid])
        left, top, right, bottom = raster_font.getbbox(character, anchor="ls")
        width, height = right - left, bottom - top
        if width < 0 or height < 0 or width > 4096 or height > 4096:
            raise ValueError("glyph raster exceeds profile bounds")
        image = None
        if width and height:
            image = Image.new("L", (width, height), 0)
            ImageDraw.Draw(image).text((-left, -top), character, font=raster_font,
                                       fill=255, anchor="ls")
            ink = image.getbbox()
            if ink:
                left += ink[0]
                top += ink[1]
                image = image.crop(ink)
            else:
                image = None
        if image is None:
            left = top = width = height = stride = 0
            packed = b""
        else:
            width, height = image.size
            stride, _, packed = _pack_coverage(image)
        if abs(left) > 1 << 20 or abs(top) > 1 << 20:
            raise ValueError("glyph bearing exceeds profile bounds")
        if HEADER_BYTES + (len(records) + 1) * RECORD_BYTES + len(coverage) + len(packed) > max_bytes:
            raise ValueError("experimental catalog exceeds byte budget")
        record = RECORD.pack(gid, 0, advance, left, top, width, height,
                             stride, len(coverage), len(packed))
        records.append(record)
        coverage.extend(packed)
        glyph_reports.append({"glyphId": gid, "advance": advance, "bearingX": left,
                              "bearingY": top, "width": width, "height": height,
                              "stride": stride, "coverageBytes": len(packed)})

    records_offset = HEADER_BYTES
    bitmap_offset = records_offset + len(records) * RECORD_BYTES
    total_bytes = bitmap_offset + len(coverage)
    header = HEADER.pack(MAGIC, VERSION, HEADER_BYTES, RECORD_BYTES, COVERAGE_BITS, 0,
                         total_bytes, len(records), records_offset, bitmap_offset,
                         len(coverage), units, size, weight, 0, ascent, descent,
                         line_gap, hashlib.sha256(source_bytes).digest(), 0, b"\0" * 4)
    blob = bytearray(header + b"".join(records) + coverage)
    checksum = zlib.crc32(blob)
    struct.pack_into("<I", blob, 88, checksum)
    result = bytes(blob)
    report = {
        "format": "JFGID1 experimental offline catalog",
        "version": VERSION,
        "sourceSha256": hashlib.sha256(source_bytes).hexdigest(),
        "catalogSha256": hashlib.sha256(result).hexdigest(),
        "catalogBytes": len(result),
        "glyphCount": len(ids),
        "glyphIds": ids,
        "bitmapBytes": len(coverage),
        "rasterPixelSize": size,
        "coverageBits": COVERAGE_BITS,
        "unitsPerEm": units,
        "weight": weight,
        "metrics": {"ascent": ascent, "descent": descent, "lineGap": line_gap},
        "raster": {"pillow": PIL.__version__, "fontTools": fontTools.__version__,
                   "freeType": features.version_module("freetype2"),
                   "layout": "BASIC", "temporaryCmap": "PUA format 12"},
        "glyphs": glyph_reports,
    }
    return result, report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", required=True, type=Path)
    parser.add_argument("--glyph-ids", required=True, type=Path,
                        help="UTF-8 text file containing one strictly increasing GID per line")
    parser.add_argument("--size", type=int, default=16)
    parser.add_argument("--max-bytes", type=int, default=DEFAULT_MAX_BYTES)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--scalar-baseline-bytes", type=int,
                        help="optional existing scalar-resource size for comparison")
    args = parser.parse_args()
    if args.output.exists() or args.report.exists() or args.output.resolve() == args.report.resolve():
        parser.error("refuse to overwrite outputs or alias catalog and report")
    try:
        ids = [int(line.strip(), 0) for line in args.glyph_ids.read_text(encoding="utf-8").splitlines()
               if line.strip() and not line.lstrip().startswith("#")]
        source_bytes = args.font.read_bytes()
        source_hash = hashlib.sha256(source_bytes).hexdigest()
        if source_hash != PRODUCT_REGULAR_SHA256:
            raise ValueError("font SHA256 does not match product Noto Sans SC 2.002 Regular")
        blob, report = build_catalog(source_bytes, ids, args.size, args.max_bytes)
        if args.scalar_baseline_bytes is not None:
            if args.scalar_baseline_bytes <= 0:
                raise ValueError("scalar baseline size must be positive")
            report["scalarBaselineBytes"] = args.scalar_baseline_bytes
            report["catalogPercentOfScalarBaseline"] = round(
                len(blob) * 100.0 / args.scalar_baseline_bytes, 3)
        with args.output.open("xb") as output:
            output.write(blob)
        with args.report.open("x", encoding="utf-8") as output:
            json.dump(report, output, indent=2)
            output.write("\n")
        print(json.dumps({key: report[key] for key in
                          ("catalogSha256", "catalogBytes", "glyphCount", "bitmapBytes")}))
        return 0
    except (OSError, ValueError, struct.error) as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
