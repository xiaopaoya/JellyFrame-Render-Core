# Noto Sans SC encoded shaping profile spike

Status: offline deterministic encoding experiment. It adds no public API,
runtime reader, JFFONT/JFTP change, or runtime dependency. It consumes the
selected closure and face hashes pinned by
[`noto_shaping_closure_prototype.py`](../../tools/noto_shaping_closure_prototype.py).
This is a size/format spike, not a claim of complete shaping support.

## Reproduction

Use Python 3.11, fontTools 4.60.1, and uharfbuzz 0.57.0 in the host environment
as required by the closure module:

```powershell
$env:PYTHONPATH = "$env:TEMP/jf-shaping-spike-deps"
python tools/noto_shaping_encoded_spike.py `
  --font-dir <Runtime>/ports/esp32s3-idf/font/source/noto-sans-sc-2.002 `
  --output-dir build/noto-shaping-encoded-spike
```

The tool checks the three pinned face SHA-256 values and refuses to overwrite
the output directory. It writes one `.jfshape` binary and a JSON report per
run. The binary hash is deterministic for identical font bytes and tool
versions. The focused format tests run with:

```powershell
python -m unittest tests/tools/test_noto_shaping_encoded_spike.py -v
```

## Representation decision

`JFSHAP1` is a little-endian, versioned container with fixed-size selection,
lookup-reference, and lookup directories followed by independently framed
lookup subtables. Script/language selections point to ordered feature/lookup
references; lookup records are deduplicated by OpenType table and lookup ID.
This keeps selection explicit while avoiding duplicate lookup payloads. The
profile currently emits 5 selections, 28 references, and 7 unique lookups per
face.

Glyph IDs, class counts, and classes are uint16; advances, placements, and
anchor coordinates are signed int16; offsets and lengths are uint32. Single substitutions and
pair records are ordered by glyph ID. Class definitions are sparse, with
unlisted glyphs retaining OpenType class zero; PairPos format 2's class-pair
matrix remains dense. Nonzero ValueRecord components use a four-bit presence
mask. Ligature records are ordered by first glyph, while each source
LigatureSet's original order is retained because it can determine precedence
for shared input prefixes. Mark/Base coverage order is retained to preserve
the parallel MarkArray/BaseArray relationship.

These are consequential schema choices for this spike, not a proposed frozen
resource contract. Fixed-width integers make bounds and decoding simple for a
future reader. Sparse PairPos values reduce this fixture's byte count; a dense
class matrix is retained because indexing an individual class pair stays
constant-time and the source table already defines that matrix. No decoder,
validation budget, access API, or execution model is specified here.

## Measured bytes

Pinned Noto Sans SC 2.002 faces, generated on 2026-11-13:

| Face | Encoded selected lookup data |
|---|---:|
| Regular | 41,508 B |
| Medium | 41,492 B |
| Bold | 41,510 B |
| Total | 124,510 B |

For each pinned face, `encodedUpperBoundBytes` in the report equals the exact
emitted byte count, including container and directory overhead. It is an
empirical bound for these three immutable inputs, not a format-wide maximum
for future fonts or profiles. The Regular profile binary is about 80% of the
51,962 B combined GSUB+GPOS table bytes reported for the Regular OTF. The
separate closure spike measured 7,824 / 7,941 / 8,173 B for 16 px raster GID
catalogs for its 71-GID inventory. Keep the table and catalog numbers separate:
they are independent experimental payloads, and their sum would omit production
resource framing, validation, alignment, indexing, and packaging. Neither
result measures flash-image costs.

## Limits

Only the profile's selected lookup closure is encoded: GSUB SingleSubst and
LigatureSubst; GPOS PairPos formats 1/2 and MarkBasePos format 1; zero lookup
flags; no device adjustments. The encoder additionally accepts only
coordinate-only Anchor format 1 for mark/base records and rejects other anchor
formats, even if the table subtable itself is MarkBasePos format 1. It rejects
values outside the chosen signed 16-bit range. It does not encode contextual
or chained lookups, extensions, alternate substitution, mark-to-mark,
mark-to-ligature, vertical layout, bidi/reordering, fallback, or layout
behavior beyond the pinned selected features.

The output is encoded lookup data only. It is not a shaper, does not execute
the encoded lookups, and does not establish that the selected lookup set is a
complete closure for arbitrary text. The separate closure prototype currently
matches 10 narrow cases per face (30 face/case comparisons), including same- and
cross-lookup multiple-mark attachment. This encoding exercise does not expand
that evidence or show that every admitted lookup is executed correctly from
these bytes. The bounded corpus does not qualify arbitrary input, other scripts,
fallback, or a production glyph-reachability policy. There is no embedded
decoder, malformed-resource testing, work/scratch bound,
firmware flash delta, latency result, or device rendering result. The current
JFFONT2/JFTP and runtime scalar behavior remain unchanged pending those gates
and a reviewed versioned resource contract.
