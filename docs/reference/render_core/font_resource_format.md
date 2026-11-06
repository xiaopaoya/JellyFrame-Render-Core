# Canonical prepared font resource: JFFONT2

This resource/scaling foundation implements part of the
[Core-owned text migration](../../evaluation/2026-10-31-core-font-ownership/decision.md).
It does not yet replace the existing layout/paint callbacks or Runtime font stack.
The new loader accepts version2 only. Existing JFFONT0/1 loaders remain pending
the coordinated caller/ABI migration; no guessed-metrics adapter is supplied.

## Profile and preparation

Scalar bitmap profile1 contains static upright faces with explicit Unicode scalar
coverage, original advances and grayscale coverage. It does not imply kerning,
ligatures, contextual shaping, bidi or cluster-safe line breaking. Unsupported
variable/italic/oblique input is refused by the project preparation tool instead
of silently losing its semantics. Family registration/face selection/fallback
order and service integration are subsequent migration work.

[prepare_font_resource.py](../../../tools/prepare_font_resource.py) uses pinned
[dependencies](../../../tools/font_resource_requirements.txt). It reads the source
font once, extracts original `head.unitsPerEm`, `OS/2.usWeightClass`, `hhea` ascent,
nonnegative descent extent and signed line gap, and `hmtx` advances. Deliberately
different OS/2 typographic/Windows metrics do not override this profile's hhea
policy. Rasterization uses Pillow BASIC/FreeType, baseline-left anchor, cropped
ink with signed bearings, and nearest quantization to1/2/4/8 bits. Empty ink has
zero geometry but retains its original advance; zero advance is also meaningful.
No port implements these calculations. TAB/CR/LF in coverage files are ignored
as layout controls; missing mappings, .notdef mappings and invalid scalars refuse.

Example (run in the pinned Python environment):

```powershell
python tools/prepare_font_resource.py --font font.otf --characters coverage.txt `
  --size 24 --bits 4 --output font.jffont2 --report font.json
```

The report records source/resource SHA256, full metrics, glyph metadata and
Pillow/fontTools/FreeType versions. Identical **prepared asset bytes** and profile
are deterministic runtime inputs. Source-font SHA256 alone is not a cache/asset
identity: different subsets, raster sizes, coverage precision or tool builds can
produce different resources from the same source. Repeat generation is checked
within the pinned environment; arbitrary platform FreeType builds are not claimed
to generate byte-identical assets. The prepared asset is shared across ports.

## Binary layout

All integers are little endian; offsets and lengths are bytes. No implicit host
structure packing or alignment is required. Header is128B, each mapping is40B,
then coverage bytes follow contiguously. Trailing bytes are forbidden.

| Header offset | Type | Meaning |
| --- | --- | --- |
| 0 | byte[8] | `JFFONT2\0` |
| 8,10,12,14 | u16 each | version2, header128, mapping40, scalar profile1 |
| 16,20,24 | u32 each | exact total size, mapping count, mapping offset128 |
| 28,32 | u32 each | bitmap offset `128+40*count`, bitmap byte count |
| 36,38,40 | u16 each | units/em1..16384, raster pixel size1..4096, weight1..1000 |
| 42,43 | u8 each | coverage bits1/2/4/8; reserved zero |
| 44,48,52 | i32 each | original hhea ascent, nonnegative descent, signed line gap |
| 56,60 | u32 each | declared fallback scalar (0 means none); reserved zero |
| 64 | byte[32] | source-font SHA256, nonzero |
| 96 | u32 | IEEE CRC32 over the entire resource with96..99 zeroed |
| 100 | u32 | project Pillow/FreeType BASIC grayscale raster profile1 |
| 104 | u16 | metric source1: hhea |
| 106 | byte[22] | reserved zero |

| Mapping offset | Type | Meaning |
| --- | --- | --- |
| 0,4 | u32 each | Unicode scalar; original nonzero glyph ID1..65535 |
| 8 | i32 | original hmtx advance0..2^20 |
| 12,16 | i32 each | raster bearing X/Y relative to baseline pen, Y down |
| 20,22 | u16 each | width/height, both zero or both positive, at most4096 |
| 24 | u32 | exact stride `ceil(width*bits/8)` |
| 28,32 | u32 each | relative coverage offset; length `stride*height` |
| 36 | u32 | reserved zero |

Mappings are strictly increasing, unique valid scalars; surrogate code points
are forbidden. Every record starts at the cumulative previous coverage length;
no overlaps, gaps or unreferenced payload. Coverage packs leftmost pixel into
the high bits, rows top to bottom; unused low bits at each row end are zero.
Empty ink requires zero stride, length and bearings. Original metrics/bearings
have absolute bound2^20; A/D are nonnegative and A+D+gap is within0..2^20.
Nonzero declared fallback must be mapped, but lookup never substitutes it
automatically. Missing glyph lookup explicitly fails.

## Core interpretation and lifetime

[FontResource](../../../include/render_core/font_resource.h) is an allocation-free
validated borrowed view. Resource bytes must remain alive and unchanged while
any resource copy or glyph coverage view is used. Load validates into a draft;
all refusals preserve the previous valid resource. Query failure clears output.
CRC32 checks accidental integrity, not authenticity. Source SHA metadata is not
verified against an unavailable original font by the runtime loader.

Sizes/results use signed integer Q1000 CSS pixels. A/D, line gap, natural line
height and advance scale by original units/em. Raster bearings/width/height
scale by the asset's nominal raster size. Round each requested value to nearest,
ties away from zero; reject negative sizes and int32 overflow. Zero size is valid.
Natural height scales from original A+D+gap, not a sum of already rounded parts.
Ink geometry is distinct from advance/line metrics. Pixel coverage resampling
and the final text service are not implemented by these metric queries.

Default load budgets:8MiB resource,65535 mappings,4096 per bitmap dimension,
and16MiB+65535 work units. Work preflight charges one unit per byte hashed,
one per bitmap byte (conservative padding-check allowance), and one per mapping
validated. CRC uses a fixed eight bit steps per hashed-byte unit; this accounting
is a bounded workload proxy, not cycle/time measurement. Byte/count/work checks
precede CRC traversal; smaller caller budgets can refuse otherwise valid assets.
Profile hard limits remain even when caller limits are raised. Queries use
binary search and fixed-width integer math, with no allocator or host callback.
