"""Focused binary-format contracts for the deterministic shaping profile spike."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
from types import SimpleNamespace as NS
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "noto_shaping_encoded_spike", ROOT / "tools" / "noto_shaping_encoded_spike.py")
spike = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = spike
SPEC.loader.exec_module(spike)


class FakeFont:
    def __init__(self):
        self.names = [".notdef", "a", "b", "c", "d", "mark"]
        self.ids = {name: index for index, name in enumerate(self.names)}

    def getGlyphID(self, name):
        return self.ids[name]

    def getGlyphName(self, gid):
        return self.names[gid]

    def __getitem__(self, key):
        return self.tables[key]


def value(**overrides):
    data = {field: 0 for field in spike.VALUE_FIELDS}
    data.update({"XPlaDevice": None, "YPlaDevice": None,
                 "XAdvDevice": None, "YAdvDevice": None})
    data.update(overrides)
    return NS(**data)


def profile_font():
    font = FakeFont()
    gsub = [
        NS(LookupType=1, LookupFlag=0,
           SubTable=[NS(mapping={"a": "b", "c": "d"})]),
        NS(LookupType=4, LookupFlag=0,
           SubTable=[NS(ligatures={"a": [NS(Component=["b", "c"], LigGlyph="d"),
                                      NS(Component=["b"], LigGlyph="c")]})]),
    ]
    pair1 = NS(Format=1, Coverage=NS(glyphs=["a"]), PairSet=[NS(PairValueRecord=[
        NS(SecondGlyph="b", Value1=value(XAdvance=-20), Value2=None)])])
    pair2 = NS(Format=2, Coverage=NS(glyphs=["a", "b"]),
               ClassDef1=NS(classDefs={"a": 1}), ClassDef2=NS(classDefs={"b": 1}),
               Class1Count=2, Class2Count=2,
               Class1Record=[NS(Class2Record=[NS(Value1=None, Value2=None),
                                               NS(Value1=value(XPlacement=3), Value2=None)]),
                             NS(Class2Record=[NS(Value1=None, Value2=None),
                                               NS(Value1=value(YAdvance=-4), Value2=None)])])
    mark = NS(Format=1, MarkCoverage=NS(glyphs=["mark"]),
              BaseCoverage=NS(glyphs=["a"]), ClassCount=1,
              MarkArray=NS(MarkRecord=[NS(Class=0, MarkAnchor=NS(Format=1,
                  XCoordinate=1, YCoordinate=2, XDeviceTable=None, YDeviceTable=None))]),
              BaseArray=NS(BaseRecord=[NS(BaseAnchor=[NS(Format=1, XCoordinate=10,
                  YCoordinate=20, XDeviceTable=None, YDeviceTable=None)])]))
    gpos = [NS(LookupType=2, LookupFlag=0, SubTable=[pair1, pair2]),
            NS(LookupType=4, LookupFlag=0, SubTable=[mark])]
    def layout(lookups, features):
        lang_sys = NS(ReqFeatureIndex=0xFFFF, FeatureIndex=list(range(len(features))))
        scripts = [NS(ScriptTag=tag, Script=NS(LangSysRecord=[], DefaultLangSys=lang_sys))
                   for tag in ("latn", "hani", "bopo")]
        records = [NS(FeatureTag=tag, Feature=NS(LookupListIndex=[lookup_id]))
                   for tag, lookup_id in features]
        return NS(table=NS(LookupList=NS(Lookup=lookups),
                           ScriptList=NS(ScriptRecord=scripts),
                           FeatureList=NS(FeatureRecord=records)))

    font.tables = {"GSUB": layout(gsub, [("ccmp", 0), ("liga", 1)]),
                   "GPOS": layout(gpos, [("kern", 0), ("mark", 1)])}
    return font


class EncodedShapingSpikeTests(unittest.TestCase):
    def test_all_admitted_subtable_kinds_have_stable_encodings(self):
        font = FakeFont()
        cases = [
            ("GSUB", 1, NS(mapping={"c": "d", "a": "b"})),
            ("GSUB", 4, NS(ligatures={"a": [NS(Component=["b", "c"], LigGlyph="d")]})),
            ("GPOS", 2, NS(Format=1, Coverage=NS(glyphs=["a"]), PairSet=[NS(PairValueRecord=[
                NS(SecondGlyph="b", Value1=value(XAdvance=-20), Value2=None)])])),
            ("GPOS", 2, NS(Format=2, Coverage=NS(glyphs=["a", "b"]),
                ClassDef1=NS(classDefs={"a": 1}), ClassDef2=NS(classDefs={"b": 1}),
                Class1Count=2, Class2Count=2,
                Class1Record=[NS(Class2Record=[NS(Value1=None, Value2=None),
                                               NS(Value1=value(XPlacement=3), Value2=None)]),
                             NS(Class2Record=[NS(Value1=None, Value2=None),
                                               NS(Value1=value(YAdvance=-4), Value2=None)])])),
            ("GPOS", 4, NS(Format=1, MarkCoverage=NS(glyphs=["mark"]),
                BaseCoverage=NS(glyphs=["a"]), ClassCount=1,
                MarkArray=NS(MarkRecord=[NS(Class=0, MarkAnchor=NS(Format=1, XCoordinate=1,
                    YCoordinate=2, XDeviceTable=None, YDeviceTable=None))]),
                BaseArray=NS(BaseRecord=[NS(BaseAnchor=[NS(Format=1, XCoordinate=10,
                    YCoordinate=20, XDeviceTable=None, YDeviceTable=None)])]))),
        ]
        for table_tag, lookup_type, subtable in cases:
            with self.subTest(table=table_tag, lookup=lookup_type,
                              format=getattr(subtable, "Format", 0)):
                first = spike._encode_subtable(font, table_tag, lookup_type, subtable)
                second = spike._encode_subtable(font, table_tag, lookup_type, subtable)
                self.assertEqual(first, second)
                self.assertGreater(len(first[1]), 4)

    def test_container_offsets_counts_hash_and_repeatability(self):
        font = profile_font()
        first, report = spike.build_encoded(font)
        second, second_report = spike.build_encoded(font)
        header = spike.HEADER.unpack_from(first)
        self.assertEqual(first, second)
        self.assertEqual(report["sha256"], second_report["sha256"])
        self.assertEqual(header[0:3], (spike.MAGIC, 1, spike.HEADER.size))
        self.assertEqual(header[3], len(first))
        self.assertEqual(header[4:6], (5, 4))
        self.assertEqual(report["encodedBytes"], report["encodedUpperBoundBytes"])
        self.assertEqual(report["payloadBytes"] + report["headerBytes"] +
                         report["selectionDirectoryBytes"] + report["lookupReferenceBytes"] +
                         report["lookupDirectoryBytes"], len(first))
        for index in range(header[4]):
            script, language, first_ref, ref_count, reserved = spike.SELECTION.unpack_from(
                first, header[6] + index * spike.SELECTION.size)
            self.assertTrue(script.strip())
            self.assertEqual(ref_count, 4)
            self.assertEqual(reserved, 0)

    def test_values_anchors_classes_and_types_are_bounded(self):
        with self.assertRaisesRegex(ValueError, "outside int16"):
            spike._value(value(XAdvance=40000))
        with self.assertRaisesRegex(ValueError, "Anchor format 1"):
            spike._anchor(NS(Format=2, XCoordinate=0, YCoordinate=0,
                             XDeviceTable=None, YDeviceTable=None))
        with self.assertRaisesRegex(ValueError, "class assignment"):
            spike._encode_subtable(FakeFont(), "GPOS", 2,
                NS(Format=2, Coverage=NS(glyphs=[]), ClassDef1=NS(classDefs={"a": 2}),
                   ClassDef2=NS(classDefs={}), Class1Count=2, Class2Count=1,
                   Class1Record=[],))
        with self.assertRaisesRegex(ValueError, "unsupported selected"):
            spike._encode_subtable(FakeFont(), "GSUB", 3, NS())


if __name__ == "__main__":
    unittest.main(verbosity=2)
