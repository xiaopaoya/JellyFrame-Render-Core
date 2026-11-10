# Canonical Background Delivery Checkpoint

Date: 2026-11-10. Core 0.9.0-dev / ABI4, branch
`codex/canonical-background-layers`, based on merged Core `708dd467`.

## Delivered in this candidate

- `Style` carries four value-semantic background image slots and one shared
  `background_color`. It defaults to one `none` slot. Scalar gradient fields,
  the packed overlay, activation flag and duplicate base color were removed.
  The measured Windows x64 `Style` size is 888 bytes, up104 bytes from the
  784-byte pre-layer ABI4 candidate.
- The bounded image-list parser handles one through four URL, gradient and
  `none` items. Cascade uses separate image/color/list slots, shorthand reset,
  cyclic longhand matching, final-layer shorthand color and one initial `none`
  slot. Invalid declarations leave prior winners intact.
- Resolver URL registration validates and commits a complete bounded batch.
  Deduplication is stable; budget and allocation failures preserve both the
  prior table and output IDs.
- The painter always consumes canonical layers in CSS order, paints the shared
  color below them, applies per-layer geometry/clips, preserves neighboring
  layers on refusal, and retains existing work/command/tile budgets. There is
  no scalar or packed-overlay painter fallback.
- Animation and repaint overrides use canonical base color and preserve image
  layers. Reuse comparison includes all four semantic layer values, so a fourth
  layer mutation forces rebuild.
- `JellyFrameRenderCoreConfig.cmake` exports `JellyFrameRenderCore_PROFILE_ID`
  from the same computed profile value as the installed JSON metadata.

## Exact-source qualification

Installed source identity is
`488874f1f2ce0880a47da5695cbe88a13a14b927461360cff3b58f8e9d2e6172/138`,
package version `0.9.0-dev`, engine ABI4. The Style header-only consumer asserts
`sizeof(Style)==888`, alignment8 and value-layer size28.

| Gate | Result |
| --- | ---: |
| Full Release CTest | 19/19 |
| Feature-cropped Release CTest | 18/18 |
| Full AddressSanitizer CTest | 19/19 at `24c3d1c2…/138` |
| Full UBSan trap CTest | 19/19 at `24c3d1c2…/138` |
| Installed full ABI4 consumer | 2/2 |
| Installed cropped ABI4 consumer | 2/2 |

An additional cascade regression covers an exhausted URL table where an
`!important` stylesheet image defeats a normal inline background shorthand.
The losing shorthand URL is no longer registered; its winning color and reset
components still apply independently. The focused style test passes. Full and
cropped Release suites remain19/19 and18/18; ASan/UBSan remain19/19 each, all
at source `488874f1…/138`.

The final source adds explicit installed-header version markers for canonical
value, layer-tree and prepared-text packet contracts. Review also found and
fixed shared-color clipping when padding/border insets are asymmetric: color
clip rectangles now use per-edge insets independently of background-origin;
empty inner clips are omitted with a diagnostic. Focused regression covers
asymmetric content insets, irrelevant invalid origin, and empty content area.
After this repair, full/cropped Release CTest are 19/19 and18/18; ASan/UBSan
are19/19 each. Installed full/cropped consumers are2/2 each at the same source
identity.

Builds are under `E:/Builds/canonical-background-layers-*`; installed prefixes
are `E:/Builds/canonical-background-layers-full-prefix` and
`E:/Builds/canonical-background-layers-cropped-prefix`. ASan execution on
Windows requires `C:/Program Files/LLVM/lib/clang/22/lib/windows` on `PATH`.

## Browser pixels

Four installed-package scenes ran through localhost HTTP at DPR1 against Chrome
157.0.8103.38 and Firefox153.0.4 at source `488874f1…/138`. Capture output and
screenshots are under `E:/Temp/f2b-v4-browser-4888-20261110`; comparison JSON
SHA256 is
`ad6f5067d4111a72828d58a8aa3bafa202d046640fae192dc99751732d8189d8`; the
installed capture executable SHA256 is
`fe26d57dc190cb153aec5313b496b8192273fea6298544ece793e7ce45f96539`. Exact
scene results are in the paired Runtime migration board and
`2026-11-10-background-multilayer-f2b-browser/REPORT-v4-4888.md`.

`none-fallback` and `implicit-none-clip` match exactly in both browsers.
`mixed-four` differs in1056 interior pixels with maximum channel delta1 in both
engines. `geometry-repeat` differs in1056 interior pixels, maximum delta3 in
Chrome and4 in Firefox; mean maximum deltas are1.6364 and1.8636. These remain
measured alpha/interpolation differences, not a blanket parity claim. The full
report records fixture, image asset, CSS, browser, executable and screenshot
hashes.

## Remaining gates

- MCU `Style` memory/peak heap, stack, image decoder/cache and work budgets have
  not been qualified. The 888-byte host size does not establish device admission.
- Runtime v9 has an isolated codec/provider commit `9d0571a5`. Requalification
  against clean Core exposed stale assumptions about optional CSS box-shadow
  payloads and missing exact-package identity checks. Those compile issues are
  being corrected. The full consumer now compiles but terminates at runtime
  with Windows status `0xC0000409`; its cause and cropped runtime result remain
  open. Worker and ordinary app entry remain open. Production Runtime
  lock/default stays 0.8.0 / ABI3 / v5.
- Required GitHub PR CI and merge have not been run for this candidate.
