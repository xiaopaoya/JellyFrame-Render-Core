# F4 fixed separate-table slice, 2026-11-10

This candidate is based on Core PR #17, commit
`431789fd78fc7f9a654f2f4a13d5e28be8f6e32f`, on branch
`codex/f4-fixed-table-slice`. It retains engine ABI 4 and the canonical
four-layer background and typed display-command contracts.

The admitted layout requires `table-layout: fixed`, an explicit integral pixel
table width, explicit positive integral pixel column widths, one `colgroup`,
one `tbody`, matching cell counts in each row, separate borders and qualified
integral sizing throughout the subtree. It supports spacing, cell padding and
borders, wrapped and hard-break row sizing, vertical alignment, top/bottom
captions, inherited table border state, auto horizontal margins, relative
table positioning, cell/caption backgrounds and proportional surplus track
growth. Unsupported geometry rolls back partial layout mutations before
ordinary bounded fallback. Spans, nested tables, automatic or intrinsic table
sizing, collapsed borders, percentages, fractional sizing, explicit cell
widths, table padding/height/constraints, and unsupported structural styles
remain outside the fixed path.

`border-spacing` calc() expressions are also refused because the shared length
function parser exposes them after integer rounding. This prevents subpixel
expressions such as `calc(-0.4px)` and `calc(0.4px)` from becoming zero spacing.

## Validation

Compiler: Clang 22.1.8 on Windows x64, Ninja, Release unless noted.

| Gate | Result |
| --- | --- |
| Full Core CTest | 19/19 passed |
| Cropped Core CTest (Canvas2D, ModernPaint, Flex/Grid, AdvancedForms off) | 18/18 passed |
| Full installed table consumer | 1/1 passed |
| Cropped installed table consumer | 1/1 passed |
| UBSan (`-fsanitize=undefined -fsanitize-trap=all`) | 19/19 passed |
| ASan (`-fsanitize=address -fno-omit-frame-pointer`) | 19/19 passed |

ASan executables initially could not start because the Clang Windows runtime DLL
was absent (`0xc0000135`). Copying
`clang_rt.asan_dynamic-x86_64.dll` from the matching LLVM 22 runtime directory
beside each test executable allowed the suite to run; no sanitizer report was
produced. `git -c core.whitespace=cr-at-eol diff --check` passes. Plain
`git diff --check` reports the repository's CRLF line endings on added lines as
trailing whitespace; those are line-ending markers, not trailing spaces.

The fresh full and cropped packages both report source hash
`cc4b1bee69cf809568769d2439f1c5fdb9bf81a786f79ee98e004c98bea4b075`, package
version `0.9.0-dev`, and ABI 4. Profiles are `render-core-default` and
`render-core-minimal-no-forms-advanced` respectively. Installed consumers
require and verify the exact source hash, ABI, package/profile metadata, and
profile JSON before linking. The table consumer checks geometry and paints via
the public typed `DisplayCommand` API.

No browser pixel-parity, font-metric parity, retained-render qualification,
target-device/resource qualification, commit, or push is claimed.
