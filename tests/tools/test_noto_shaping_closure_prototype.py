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


if __name__ == "__main__":
    unittest.main(verbosity=2)
