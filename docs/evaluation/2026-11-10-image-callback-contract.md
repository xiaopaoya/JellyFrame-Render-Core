# Image callback and independent painting clip contract

Date: 2026-11-10. Base: 421a4ffb (typed command PR14 tree).
Worktree: `E:/Workspaces/CoreImageContract-20261110`.

This bounded ABI4 / 0.9.0 source migration replaces resolver output with
`ResolvedImageResource` and image painter arguments with `ImagePaintRequest`.
BasicPaint gradients and ImagePaint now consume independent relative painting
clips. Raster, rounded temporary surfaces, compositor affine sources, equality,
copy/move, translation and metadata flattening preserve full sampling geometry.
No parser, canonical Style, CSS shadow, layout or table changes are included.
Existing URL object-fit/sizing behavior and legacy content placeholders remain.

Known background natural sizes are validated in 1..8191 per axis. Unknown is
accepted by the legacy URL path; Invalid or malformed Known metadata omits the
background image. Content keeps its valid-handle semantics without adding a
natural-size decoder limit. Failed resolver results and zero handles are never
published. Resolver/painter contexts and provider resource bytes stay borrowed
through the last paint; Core does not acquire provider ownership.

Measured Windows x64 Clang 22 release sizes: DisplayCommand 128, BasicPaint 24,
TextPaint 32, ImagePaint 32, BackgroundPaintClip 8 bytes. The largest payload
remains TextPaint/ImagePaint at 32 bytes, so clips do not grow DisplayCommand.
No canonical all-command-kind API stamp was introduced.

Qualification used fresh E: build directories with Ninja and Clang 22.1.8:

- `E:/Builds/CoreImageContract-20261110`: Release full, 16/16 CTest.
- `E:/Builds/CoreImageContract-crop-20261110`: Release with Canvas2D,
  modern paint, flex/grid and advanced forms OFF, 15/15 CTest.
- `E:/Builds/CoreImageContract-asan-20261110`: RelWithDebInfo,
  `-fsanitize=address`; targeted main renderer, typed command and callback
  suites 3/3. Runtime DLL directory was added to the process PATH.
- `E:/Builds/CoreImageContract-ubsan-trap-20261110`: RelWithDebInfo,
  `-fsanitize=undefined -fsanitize-trap=all`; same targeted suites 3/3.
- Installed callback consumer: 1/1 for each full/cropped profile; installed typed
  command consumers: 2/2 for each profile. Callback consumers use Release to
  match the installed Windows CRT/iterator level.

The installed source hash for both profiles is
`b62c4a054dfbb7084cf54f36b32a56436fd4f463c54abbcf0e1e661d2b484862`.
Consumers require the exact source hash and profile ID. They compile against
installed public headers and link the exported target without private includes
or compiling Core sources.

Focused coverage includes cleared resolver output, handle publication refusal,
Known/Unknown/Invalid metadata, content compatibility, independent sample and
painting rectangles, external clip intersections, deliberately unconstrained
provider writes, missing resources, malformed clip metadata, rounded painting
areas, absent metadata on temporary targets, affine source callbacks, gradient
sampling and clip bounds in full/cropped profiles, value equality and translation,
and sampling-preserving metadata flattening.

A plain Windows UBSan configuration was attempted first and failed at compiler
probe linking because the installed runtime has unresolved `__coe_win` helpers.
The trap configuration avoids that environment limitation. The initial ASan
execution needed its runtime DLL on PATH; qualified runs above include it.

## Final integrated package

After rebasing onto master910e8d98, the final source identity is
1014e055dea6a62a27ae9f0db84736a59c972039b4cc4018f3187b8044c0193d /135.
Fresh full17/17, cropped16/16, AddressSanitizer17/17 and UBSan trap17/17
all pass. Full/cropped installed callback consumers each pass1/1 and typed
linked/header consumers each pass2/2. All eight wrong-source/profile checks
reject the mismatched configuration.

Final logs are at E:/Temp/core-image-contract-combined-qualification-20261110.
Installed consumer logs and machine summary are
E:/Temp/core-image-contract-combined-installed-summary-20261110.json and
E:/Temp/core-image-contract-combined-{full,cropped}-{image_callback,typed_command}-consumer-20261110.log.
Earlier independent-base observations above retain their named identities.
