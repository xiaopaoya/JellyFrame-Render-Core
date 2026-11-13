# Noto Sans SC feature profile follow-up

Status: offline font-data inspection and HarfBuzz oracle probe. It changes no
Core API, packet, default behavior, or font resource. This follows the
resource-model investigation in PR42 and the narrow `fwid`/`kern` spike in
PR46. The worktree was based on Core master commit `c69bb7fb` (PR45); PR46
contains only the earlier script/report and no Core implementation or API
change.

## Inputs and reproduction

Inputs are the three product Noto Sans SC 2.002 OTFs in Runtime's
`ports/esp32s3-idf/font/source/noto-sans-sc-2.002`:

| Face | Bytes | SHA-256 | Glyphs | GSUB bytes | GPOS bytes |
|---|---:|---|---:|---:|---:|
| Regular | 8,482,020 | `A2B93E6C2DB05D6BBBF6F27D413EC73269735B7B679019C8A5AA9670FF0FFBF2` | 30,900 | 10,592 | 41,370 |
| Medium | 8,508,580 | `9C62CEB174D7529AE4A7F2071F6531991CFADBC2F1897910B48BA951A580AC57` | 30,900 | 10,592 | 41,370 |
| Bold | 8,716,392 | `D1961BE1161EA1BE08496C920862D06EA5C23A757628F4FD69368DE1D9F51BED` | 30,900 | 10,592 | 41,566 |

The product OTFs are CFF OpenType fonts with 1000 units per em. The runtime
JFFONT2 is a scalar bitmap-glyph resource and has no GSUB/GPOS data; this probe
reads the OTF sources directly.

Verified on Python 3.11 with fontTools 4.60.1 and uharfbuzz 0.57.0. The script
enforces both Python package versions and exits with status 2 on a mismatch.
Set the font directory to the checked-out Runtime source location:

```powershell
python -m pip install --target $env:TEMP/jf-shaping-spike-deps fontTools==4.60.1 uharfbuzz==0.57.0
$env:PYTHONPATH = "$env:TEMP/jf-shaping-spike-deps"
$runtimeFontDir = 'E:/Workspaces/migration-followup/Runtime/ports/esp32s3-idf/font/source/noto-sans-sc-2.002'
python tools/noto_sans_sc_feature_probe.py $runtimeFontDir
```

Replace the example Runtime checkout path as needed; the script takes the
directory as its only positional argument and reads the three named OTF files
from it.

The script emits JSON containing Script/LangSys feature closure, selected
lookup IDs and decoded subtable counts, per-face source hashes, and HarfBuzz
glyph IDs, clusters, advances and offsets. The probe only inspects the
requested `ccmp`, `locl`, `liga`, `kern`, `mark`, and `mkmk` closures. Its
corpus includes decomposed acute, `office`, a Simplified Chinese locale
control for U+2014, Chinese punctuation, Latin kerning, a Bopomofo base with
combining grave, and a two-mark chain.

## Feature selection and lookup closure

The GSUB table has six Script records in each face: `DFLT`, `cyrl`, `grek`,
`hani`, `kana`, and `latn`. The non-default `ZHS ` LangSys is present for all
five non-DFLT scripts. GPOS has the same six scripts, each with only the
default LangSys. GSUB default LangSys lists `locl` for `cyrl`, `grek`, and
`latn`, while `hani` and `kana` gain it only under `ZHS `. For every script
with `locl`, the selected lookup differs by language system. All three faces
have the same 17 relevant Script/LangSys closures and lookup shapes below.

| Table / Script / LangSys | Feature | Lookup closure |
|---|---|---|
| GSUB `latn` default | `ccmp` | lookup 2, type 4 LigatureSubst: 32 first-glyph keys, 38 ligatures |
| GSUB `latn` default | `locl` | lookup 10, type 1 SingleSubst: 11 mappings |
| GSUB `latn` default | `liga` | lookup 4, type 4 LigatureSubst: 1 first-glyph key, 5 ligatures |
| GSUB `latn` `ZHS ` | `ccmp`, `liga` | same lookup 2 and lookup 4 |
| GSUB `latn` `ZHS ` | `locl` | lookup 11, type 1 SingleSubst: 14 mappings |
| GSUB `hani` default | `ccmp`, `liga` | lookup 2 and lookup 4; `locl` absent |
| GSUB `hani` `ZHS ` | `ccmp`, `liga`, `locl` | lookups 2, 4, and 11 |
| GSUB `cyrl`, `grek` default | `ccmp`, `locl`, `liga` | lookups 2, 10, and 4 |
| GSUB `cyrl`, `grek`, `kana` `ZHS ` | `ccmp`, `locl`, `liga` | lookups 2, 11, and 4 |
| GSUB `kana` default | `ccmp`, `liga` | lookup 2 and lookup 4; `locl` absent |
| GPOS `latn`, `hani`, or `DFLT` default | `kern` | lookup 5, type 2 PairPos: format 1 has 142 covered first glyphs/845 pairs; format 2 has 359 covered first glyphs, 358 ClassDef1 entries, 352 ClassDef2 entries, and a 122x105 class matrix |
| GPOS `latn`, `hani`, or `DFLT` default | `mark` | lookups 0 and 1, type 4 MarkBasePos format 1; respectively 1 mark/48 bases and 5 marks/43 bases |
| All 17 relevant Script/LangSys closures | `mkmk` | absent; no selected feature or MarkMarkPos lookup |

All selected lookups have flags 0. Bold's GPOS table is 196 bytes larger
because of face-specific values; feature closure, lookup types, and subtable
counts are otherwise the same across faces.

The locale pair is observable, not only metadata: with HarfBuzz `Latn` and
`zh-cn`, U+2014 selects the `ZHS ` path and emits GID 466; the English control
uses the default `latn` language system and emits GID 465. The output IDs and
ZHS advance 1000 units are identical across these three faces; English advance
is 894/900/908 for Regular/Medium/Bold.

## HarfBuzz cases and exact-comparison boundary

Default HarfBuzz output is stable in glyph/cluster shape across the three
faces; advances vary by weight. Representative Regular results:

| Case | Output |
|---|---|
| `A` + U+0301 | composed GID 126, cluster 0, advance 607 |
| `office` | GIDs 80, 29621, 68, 70; clusters 0, 1, 4, 5; the `ffi` span maps to one ligature glyph |
| U+2014 with `zh-cn` | GID 466; English control: GID 722 |
| `中文，。！？` | GIDs 8744, 14689, 29702, 1131, 30436, 30441; 1000-unit advances |
| `AVATAR To Wa` with only `kern` enabled | 12 glyphs; advances 592, 560, 546, 554, 607, 636, 224, 525, 606, 224, 860, 564 |
| U+3113 + U+0300 | base GID 1396; mark GID 251 at cluster 0 with zero advance, X offset -360, Y offset +600 |
| `A` + U+0307 + U+0301 | three glyphs at cluster 0; no nonzero mark-to-mark offsets |

Across Regular/Medium/Bold, the composed acute result is GID 126 at advances
607/622/641. The `ffi` ligature result remains GID 29621 at cluster 1 with
advances 918/964/1024. The Simplified-Chinese U+2014 result remains GID 466;
the Bopomofo mark result remains GID 251 at (-360,+600) with zero advance.
The HarfBuzz corpus contains eight cases per face (24 face/case executions).

The prior PR46 bounded prototype exactly represents the isolated `kern`
PairPos format 1/2 path (and the separate `fwid` SingleSubst path). Re-running
that prototype on Regular, Medium, and Bold gives 4/4 exact cases per face,
zero mismatches per face. This follow-up does not claim its simpler model
represents all default HarfBuzz cases. The new combining, ligature, locale,
Chinese, and mark examples are oracle observations; their selected closure
requires GSUB LigatureSubst or MarkBasePos handling, and source-cluster
mapping for many-to-one output, beyond that prototype.

The locale lookup is a SingleSubst and the table representation itself is
small, but this experiment does not implement a second shaping path for it.
The `mkmk` feature is absent in the source, so the mark-chain sample cannot
establish mark-to-mark attachment support. The `mark` feature is present and
the Bopomofo sample demonstrates nonzero attachment offsets for a real mapped
mark/base pair.

## Implications and limits

For all three faces, complete GSUB+GPOS tables total 51,962 bytes in Regular
and Medium, and 52,158 bytes in Bold. This raw table size excludes alignment,
any generated index or normalized representation, and resource validation
metadata. It is not a proposed JFFONT profile size. The OTF source itself is
8.48-8.72 MB per face; a parser operating on original OTF has correspondingly
larger input availability requirements. This host probe loads complete OTFs
through fontTools and does not measure embedded parser code, work, stack, or
scratch.

The lookup inventory shows that the bounded profile decision must include
LigatureSubst, SingleSubst under language selection, PairPos formats 1/2, and
MarkBasePos. Handling only the previous `fwid`/`kern` profile would omit
demonstrated composition, common ligatures, Chinese locale alternates, and
nonzero mark attachment. JFFONT2 has none of these lookup tables. No Core API,
JFFONT version, Runtime resource, default shaping behavior, or public
compatibility contract was changed here.

The probe does not cover lookup extension types, contextual/chained
substitutions, GPOS mark-to-mark, GPOS mark-to-ligature, vertical shaping,
bidi/reordering, complete cluster/source maps, fallback, wrapping, all
languages, or malformed-font refusal. It does not estimate a production
parser's memory or work. The corpus is representative evidence, not broad
Unicode qualification.
