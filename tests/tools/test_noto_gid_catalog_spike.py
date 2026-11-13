"""Focused contracts for the experimental offline GID bitmap catalog."""

from __future__ import annotations

import hashlib
import importlib.util
import io
import struct
import unittest
import zlib
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "noto_gid_catalog_spike", ROOT / "tools" / "noto_gid_catalog_spike.py")
spike = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(spike)


def fixture_font() -> bytes:
    builder = FontBuilder(1000, isTTF=True)
    order = [".notdef", "space", "A", "unencoded_mark", "unencoded_ligature"]
    builder.setupGlyphOrder(order)
    builder.setupCharacterMap({32: "space", 65: "A"})
    glyphs = {}
    bounds = {".notdef": (0, 0, 400, 600), "space": None,
              "A": (0, 0, 500, 700), "unencoded_mark": (100, 600, 300, 800),
              "unencoded_ligature": (0, 0, 700, 700)}
    for name, box in bounds.items():
        pen = TTGlyphPen(None)
        if box:
            left, bottom, right, top = box
            pen.moveTo((left, bottom))
            pen.lineTo((right, bottom))
            pen.lineTo((right, top))
            pen.lineTo((left, top))
            pen.closePath()
        glyphs[name] = pen.glyph()
    builder.setupGlyf(glyphs)
    builder.setupHorizontalMetrics({".notdef": (500, 0), "space": (250, 0),
                                    "A": (600, 0), "unencoded_mark": (0, 100),
                                    "unencoded_ligature": (700, 0)})
    builder.setupHorizontalHeader(ascent=800, descent=-200, lineGap=50)
    builder.setupNameTable({"familyName": "GID Fixture", "styleName": "Regular",
                            "uniqueFontIdentifier": "GID Fixture 1",
                            "fullName": "GID Fixture Regular", "psName": "GIDFixture-Regular"})
    builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, sTypoLineGap=50,
                     usWinAscent=900, usWinDescent=300, usWeightClass=400)
    builder.setupPost()
    builder.font["head"].created = builder.font["head"].modified = 2082844800
    builder.font.recalcTimestamp = False
    output = io.BytesIO()
    builder.save(output)
    return output.getvalue()


def unpack(blob):
    header = spike.HEADER.unpack_from(blob)
    magic, version, header_bytes, record_bytes, bits = header[:5]
    total, count, records_offset, bitmap_offset, bitmap_bytes = header[6:11]
    return (magic, version, header_bytes, record_bytes, bits, total, count,
            records_offset, bitmap_offset, bitmap_bytes)


def decode_nibble(blob: bytes, record: tuple, x: int, y: int) -> int:
    _, _, _, _, _, width, height, stride, offset, length = record
    if not (0 <= x < width and 0 <= y < height and length == stride * height):
        raise ValueError("pixel outside bitmap")
    packed = blob[unpack(blob)[8] + offset + y * stride + x // 2]
    return packed >> 4 if x % 2 == 0 else packed & 0x0F


class GidCatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.font = fixture_font()

    def test_non_cmap_glyph_ids_zero_advance_and_bitmap_decode(self):
        # GID 3 is deliberately absent from cmap and has zero horizontal advance.
        with TTFont(io.BytesIO(self.font)) as font:
            cmap_names = set(font.getBestCmap().values())
            self.assertNotIn(font.getGlyphName(3), cmap_names)
        blob, report = spike.build_catalog(self.font, [2, 3, 4], size=20)
        self.assertEqual(report["glyphIds"], [2, 3, 4])
        self.assertEqual(report["glyphs"][1]["advance"], 0)
        self.assertGreater(report["glyphs"][1]["width"], 0)
        self.assertGreater(report["glyphs"][1]["height"], 0)
        info = unpack(blob)
        self.assertEqual(info[:5], (spike.MAGIC, 1, 96, 32, 4))
        self.assertEqual((info[5], info[6], info[7]), (len(blob), 3, 96))
        self.assertEqual(info[8], 96 + 3 * 32)
        gids = []
        for index in range(info[6]):
            record = spike.RECORD.unpack_from(blob, info[7] + index * info[3])
            gids.append(record[0])
        self.assertEqual(gids, [2, 3, 4])
        mark = spike.RECORD.unpack_from(blob, info[7] + info[3])
        self.assertEqual(mark[2], 0)
        self.assertGreater(decode_nibble(blob, mark, 0, 0), 0)

    def test_sorted_unique_ids_and_deterministic_hash(self):
        first, report = spike.build_catalog(self.font, [1, 3, 4], size=18)
        second, second_report = spike.build_catalog(self.font, [1, 3, 4], size=18)
        self.assertEqual(first, second)
        self.assertEqual(report["catalogSha256"], hashlib.sha256(first).hexdigest())
        self.assertEqual(report["catalogSha256"], second_report["catalogSha256"])
        self.assertEqual(zlib.crc32(first[:88] + b"\0\0\0\0" + first[92:]),
                         struct.unpack_from("<I", first, 88)[0])
        for invalid in ([1, 1, 2], [2, 1], [], [-1], [5]):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                spike.build_catalog(self.font, invalid)

    def test_byte_budget_and_input_bounds_refuse(self):
        with self.assertRaisesRegex(ValueError, "byte budget"):
            spike.build_catalog(self.font, [2, 3, 4], size=20, max_bytes=96 + 3 * 32)
        for source, ids, size, budget in [
                (b"", [1], 16, 1024), (self.font, [1], 0, 1024),
                (self.font, [65536], 16, 1024), (self.font, [1], 16, 95)]:
            with self.subTest(ids=ids, size=size), self.assertRaises(ValueError):
                spike.build_catalog(source, ids, size=size, max_bytes=budget)


if __name__ == "__main__":
    unittest.main(verbosity=2)
