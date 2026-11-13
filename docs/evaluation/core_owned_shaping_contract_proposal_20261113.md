# Core-Owned Shaping Contract Proposal

> Status: proposal only. None of the API, packet, or acceptance gates below are
> implemented by this document. The proposal is based on the current Core text
> contracts and historical experiment sources available in the repository's Git
> objects. It is intended to guide a separately reviewed ABI change.

## Purpose and constraints

This proposal defines a bounded shaping boundary for Render Core that can
support cluster-safe layout and shared inline paragraphs on desktop and
embedded targets. Core should own shaping decisions, fallback, metrics, source
mapping, line-break safety, and glyph placement. Runtime should register and
retain immutable font resources and provide display/frame transport, without
supplying shape, measure, line-metric, or glyph-paint callbacks.

The initial shaping profile should be explicit and conservative. Unsupported
scripts, directions, controls, font features, resource combinations, or limit
exhaustion must be reported as refusal. They must not silently invoke a host
shaper or a second renderer. Admission can expand through separately measured
and tested profiles.

## Current and historical contracts

The current [`TextSystem`](../reference/render_core/text_system.md) binds a
borrowed immutable array of registered `TextFontFace` resources. A stable
nonzero `resource_id` identifies each exact resource while results remain live;
the host retains the registration array and bytes. `TextSystem::measure` and
`prepare` accept one scalar run and share deterministic family/weight/strike
selection. `PreparedTextGlyph` stores one resource and glyph ID, a source
codepoint and UTF-8 byte interval, Q1000 pen position, and scaled geometry.
The current profile expressly does not promise contextual shaping or
cluster-safe wrapping.

The current JFFONT2 resource is a prepared bitmap-glyph catalog, not an
OpenType font: it stores scalar coverage, glyph IDs, advances, vertical metrics
and grayscale bitmap coverage, but no GSUB/GPOS tables, kerning pairs,
ligatures or contextual substitution data. Contextual shaping therefore
cannot be implemented correctly from JFFONT2 alone. Adding `shape()` over the
existing resource without changing that input model would only rename scalar
mapping and must not be presented as cluster-capable shaping.

JFTP v1/v2 is a value-only prepared scalar packet with a 32-byte header and
52-byte glyph records. Decode is allocation-free and accepts unaligned packet
storage. V2 adds a signed Q1000 initial X; v1 retains zero initial X. The
current record validation expects glyphs to represent a contiguous sequence of
source scalars and derives subsequent X positions from advances. It cannot
represent a many-to-one or one-to-many source/glyph mapping or cluster tables.
The shared-inline delivery binds immutable paragraph snapshots, source/member
topology, TextSystem identity and generation, final packets, and line geometry;
it constructs the complete draft before publishing and preserves prior state
on failure. See
[`shared_inline_paragraphs.md`](../reference/render_core/shared_inline_paragraphs.md).

The historical `experiment/web-feature-expansion` implementation is reference
material, not a compatible API to restore. Its private
`InlineShapeProvider` callback accepted a contiguous UTF-8 run and integer
font key, streamed glyph IDs, byte clusters, advances, and offsets, then
returned a host `font_instance_id`, total advance, and line height. It checked
whole-run cluster coverage, normalized-token boundaries, horizontal-only
advances, measured-versus-emitted advance equality, and stable font identity.
Its mapper associated shaped clusters with source owner spans, including
clusters spanning more than one owner. Whole runs were the invalidation domain
because contextual influences can cross cluster boundaries. The JFGR packet
then stored a host font-instance ID and integer positioned glyphs in a 24-byte
header plus 12-byte records. It assumes one host font instance per packet and
does not carry Core resource identities or Q1000 geometry. These callback and
packet contracts conflict with current Core ownership and JFTP; the useful
cluster and source-map invariants should be re-expressed against `TextSystem`
and registered Core resources.

## Proposed API and ownership

Before freezing the API, choose and qualify the shaping-data path. The Core
boundary remains callback-free in every option:

1. Core consumes original OpenType font bytes and owns a portable parser and
   shaping engine. This offers broad font behavior but has the largest code,
   scratch-memory and MCU qualification cost.
2. The canonical font preparation tool emits a bounded, versioned shaping
   subset alongside prepared glyph resources (a new JFFONT profile). Core owns
   deterministic substitutions, positioning and cluster mapping over those
   tables. The profile must identify supported tables/features and preserve
   enough source data to make desktop and MCU results identical.
3. If neither data path fits product limits, retain scalar JFFONT2 behavior and
   defer contextual shaping. Do not add host callbacks or silently downgrade
   unsupported sequences.

The first two options need an explicit format/version, dependency and licensing
review where applicable, generator validation, sample-font corpus, and measured
resource/scratch/work budgets before the shape API is frozen. A shaping
algorithm test using hand-authored synthetic mappings is useful for the mapper,
but it does not prove production font resources can provide shaping data.

The following is a shape of an API, not a final spelling or committed ABI:

```cpp
TextSystemStatus shape(std::string_view source,
                       const TextFontRequest& request,
                       ShapedTextGlyph* glyphs, std::size_t glyph_capacity,
                       ShapedTextCluster* clusters, std::size_t cluster_capacity,
                       ShapedTextMetrics& metrics) const noexcept;
```

The API should be allocation-free at its public boundary, using caller-owned
output spans (or a documented bounded Core scratch arena). It should snapshot
and validate inputs before publication, report required counts on success or
capacity refusal according to one documented convention, and leave output
arrays and metrics unchanged/cleared according to an explicit transactional
rule. A future convenience packet encoder can allocate separately and must
preserve its prior output on failure.

Each shaped glyph should carry exact `resource_id` and `glyph_id`, resolved
cluster/source linkage, Q1000 pen advance and X/Y offsets, and the final scaled
ink geometry needed by Core paint. Each cluster should identify its complete
source byte interval and glyph interval. The result should report total
advance, primary font/line strut metrics, used-font extents, and ink bounds as
separate values. Measurement and preparation must use the same shaping result
and policy; measurement cannot report geometry inconsistent with preparation.

The host continues to own immutable font bytes and registration storage. A
resource ID names one exact prepared resource and must never be reused for
different bytes while any cache, packet, display command, or retained frame
refers to it. Live layout bindings include TextSystem instance/catalog
generation, as current prepared-text caches do. A retained packet contains
resource IDs, not pointers or host font-instance IDs. Runtime must keep a
catalog snapshot with those same IDs alive through final paint/replay; catalog
replacement creates new IDs or a distinct generation and cannot mutate bytes
visible to old frames. Core validates packet glyph/resource identity against
the bound immutable catalog before target writes.

Family fallback should use the current ordered-family request policy, with an
additional cluster-atomic rule: a cluster is resolved to a coherent set of
resources according to documented shaping behavior, and fallback must not
split a combining or contextual cluster across families. Initially, if the
registered resources cannot shape the complete cluster under the admitted
profile, Core refuses the run. Selection, fallback, metrics, and shaping stay
inside Core; no callback or implicit platform fallback is part of the API.

## Source, cluster, and owner mapping

The shaper operates over a complete logical run and returns clusters in a
documented order. The first profile should be horizontal LTR. Each cluster
must cover a nonempty UTF-8 source interval at valid scalar boundaries;
clusters should cover the admitted source exactly, in order, without gaps or
overlap. Each cluster maps to one or more glyphs, and glyphs may have zero
advance or share a cluster. The representation must support many source
scalars to one glyph and one source interval to several glyphs. Unsupported
reordering/direction refuses until an explicit visual-order and mapping
contract is added.

Wrapping may occur only between clusters. `overflow-wrap:anywhere` must not
split a cluster. For a whole shaped run, contextual shaping can depend on
neighboring clusters; therefore live invalidation should conservatively bind
the complete run (or paragraph shaping dependency), not only a changed cluster.
The source map remains independently useful for owner-specific paint and hit
behavior.

For shared inline paragraphs, add an owner-span table that links normalized
source/token intervals to DOM owner identities and source byte ranges. A
cluster may intersect multiple adjacent owners; preserve all relevant spans
with their order rather than assigning a single inferred owner to the glyph.
Keep omitted collapsible whitespace and its DOM position in the existing
paragraph source proof even when it generates no glyph. Owner mapping supplies
source color/decorations and per-line logical hit fragments; it does not decide
shaping or infer style. Cache/binding validation must include the complete
source order, style inputs, and topology already required by shared paragraphs.

## Packet strategy and compatibility

Preserve the JFTP v1/v2 decoder and encoder behavior. Do not reinterpret their
record layout. Shaped output requires a new versioned format, either JFTP v3
with explicit table offsets/counts or a distinct magic such as JFSP. The final
choice should follow consumer capability negotiation and packet ownership
review. The format must use fixed-width little-endian fields, checked arithmetic,
exact total-size validation, and bytewise reads safe for unaligned buffers.
It must carry no pointers or source text. It needs explicit glyph and cluster
tables, resource IDs per glyph, Q1000 positioning/geometry, source byte ranges,
and, if transport consumers need owner-aware operations, owner-span tables.
Do not duplicate owner style/color in the shaping packet unless a separately
reviewed transport contract requires it; those remain paint inputs.

Decode should allocate nothing, validate all counts and cross-table ranges
before publishing a view, and preserve the caller's previous view on refusal.
Packet encode should be transactional. Core paint must prevalidate every
resource, glyph, geometry value, and aggregate pixel/work budget before the
first write. Existing JFTP packets remain replayable under their existing
contract; shaped packet support requires an explicit Runtime frame capability
version and consumer update rather than pretending a JFGR packet is JFTP.

## Budgets and transactional behavior

Add shape-specific limits under the existing per-operation resource ledger:
source bytes, shape calls, shaped bytes, emitted glyphs, clusters, owner spans,
planner/shape work units, packet bytes, and cumulative acquired payload. Set
conservative defaults and hard ceilings suitable for target qualification;
all arithmetic must be overflow-checked. Candidate and final shaping, retries,
scratch acquisition, and speculative preparation are charged cumulatively.
Rollback does not refund work or acquired-capacity accounting. Host callback
time and allocations are not bounded because the proposed Core API has no host
shape callback.

Construct the complete shaped paragraph draft, source maps, final packets, and
all bindings before publishing any cache, box geometry, or display command.
On admission, shaping, mapping, allocation, coordinate, resource, or budget
failure, preserve previous bindings and geometry. Existing shared-inline
fallback may run only as an explicit whole-context decision; committing it
must first retire an earlier shared/shaped binding and publish its geometry
transactionally. Painting validates all packet/resource constraints before
writing, so refusal leaves the target unchanged. Frozen packets remain
replayable after live DOM mutation while their exact resource catalog remains
available.

Current shared-inline accounting defaults provide a starting reference, not a
shaping budget commitment: 128 attempts, 64 KiB charged payload per context,
256 KiB cumulative acquisitions per public layout call, 262144 planner work
units, 2048 measurements, 262144 measured bytes, 128 nodes, depth 32, 8192
source bytes, 128 prepared glyphs, and 128 lines. Shaping may have higher
cluster/map costs; proposed limits must be measured on target devices before
adoption and must share the enclosing operation ledger instead of creating
independent allowances.

## Resource Model Assessment

The product Noto Sans SC 2.002 OTFs provide a useful storage bound for the
first profile. The three faces are 8,482,020 bytes (Regular), 8,508,580 bytes
(Medium), and 8,716,392 bytes (Bold). Their complete raw GSUB+GPOS tables are
51,962, 51,962, and 52,158 bytes respectively: 156,082 bytes across the three
weights, before any generated index or alignment. Each OTF exceeds the
ESP32-S3 port's documented 8 MiB application partition, while the current
16 px Regular JFFONT2 asset is 716,274 bytes. This makes original-OTF parsing
an unsuitable first embedded resource path and makes a generated shaping
profile a plausible size candidate. The table figures are upper bounds for
carrying the complete layout tables, not measurements of a proposed compact
profile or of runtime memory.

The shaping lookup output also exposes a resource gap: JFFONT2 addresses
bitmaps through Unicode scalar mappings, while GSUB can emit glyph IDs that are
not the scalar's cmap glyph, and GPOS mark lookups can emit zero-advance glyphs
with nonzero offsets. The profile must therefore provide validated bitmap and
metric lookup by glyph ID for every glyph the admitted substitutions can
produce. Appending GSUB/GPOS bytes to a scalar-only JFFONT2 resource would not
make ligatures or marks drawable.

The current evidence selects a generated, Core-owned shaping-data path for the
next prototype. It does not select a final JFFONT version, packet format,
public API, or supported-feature promise. The prototype still needs to prove a
deterministic resource containing the admitted Script/LangSys lookup closure
and its glyph-ID catalog, then measure encoded flash size, target load peak,
scratch, bounded work, and latency on the ESP32-S3 profile. Until those
measurements pass explicit budgets, scalar JFFONT2 remains the only supported
behavior and all shaping stays opt-in/experimental.

## ABI and release implications

`TextSystem`, prepared packet views/records, layout options/cache bindings, and
frame transport are installed integration surfaces. The proposal changes
source API and packet behavior, so it requires an explicit Core ABI/package and
source-manifest identity decision, rebuilt installed consumers, and a matched
Runtime lock/capability update. No compatibility wrapper around historical
host shapers is proposed. JFTP v1/v2 decoding should remain supported for
already-produced scalar packets for the documented compatibility window; the
shape packet version and Runtime transport capability are negotiated
separately. Product defaults stay unchanged until Core, Runtime, desktop, and
embedded qualification pass as one matched package identity.

## Staged implementation and acceptance gates

1. **Select a shaping resource model.** Compare original-font consumption with
   a bounded generated shaping-table profile against code size, canonical data
   availability, generator complexity, and ESP32 memory/work budgets. Gate:
   a named profile and versioned resource proposal, fixture fonts/corpus, and
   measured host/target storage estimates. If neither path meets limits, stop
   at scalar JFFONT2 rather than inventing partial contextual semantics.
2. **Resource-backed shaping primitive.** Define the admitted shaping profile,
   `TextSystem` result types, stable resource resolution, and deterministic
   metrics/prepare behavior against the selected source data. Gate: synthetic-font tests cover simple scalar
   identity shaping, combining and ligature mappings, multiple glyphs per
   cluster, zero-advance glyphs, fallback atomicity, stable repeated output,
   and explicit refusal for unsupported direction/script/features.
3. **Cluster and owner mapper.** Build a pure bounded mapper from shaped source
   intervals to normalized paragraph tokens and owner spans. Gate: property and
   regression tests prove complete ordered coverage, token-boundary checks,
   multi-owner clusters, omitted whitespace/source positions, malformed UTF-8
   refusal, and no output mutation on every failure.
4. **Packet and paint support.** Choose and implement JFTP v3 or JFSP while
   retaining JFTP v1/v2. Gate: golden byte fixtures, unaligned decode,
   malformed/truncated/overflow/count-table fuzz cases, cross-table validation,
   no-allocation decode, encode rollback, catalog mismatch refusal, and
   unchanged paint target on resource/geometry/budget refusal.
5. **Ordinary layout integration.** Add opt-in cluster-safe wrapping and cache
   validation using the shared Core shaper. Gate: line breaking never splits a
   cluster, Q1000 positions survive fractional widths, contextual run changes
   invalidate the full dependency, measurement agrees with prepared advance,
   and existing scalar JFTP output remains unchanged when shaping is not used.
6. **Shared inline integration.** Attach shaped runs and owner maps to the
   opaque paragraph binding; emit owner-colored fragments, logical hit
   fragments, damage bounds, and frozen packets transactionally. Gate: mixed
   owner styles/colors, preserved whitespace and CRLF, cluster-safe wrap,
   mutation/damage validation, speculative rollback, allocation-failure sweep,
   and stale-binding retirement all pass.
7. **Matched Runtime and device release.** Update package lock, transport
   capability, catalog lifetime, and installed consumer tests. Gate: clean
   package-consumer builds; full/cropped/disabled feature profiles; desktop
   rendering and retained-frame replay; enabled ESP32 build and worker/session
   teardown; measured packet RAM, peak scratch, work and latency against named
   device budgets; visual smoke-page acceptance on physical hardware before
   changing defaults.

## Unresolved choices

- Whether the first supported profile includes only LTR horizontal scripts or
  also a narrowly specified RTL profile; visual-order glyph storage and logical
  source mapping need an explicit packet/layout contract before RTL admission.
- Whether shaping returns final scaled geometry or resource-local metrics plus
  a separately versioned scale profile. The first option simplifies replay;
  the second can reduce packet size but requires stricter catalog/profile
  identity validation.
- Whether cluster and owner maps live in a distinct packet or only in the
  in-process immutable paragraph binding. Worker/UI hit testing and transport
  requirements determine whether the maps must cross the frame boundary.
- The precise cluster-atomic fallback rule when a cluster's glyph sequence
  uses multiple registered resources, and how font fallback participates in
  contextual shaping.
- Whether a shaped empty run has strut metrics and an empty packet, matching
  current scalar `TextSystem` semantics, or is rejected by the new API.
- Concrete MCU limits for shape work, per-paragraph glyph/cluster counts,
  packet bytes, scratch capacity, retained frame memory, and acceptable
  latency. Existing host defaults are not device qualification.
- Packet magic/version and compatibility window. This depends on Runtime's
  installed frame codec and whether older consumers need to replay scalar
  packets after the ABI transition.

## References

- [`TextSystem` current contract](../reference/render_core/text_system.md)
- [`PreparedText` packet API](../../include/render_core/prepared_text.h)
- [`TextSystem` installed API](../../include/render_core/text_system.h)
- [`Core-owned text migration decision`](../reference/render_core/core_owned_text_migration.md)
- [`Shared inline paragraph contract`](../reference/render_core/shared_inline_paragraphs.md)
- Historical sources in Git ref `experiment/web-feature-expansion`:
  `src/inline_shaped_paragraph.h`, `src/inline_shaped_paragraph.cpp`,
  `src/inline_text_shape_map.h`, and `include/render_core/glyph_run.h`.
