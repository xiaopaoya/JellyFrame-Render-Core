"""Focused contracts for the bounded offline shaping-closure prototype."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "noto_shaping_closure_prototype", ROOT / "tools" / "noto_shaping_closure_prototype.py")
spike = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = spike
SPEC.loader.exec_module(spike)


class ShapingClosurePrototypeTests(unittest.TestCase):
    def test_value_fields_use_open_type_device_field_names(self):
        cases = (
            ("XPlacement", "XPlaDevice", -3),
            ("YPlacement", "YPlaDevice", 4),
            ("XAdvance", "XAdvDevice", 120),
            ("YAdvance", "YAdvDevice", -80),
        )
        for value_field, device_field, expected in cases:
            with self.subTest(value_field=value_field):
                value = SimpleNamespace(**{value_field: expected, device_field: None})
                self.assertEqual(spike._value_component(value, value_field), expected)

    def test_device_adjustments_are_rejected_for_each_value_field(self):
        cases = (
            ("XPlacement", "XPlaDevice"),
            ("YPlacement", "YPlaDevice"),
            ("XAdvance", "XAdvDevice"),
            ("YAdvance", "YAdvDevice"),
        )
        for value_field, device_field in cases:
            with self.subTest(value_field=value_field):
                value = SimpleNamespace(**{value_field: 0, device_field: object()})
                with self.assertRaisesRegex(ValueError, device_field):
                    spike._value_component(value, value_field)

    def test_none_value_record_component_is_zero(self):
        self.assertEqual(spike._value_component(None, "XAdvance"), 0)

    def test_unknown_value_field_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "unsupported GPOS value field"):
            spike._value_component(SimpleNamespace(), "Weight")

    def test_canonical_composition_uses_available_font_glyph(self):
        cmap = {ord("A"): "A", ord("\u0301"): "acute", ord("Á"): "Aacute",
                ord("B"): "B"}
        self.assertEqual(spike._canonical_scalars("A\u0301B", cmap), [("Á", 0), ("B", 2)])

    def test_canonical_composition_preserves_marks_without_composite_glyph(self):
        cmap = {ord("A"): "A", ord("\u0307"): "dot", ord("\u0301"): "acute"}
        self.assertEqual(spike._canonical_scalars("A\u0307\u0301", cmap),
                         [("A", 0), ("\u0307", 0), ("\u0301", 0)])

    def test_mark_base_position_skips_preceding_marks(self):
        names = {1: "base", 2: "grave", 3: "acute"}
        font = SimpleNamespace(
            getGlyphName=lambda gid: names[gid],
            getGlyphID=lambda name: next(gid for gid, glyph_name in names.items()
                                         if glyph_name == name),
        )
        mark_anchor = SimpleNamespace(XCoordinate=0, YCoordinate=100,
                                      XDeviceTable=None, YDeviceTable=None)
        base_anchor = SimpleNamespace(XCoordinate=640, YCoordinate=700,
                                      XDeviceTable=None, YDeviceTable=None)
        subtable = SimpleNamespace(
            Format=1,
            MarkCoverage=SimpleNamespace(glyphs=["grave", "acute"]),
            BaseCoverage=SimpleNamespace(glyphs=["base"]),
            MarkArray=SimpleNamespace(MarkRecord=[
                SimpleNamespace(MarkAnchor=mark_anchor, Class=0),
                SimpleNamespace(MarkAnchor=mark_anchor, Class=0),
            ]),
            BaseArray=SimpleNamespace(BaseRecord=[
                SimpleNamespace(BaseAnchor=[base_anchor]),
            ]),
        )
        lookup = SimpleNamespace(LookupType=4, SubTable=[subtable])

        single = [spike.Glyph(1, 0, 1000), spike.Glyph(2, 0, 0)]
        spike._apply_mark_position(font, single, lookup)
        self.assertEqual((single[1].x_offset, single[1].y_offset), (-360, 600))

        double = [spike.Glyph(1, 0, 1000), spike.Glyph(2, 0, 0),
                  spike.Glyph(3, 0, 0)]
        spike._apply_mark_position(font, double, lookup)
        self.assertEqual((double[2].x_offset, double[2].y_offset), (-360, 600))

        grave_subtable = SimpleNamespace(
            Format=1,
            MarkCoverage=SimpleNamespace(glyphs=["grave"]),
            BaseCoverage=subtable.BaseCoverage,
            MarkArray=SimpleNamespace(MarkRecord=[subtable.MarkArray.MarkRecord[0]]),
            BaseArray=subtable.BaseArray,
        )
        grave_lookup = SimpleNamespace(LookupType=4, SubTable=[grave_subtable])
        acute_subtable = SimpleNamespace(
            Format=1,
            MarkCoverage=SimpleNamespace(glyphs=["acute"]),
            BaseCoverage=subtable.BaseCoverage,
            MarkArray=SimpleNamespace(MarkRecord=[subtable.MarkArray.MarkRecord[1]]),
            BaseArray=subtable.BaseArray,
        )
        acute_lookup = SimpleNamespace(LookupType=4, SubTable=[acute_subtable])
        cross_lookup = [spike.Glyph(1, 0, 1000), spike.Glyph(2, 0, 0),
                        spike.Glyph(3, 0, 0)]
        selected_mark_glyphs = {2, 3}
        spike._apply_mark_position(font, cross_lookup, grave_lookup, selected_mark_glyphs)
        spike._apply_mark_position(font, cross_lookup, acute_lookup, selected_mark_glyphs)
        self.assertEqual((cross_lookup[2].x_offset, cross_lookup[2].y_offset), (-360, 600))


if __name__ == "__main__":
    unittest.main(verbosity=2)
