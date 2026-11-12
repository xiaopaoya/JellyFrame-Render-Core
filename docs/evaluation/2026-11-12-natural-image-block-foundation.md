# Natural image block foundation — 2026-11-12

This is the first bounded natural-image migration slice on Core 3468c310
(70e076f1 plus the locally qualified text-style adaptation). It is an additive
unpublished 0.9.0 / ABI4 candidate, not Runtime or device adoption.

## Source and scope

The actual experimental source is 88eb0539's natural_image_sizing implementation:
immutable metadata-only host queries, cached declines, both/one auto dimension,
CSS2 coupled min/max constraints and authored versus natural ratio box rules.
Current code selectively adapts those rules to typed computed dimensions and
Q1000 final geometry. It does not copy the old layout/text files or font callbacks.

The first consumer admits empty static block img elements with nonempty src
through an ordinary block ancestor chain. A direct picture child is excluded.
Inline/inline-block contexts, natural inline paragraphs (including later
fa4071b2/59b4bbf1 source work), picture selection, positioned images and
Flex/Grid contributions remain pending. Existing intrinsic source queries still
refuse replaced sources atomically. Existing mixed CSS declaration admission
does not change; the focused test supplies retained mixed values directly.

## Host contract and bounds

JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION is 1. Hosts opt into
LayoutEngineOptions::natural_image_sizing and return existing ImageIntrinsicSize
values from their immutable image snapshot. Known dimensions must be positive,
at most 8191 per axis, and density-corrected CSS pixels. There is no image-byte
decoding/loading or handle resolution in layout. The metadata callback/context
are borrowed through the layout call. The host binds its existing paint resolver
to the same immutable snapshot/source selection and retains image bytes through
last display-list replay. Metadata changes require a new layout.

The pass-local sparse memo stores successes and declines, reserving storage
before any callback. Default caps: 128 callback attempts, 128 entries and 4096
scratch bytes; hard entry ceiling 4096. The x64 tested Entry is 16B: default
reservation is 2048B. An explicitly enlarged host configuration can reserve at
most 65536B of memo payload at the hard entry ceiling, also constrained by the
configured scratch cap. Allocator bookkeeping and transitive host allocations
are excluded. No per-LayoutBox field was added: LayoutBox remains 1304B in the
tested x64 profile, so incremental retained cost at 4096 boxes is zero bytes.
No MCU size or latency qualification is inferred from these host measurements.

Both definite pixel/HTML axes skip metadata lookup. Percentage axes can be
indefinite, so their source metadata can be captured before basis resolution.
Unknown/invalid/limited metadata follows legacy layout fallback. A failed
allocation closes new memo admission while existing entries remain stable.
No metadata queries occur in painting or display-list replay.

Q1000 ratio conversion uses existing scale_size_units where its int32
denominator/product guards admit the inputs. Both-auto scaling must also handle
the initial coordinate-box ratio after decoration or zero-content clamping;
its components can exceed int32. A private exact binary-fraction fallback handles
those bounded components in at most 42 steps, without a product overflow.
Unrepresentable decorated minima are refused before constructing clamp ranges.

## Current qualification

Clang 22.1.8, Ninja, Windows x64. Source manifest identity:
97aa69552d4e5e32b4996748330b7fea7312295f307b299129638931a4c2449d.

- Full Release: 29/29 CTest tests pass.
- Minimal profile (Canvas/modern paint/Flex/Grid/advanced forms disabled):
  focused natural-image test passes.
- Installed public-only natural_image_consumer passes against both profiles,
  checking exact source hash, ABI4, API gate and natural fractional geometry.
- ASan RelWithDebInfo focused natural-image test passes. Initial ASan Debug
  CRT executable fails during ucrtbased/MSVCP140D startup with a bad-free;
  its raw log is retained. That configuration is not a passing qualification.
- Focused coverage: HTML hints and CSS overrides; one/both auto; explicit/auto
  ratios; coupled/opposing min/max; border/content sizing; missing/invalid data;
  query/memo/scratch limits; callback bad_alloc; cached declines; retained mixed
  dimensions; arena path; intrinsic refusal; old/new snapshots; fractional flow,
  paint and repeated replay. Arithmetic includes 64 small exact-rational cases,
  three large ratio/saturation cases and extreme decorated typed minima.

Builds are under E:/Builds/natural-image-delivery-20261112-*; raw logs are under
E:/Experiments/natural-image-delivery-20261112. No device access, push, PR,
Runtime source, global execution-board or shared-memory update was performed.
