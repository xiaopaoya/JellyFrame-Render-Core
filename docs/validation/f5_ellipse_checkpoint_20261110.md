# F5 elliptical radius candidate checkpoint

Candidate base: Core `b5b1533b`, branch `codex/f5-ellipse-slice`.
This is not a merged feature or target-resource qualification.

## Representation and boundaries

Basic, Text and Image paint values carry an explicit encoded Y radius; `-1`
alone inherits X for legacy producers. Packed nonuniform radii legitimately
have the sign bit set. FlattenedClip appends Y after its existing parent field.
CSS percentages resolve against width and height independently. Both axes use
one overlap reduction, then project to bounded integer raster coordinates;
rounding down prevents a projected edge sum from overlapping again. This is
integer geometry, not fractional browser geometry parity.

Uniform axis values remain full-range signed positive scalars; nonuniform
corners use the existing 7-bit-per-corner representation. Unrepresentable used
values refuse both axes together with a diagnostic. Functional radius syntax
and corner longhands remain outside the admitted grammar. Elliptical shadow
painting is explicitly refused with a diagnostic. Background image/gradient
box geometry retains its existing uniform border/padding restriction; shared
color clips use physical per-corner insets. Y is assigned where each semantic
paint command is created, never inferred from command rectangle equality.

Measured Windows x64 Release layouts (Clang22.1.8):

| Value | Pre-F5 | Candidate | Delta |
| --- | ---: | ---: | ---: |
| Style | 888 | 920 | +32 |
| BasicPaint | 24 | 28 | +4 |
| TextPaint | 32 | 36 | +4 |
| ImagePaint | 32 | 36 | +4 |
| DisplayCommand | 128 | 136 | +8 |
| FlattenedClip | 24 | 28 | +4 |

The corresponding compile-time guards explicitly record these changes in the
unpublished ABI4 candidate. These sizes do not prove MCU peak heap admission.

## Verified host checkpoint

Fresh Release Clang/Ninja build:
`D:/CodexTemp/migration-followup/builds/f5-ellipse-full`.
CTest passed 19/19 with individual 45-second timeouts (2.55 seconds total).
Exact installed profile: `render-core-default`, engine ABI4, `0.9.0-dev`.
Source manifest: `69926c30f785172ef449e2fbdb798477646d362e9c9ef656b7c6e07559e5d49d`,
139 inputs. Installed linked/header-only typed and multilayer consumers pass
2/2 each. Cropped, sanitizers and final Runtime consumers are still pending
at this checkpoint; they must be bound to this exact source or refreshed.

## Browser comparison

Independent reference fixture: `E:/Temp/f5-radius-oracle/oracle.html`, SHA256
`654e2eb4e453b1d7f15a10bc8204593d1006a472d7f16a3170c0ee5d9d451cff`.
Edge156 and Firefox153 reference PNG/JSON are retained there. The installed
consumer at `D:/CodexTemp/migration-followup/f5-browser-consumer` renders the
same CSS/HTML at 640x420 with a white compositing background. It requires an
explicit matching installed source hash. Capture: `core.ppm`/`core.png`;
comparison: `core-browser-comparison.json` and `core-vs-{edge,firefox}.png`.

| Scene | Edge differing pixels / max channel error | Firefox differing pixels / max channel error |
| --- | ---: | ---: |
| asymmetric ellipse | 276 / 40 | 242 / 40 |
| mixed percentages | 257 / 38 | 232 / 39 |
| overlap reduction | 329 / 83 | 314 / 111 |
| asymmetric corners | 301 / 36 | 255 / 41 |
| nested overflow clip | 329 / 93 | 314 / 90 |

Differences concentrate around antialiased boundaries. Integer radius
projection and repeated clipping remain visible residuals, especially overlap
reduction/nested clips. This observation is not a pixel-parity pass and does
not establish a product error tolerance. No text, images, strokes or gradient
browser differential is implied by these five solid-fill scenes.

## Debug test failure diagnosis

The apparent hanging observer test was a CRT abort dialog caused by global
allocation-failure injection reaching MSVC Debug iterator-proxy construction
inside a noexcept constructor. Production DOM sources match the base. The
test-only injector now accounts for Debug metadata and allows unwinding; the
Release persistent-exhaustion path remains. Debug/Release observer runs pass.
All MSVC test executables use stderr CRT diagnostics, and the combined runner
reports uncaught test exceptions as failures. Temporary assert, throw and
abort probes retain nonzero exits and finish without modal dialogs.

The rounded-coverage test now catches exceptions and revealed real failures:
negative packed Y mistaken for the legacy sentinel, and lost large-circle
overflow protection. Both are fixed; independent 4x4 ellipse samples, common
scaling, odd-edge projection and large uniform insets are exercised.

## Remaining delivery gates

Refresh cropped and ASan/UBSan suites, exact installed profile consumers,
Runtime v9/v10 signed-axis round trips and legacy ABI compatibility. Qualify
remaining paint routes and retained damage/hit behavior, evaluate browser
edge residuals, document supported grammar, and measure target resource/work
cost before claiming end-to-end or device admission. No F5 commit/PR exists.
