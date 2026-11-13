# Common text selection and scalar preparation

[TextSystem](../../../include/render_core/text_system.h) implements project-owned
family/weight/strike selection, metrics, actual glyph references and Q1000 scalar
positions from [JFFONT2 resources](font_resource_format.md). It has no host policy
callbacks. This is a bounded first service implementation; production layout,
software paint, Runtime registration/transport and removal of old providers remain
the coordinated migration in the [architecture decision](../../evaluation/2026-10-31-core-font-ownership/decision.md).

## Inputs and lifetime

Bind an immutable array of `TextFontFace` records. Each record supplies an exact
prepared resource, its family identity and nonzero resource ID. At least one
default-family0 resource is required. Named families may declare the normalized
CSS family-token hash used by Core's style parser; every strike registered for
one hash must resolve to the same family ID. `resolve_family_hash` performs this
catalog lookup without a callback and fails for an unregistered name. A zero
hash resolves to family0. IDs must be unique within a set and must
never name different resource bytes while any referring glyph result, cache or
transport survives. IDs/registration are data and lifetime responsibilities;
selection/scaling are implemented by Core, not callbacks supplied with the set.

The array and all underlying resource bytes remain alive and unchanged while
bound or while another copied service view uses them. The service borrows them;
it does not make ownership claims on a Runtime font manager's behalf. Failed
bind preserves the previous set. Same-source resources must agree on original
units, weight and vertical metrics, so raster strikes cannot redefine those
properties. SHA metadata is not source authentication or proof of all font-table
contents. Asset bytes must come from the canonical project preparation path.

A `TextFontRequest` supplies one ordered list of already-resolved family IDs,
Q1000 size, requested weight and style. Both `measure` and `prepare` use this
same request type. There are no string/range or family/nonfamily provider variants.
The text is a borrowed UTF-8 view for the call; it need not be NUL terminated.
Each service call handles one supported scalar run. CSS whitespace processing,
line breaking, spacing, alignment and final line-box construction belong to the
layout caller and are not performed by this font-service slice.

## One deterministic selection policy

1. Deduplicate the requested family IDs without reordering. Append default0 when
   absent. No other registered family is an implicit fallback. Registration data
   therefore represents the same explicit input on host/target.
2. Within each family choose one weight independently of character coverage.
   Use [CSS Fonts weight matching](https://drafts.csswg.org/css-fonts-4/#font-weight-matching):
   below400 prefer available weights at/below the request descending, then above
   ascending; above500 prefer at/above ascending, then below descending. For
   requests400..500, search upward through500, then below the request descending,
   then above500 ascending. These are actual source OS/2 weights, not synthetic
   emboldening or @font-face descriptor overrides.
3. Order that weight's prepared resources by distance of nominal raster size from
   requested Q1000 size. On a distance tie choose larger raster size, then higher
   coverage precision, then earlier registration. This tie rule is a project
   bitmap-profile policy, not a browser rasterizer-parity claim.
4. The first candidate supplies primary/strut metrics even for empty text. For
   each admitted scalar, search real coverage in candidate order. A different
   weight in the same family is not searched for a missing character. Explicit
   later families/default may provide the glyph, and another strike at the chosen
   weight may provide a different prepared subset.
5. Only after all real coverage is exhausted may the primary resource's declared
   fallback scalar supply a replacement glyph. Otherwise return `MissingGlyph`.
   Preserve the source scalar/byte range and report the substitution explicitly.
   There is no guessed advance, synthetic .notdef or platform fallback search.

Changing catalog order/data, family IDs, requested style/size/weight or prepared
asset bytes changes the input. A source-font SHA alone is not a complete resource
identity. Identical catalog inputs/profile yield the same choices and geometry.

## Results and refusal

`measure` returns primary font metrics, maxima of primary/used font extents,
advance, ink edge union, glyph count and substitution count. The maxima are
observations of used fonts, **not the final CSS line box**; signed half-leading
and mixed-span line construction remain the layout engine's responsibility.
Ink and advance remain distinct; empty ink can advance, zero-advance glyphs can
have ink, and negative bearings are preserved. Advance is the sum of individually
rounded Q1000 scalar advances, matching the actual prepared positions.

`prepare` writes value-only glyph records into caller-owned storage: exact
resource/glyph IDs, resolved/source scalar, original UTF-8 byte interval, pen X
and scaled glyph geometry. It does not retain a text pointer, callback/context or
borrowed font pointer in the records. Their resource IDs must remain resolvable
to the same assets when consumed. The JFTP v1/v2 packet codec in `prepared_text.h`
preserves those per-glyph choices and geometry and decodes to a borrowed,
allocation-free view. It does not carry source text or raw pointers and does not
reuse the old single-font packet. `LayoutEngineOptions::text_system` can measure,
break and prepare supported scalar text into per-line packets when no letter
spacing or ellipsis is required. The cache retains each line's packet and
baseline plus the bound TextSystem generation; the layer builder must receive
the same service instance and generation and emits one
`PreparedText` command per line. Current line opportunities are ASCII whitespace,
hyphen/slash and CJK punctuation/ideographs; `overflow-wrap:anywhere` may split
at scalar boundaries. This remains bounded scalar wrapping, not cluster-safe
Unicode line breaking. Worker transport remains pending.

`TextSystem::paint` consumes a JFTP view against the same bound font set. It
revalidates exact resource/glyph identity and scaled geometry, admits total
glyph/pixel/work before writing, and rasterizes the canonical bitmap coverage
with Core source-over blending. Target and dirty clipping are applied after
complete glyph bounds are charged, so tiled paints stay under the same work
policy. Refusal leaves the target unchanged. Unsupported shaping, line controls,
letter spacing, ellipsis or an unregistered CSS family is refused on the Core
layout path; it does not fall back to a host callback. Leaving the TextSystem
option unset retains the existing provider path during this ABI migration.
The layout path defaults to at most 128 prepared glyphs per text node so retained
JFTP packets remain bounded independently of the lower-level service's 4096-glyph
limit; `LayoutEngineOptions::max_prepared_text_glyphs` can lower this bound.

Both methods strictly reject malformed UTF-8, overflow, unavailable glyphs and
budget refusal. Failure zeroes the metric result. Preparation validates the
complete input, capacity and both passes' work before writing, so refusal leaves
the glyph storage unchanged. Output storage must not alias the input, catalog,
font bytes or other output objects. Immutability is required across both passes.
Successful empty input may use null/zero glyph storage and still reports strut
metrics. Zero font size is valid. Measure, prepare, decode, and paint allocate
no memory; packet encoding uses a bounded temporary string and preserves output
on allocation failure.

Canvas2D can bind the same `TextSystem` for `measureText` and single-line
`fillText`; it encodes prepared glyphs and routes painting through the Core
software rasterizer. A refusal does not retry the legacy Canvas text callbacks.
The old Canvas backend setter remains only during the coordinated Runtime
consumer migration.

The initial horizontal repertoire is deliberately conservative: ASCII20..7E,
NBSP, U+2026, CJK punctuation3000..3029/3030..303F (tone marks302A..302F excluded),
Han3400..4DBF/4E00..9FFF, compatibility ideographsF900..FAFF and fullwidth
punctuation/symbolsFF01..FF60/FFE0..FFE6. Combining sequences, variation selectors,
ZWJ, bidi/Arabic/Indic, emoji, line controls and other scalars refuse with
`UnsupportedText`; malformed encoding returns `InvalidUtf8`. Italic/oblique and
unknown styles return `UnsupportedStyle`. No failed contextual case is silently
published as scalar shaping. This admission limit is not a claim that every
excluded character requires shaping. It remains a gate before production switch;
the common shaper/Unicode work must expand support explicitly.

## Budgets and migration boundary

Defaults:64 faces,16 requested families,64KiB text,4096 scalar glyphs,1Mi work
units. Faces/family-list lengths also have fixed64/16 hard limits. Smaller limits
can refuse otherwise valid input. Work charges source bytes, catalog visits and
binary-search-step estimates; it is a bounded proxy with fixed overhead, not
cycle/time measurement. Preparation reserves its second traversal before the
first output write. Measurement may fit a budget for which preparation refuses;
integration must propagate that refusal, not fabricate geometry or partial paint.

The caller owns and budgets output storage; the maximum count is not a promise
that an MCU can afford that output. No general target RAM/stack/latency admission
is inferred from host correctness. Static arrays bound candidate/family scratch;
there is no persistent policy cache or hidden global registration state.

Next route Core layout/count/cache/emission and software paint to this service,
extend supported shaping as required, and migrate Runtime font ownership, stable
IDs and worker/UI transport. The convenience host adapter is removed; callback
provider APIs remain until their Runtime consumers migrate in a coordinated
ABI/package/codec update.

Shared neutral inline paragraphs use the same current TextSystem and final
prepared packets. See [shared inline paragraphs](shared_inline_paragraphs.md)
for complete-context ownership, Q1000 initial packet offsets and operation limits.
