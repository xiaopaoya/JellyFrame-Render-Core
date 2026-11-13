# Noto Sans SC shaping closure prototype

Status: offline, bounded prototype and generated GID bitmap catalogs. This
confirms a selected lookup closure and sample parity; it does not add runtime
shaping or change Core APIs, JFFONT2, JFTP, or defaults.

## Inputs and reproduction

Inputs are the pinned Noto Sans SC 2.002 Regular, Medium, and Bold OTF faces
used by Runtime, plus its pinned 16 px Regular JFFONT2 resource. The script
checks their SHA-256 hashes and requires Python 3.11, fontTools 4.60.1, and
uharfbuzz 0.57.0. Given those dependencies on `PYTHONPATH`, run from the Core
checkout:

```powershell
python tools/noto_shaping_closure_prototype.py `
  --font-dir <Runtime>/ports/esp32s3-idf/font/source/noto-sans-sc-2.002 `
  --jffont2 <Runtime>/ports/esp32s3-idf/font/core-system-noto-regular-16.jffont2 `
  --output-dir build/shaping-closure-prototype-16px `
  --raster-size 16
```

The script emits a JSON report and one experimental `JFGID1` catalog per face.
It refuses to overwrite an existing output directory. The complete GID list,
source identities, individual catalog hashes, and each oracle comparison are
in that report.

## Results

All three faces have the same 71 GIDs emitted by the selected GSUB closure or
required as selected MarkBasePos mark glyphs but absent from the pinned
JFFONT2 mapping. The inventory is:

```text
249 250 251 252 254 255 1090 1091 1178 1179
29618 29619 29620 29621 29622
30301 30302 30303 30304 30305 30306 30307 30308 30309 30310
30311 30312 30313 30314 30315 30316 30317 30318 30319 30320
30321 30322 30323 30324 30325 30326 30327 30328 30329 30330
30331 30332
30416 30417 30418 30420 30423 30424 30425 30426 30427
30432 30433 30442 30443 30444 30445 30446 30447 30448 30449
30450 30451 30452 30551 30552
```

The ten corpus cases were compared record-for-record with HarfBuzz for each
face: glyph ID, source cluster, both advances, and both offsets. All 30
comparisons matched. The corpus covers canonical composition, `ffi` ligature,
English/Simplified-Chinese `locl` control, Chinese punctuation, Latin kerning,
Bopomofo single-mark attachment, two double-mark attachment sequences, and the
`A` plus two-mark sequence. For both `ㄓ` + U+0300 + U+0301 and `ㄓ` + U+0307 +
U+0300, both marks attach to the same base and the second mark has X/Y offsets
-360/+600 in all three faces. The second sequence crosses two selected MarkBasePos
lookups, so it also checks mark recognition across the selected feature closure.
The separate `A` mark-chain case still has no nonzero attachment and does not
establish mark-to-mark support. The implementation applies font-aware canonical
composition and the selected SingleSubst, LigatureSubst, PairPos formats 1/2,
and MarkBasePos format 1 operations needed by this profile.

At 16 px with 4-bit coverage, the experimental catalogs measured:

| Face | Catalog bytes | Bitmap bytes |
|---|---:|---:|
| Regular | 7,824 | 5,456 |
| Medium | 7,941 | 5,573 |
| Bold | 8,173 | 5,805 |
| Total | 23,938 | 16,834 |

These are raster-catalog sizes for only the 71 additional GIDs at one size.
They are not the complete shaping resource size. They exclude any encoded
GSUB/GPOS data, production indexing and validation metadata, alignment, other
font sizes, or resource packaging overhead. They also do not measure firmware
flash delta, parser code, peak RAM, scratch, execution work, latency, or device
rendering.

## Limits and next gate

The prototype is horizontal LTR and only admits zero-flag lookups without
device adjustments: GSUB SingleSubst and LigatureSubst; GPOS PairPos formats 1
and 2; and MarkBasePos format 1. It has no contextual/chained substitutions,
extension lookups, mark-to-mark or mark-to-ligature, bidi/reordering, fallback,
wrapping, complete Unicode qualification, or production Core integration.
Ten examples establish a narrow oracle check, not a general shaping claim.

The deterministic lookup-data encoding and its host-side byte measurement are
recorded in the separate encoded-profile evaluation. The next gate is an
isolated N16R8 resource-only build that measures linked flash delta. It does
not qualify a runtime reader or imply support. Only if the size fits an
explicit budget should work proceed to resource validation, bounded reader/work
and scratch analysis, then device rendering acceptance. Runtime must continue
to use scalar JFFONT2 behavior until those gates and a reviewed API/resource
contract are complete.
