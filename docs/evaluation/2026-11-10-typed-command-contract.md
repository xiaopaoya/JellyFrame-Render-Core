# Typed paint command contract

This slice replaces the flat paint fields with a checked Basic/Text/Image union
on the unpublished 0.9.0 / ABI4 line. All Core producers, software raster paths,
Canvas prepared-text emission, benchmarks and existing tests use the active
payload. The nine existing command kinds retain their rendering semantics.
Public construction uses `set_type()` or the explicit constructor. Changing
kind resets its payload, including changes within the shared Basic/Text family.
Reselecting a kind preserves the current payload. Inactive accessors reject
access with `std::logic_error`. Invalid kind selection preserves the old value.

The owned string and common rectangle/colors/transform remain outside the
union. Copy assignment prepares a complete copy before publication. Equality
compares semantic fields. PreparedText has dedicated origin and baseline fields.
The common translation helper moves those fields alongside the rectangle.
Both layer flatten entry points have a regression for translated prepared text.
An isolated rect-only translation variant fails that regression.

## Version and layout

This source API replacement is deliberately part of the unreleased ABI4
migration window. It replaces the old public scalar field API directly.
Installed consumers require the exact candidate source identity and profile.
The package stays `0.9.0-dev` with engine ABI4 until coordinated release/adoption.

Windows x64 Clang22.1.8 measures the preceding command at152 bytes and this
command at128 bytes. BasicPaint is16 bytes, TextPaint32 and ImagePaint24.
The reduction is24 bytes per command. Later paint payload additions need their
own explicit layout decision. Background painting clips continue to be a
separate geometry value in this slice. Their command/painter integration follows
with the structured image callback contract. The existing BoxShadow kind stays
in the Basic family.

## Qualification

Frozen source/test/tool/benchmark snapshot
`E:/Temp/core-typed-command-frozen-20261110` has213 raw-hashed inputs.
The normalized production source identity is
`7841bb3ba98503de39fb52fca9097fa7acad89eb8f6bf2c18512d0467dd9d275`,
135 inputs. Qualification logs are at
`E:/Temp/core-typed-command-qualification-20261110`.

| Gate | Result |
| --- | --- |
| Full Release including three CPU2D output smoke tests |18/18 pass|
| Cropped Release |14/14 pass|
| AddressSanitizer Release |15/15 pass|
| UBSan trap Release |15/15 pass|
| Installed full linked and header-only consumers |2/2 pass|
| Installed cropped linked and header-only consumers |2/2 pass|
| Wrong source/profile configurations, both profiles |4/4 reject|

Full/cropped prefixes are
`E:/Builds/core-typed-qualified-full-20261110-prefix` and
`E:/Builds/core-typed-qualified-cropped-20261110-prefix`.
Their profiles are `render-core-default` and
`render-core-minimal-no-forms-advanced`. Installed consumers compile through the
imported public target and a separate executable uses installed headers alone.
Raw consumer logs are `E:/Temp/typed-consumer-{full,cropped}-*.log`.

Existing pixel expectations stay intact. The new focused regression verifies
defaults, kind selection/reset, invalid/inactive access, independent owned
copies, move/self assignment, explicit origin and saturated translation.
The host memory layout measurement does not establish MCU cycle/heap effects.
