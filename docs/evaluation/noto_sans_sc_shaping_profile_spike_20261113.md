# Noto Sans SC shaping profile spike

Status: offline experiment only. It does not change `TextSystem`, JFTP, Core
defaults, Runtime, or JFFONT2. It is a concrete input-profile probe for the
resource-model decision in
[`core_owned_shaping_contract_proposal_20261113.md`](core_owned_shaping_contract_proposal_20261113.md).

## Inputs and reproduction

The input is the product Noto Sans SC 2.002 Regular OTF from Runtime
`ports/esp32s3-idf/font/source/noto-sans-sc-2.002/NotoSansSC-Regular.otf`,
SHA-256 `A2B93E6C2DB05D6BBBF6F27D413EC73269735B7B679019C8A5AA9670FF0FFBF2`
(8,482,020 bytes). The paired product resource
`ports/esp32s3-idf/font/core-system-noto-regular-16.jffont2` is 716,274 bytes,
SHA-256 `7454E5A35C99E1BFBEB0CF226C0E9C128C1D9520B40EC9037A293A2A6C43A320`.
That JFFONT2 is a scalar-to-bitmap resource; it contains no OpenType shaping
tables. This experiment reads the OTF directly and does not infer GSUB/GPOS
from JFFONT2.

Verified with Python 3.11, fontTools 4.60.1, and uharfbuzz 0.57.0. Install
both pinned dependencies in a temporary target directory, then run:

```powershell
python -m pip install --target $env:TEMP/jf-shaping-spike-deps fontTools==4.60.1 uharfbuzz==0.57.0
$env:PYTHONPATH = "$env:TEMP/jf-shaping-spike-deps"
python tools/noto_shaping_profile_spike.py `
  E:/Workspaces/migration-followup/Runtime-core-release-status-20261113/ports/esp32s3-idf/font/source/noto-sans-sc-2.002/NotoSansSC-Regular.otf
```

The prototype parses and applies only GSUB `fwid` and GPOS `kern`, using
fontTools' decoded OpenType records. Its oracle is HarfBuzz with all unrelated
default features disabled. The cases isolate horizontal Latin kerning,
numeric/punctuation kerning, explicit fullwidth substitution, and Han plus
Chinese punctuation. Font scale is 1000 units per em. Run exit code 0 means all
glyph IDs, X/Y advances, and X/Y offsets matched.

## Observed profile and result

The OTF has 30,900 glyphs, 1000 units per em, a 10,592-byte GSUB table, and a
41,370-byte GPOS table. GSUB exposes `aalt`, `ccmp`, `dlig`, `fwid`, `hist`,
`hwid`, `liga`, `locl`, `pwid`, `ruby`, `vert`, and `vrt2`; its lookup types are
11 SingleSubst, 1 AlternateSubst, and 3 LigatureSubst lookups. GPOS exposes
`halt`, `kern`, `mark`, `palt`, `vert`, `vhal`, `vkrn`, and `vpal`; its lookup
types are 5 SinglePos, 2 PairPos, and 3 MarkBasePos lookups.

Selected `fwid` is GSUB lookup 6, type 1, one SingleSubst subtable with 218
mappings. Selected `kern` is GPOS lookup 5, type 2, with a PairPos format 1
subtable (142 covered first glyphs, 845 explicit pairs) and a format 2 class
subtable (359 covered first glyphs, 358 explicit ClassDef1 assignments, 352
explicit ClassDef2 assignments, 122 by 105 class matrix). Coverage count and
ClassDef2 assignment count describe different fields. Both have lookup flags
0. The prototype supports
these exact representations and refuses unexpected selected lookup types or
PairPos formats.

| Corpus | Scalars | Match |
|---|---:|---|
| `AVATAR To Wa` with `kern` | 12 | exact |
| `A1, 9.50! ABC` with `kern` | 13 | exact |
| `ABC 123!?` with `fwid` | 9 | exact |
| `中文，。！？` with `kern` | 6 | exact |

Result: 4/4 cases matched HarfBuzz, including the CFF font's nominal advances,
SingleSubst advance replacement, and pair positioning. The prototype and
HarfBuzz compared 40 glyph records; mismatches: 0.
The pinned fontTools 4.60.1 / uharfbuzz 0.57.0 rerun on 2026-11-13 produced
the same 4/4 exact matches and the same 359-coverage / 352-ClassDef2 counts.

## Boundary and implications

This supports the proposal's concern that a generated shape profile would
need new resource data: this product JFFONT2 preserves scalar glyph IDs,
advances, vertical metrics, and bitmap coverage, but cannot answer even the
tested `fwid` or `kern` lookups. The OTF's complete GSUB+GPOS payload is 51,962
bytes per face before alignment or any generated-index overhead. This is only
the uncompressed table size; it is not a proposed JFFONT extension size.

The script is a host experiment, not a Core parser estimate. It loads the full
8.5 MB OTF through fontTools and constructs Python table objects; its peak
memory and work are not representative of a bounded embedded parser. It emits
no binary resource, copies no font, adds no Runtime dependency, and has no MCU
scratch measurement. A production profile would need versioned table encoding,
layout/script/language selection rules, table validation, bounds on parsed
work and scratch, generator reproducibility, and face fallback semantics.

Unsupported here: `ccmp`, `locl`, ligatures, alternates, contextual/chained
lookups, mark attachment, vertical writing, bidi/reordering, combining
clusters, multi-glyph clusters, cluster/source maps, fallback, wrapping, and
text layout. The test corpus is deliberately small and does not establish a
general shaping profile. JFFONT2 behavior and all public APIs remain unchanged.

## Recommended next gate

Keep this as evidence for staged proposal gate 1. Before choosing original OTF
consumption or a generated shaping resource, probe a deliberately bounded
profile across all three product weights and a larger corpus. Include `ccmp`,
`locl` under explicit language systems, ligature/combining examples, and mark
positioning, then record the selected lookup closure and invalid/unsupported
table cases. In parallel, measure an encoded generated-profile size and a
bounded parser's peak scratch/work. Do not freeze `shape()` or packet APIs from
this single-feature experiment.
