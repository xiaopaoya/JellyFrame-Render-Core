# Private shaping-resource reader stage, 2026-11-16

Status is an implemented private admission reader and test format proposal. No
installed ABI is added. The library builds the reader, while TextSystem has no
shaping consumer yet. JFSHR2 remains subject to review before resource production.

## Ownership and identity

The caller supplies independently expected source SHA-256, named profile SHA-256
and original font maxp glyph count. Success binds these exact header identities.
The profile descriptor and its digest are recorded in the fixture manifest. A
companion raster catalog must independently bind the same source/profile and
prove every admitted input and reachable substitution GID has metrics and bitmap
coverage. The font glyph-count ceiling is distinct from populated catalog count.
A passing reader alone establishes structural admission, with no raster coverage
or font-source equivalence proof. CRC32 detects accidental payload corruption.
Header integrity relies on exact schema checks and independently expected identity,
including glyph count. SHA fields carry identity assertions, with no authenticity
claim. Generation must verify source bytes against their pinned hash.

The caller retains immutable backing bytes through every view use. View is a
private value type and can be forged by C++ callers. A future consuming resource
binding must validate bytes and companion linkage before publication and must
retain the exact resource bytes/catalog through cached results and frame replay.
No existing renderer currently consumes this view.

## JFSHR2 proposal

All integers are unsigned little-endian unless identified as signed. Reads are
bytewise and permit unaligned storage. Header is exactly 112 bytes.

| Offset | Width | Field |
| --- | --- | --- |
| 0 | 8 | `JFSHR2` followed by two zero bytes |
| 8 | 2 | Version 2 |
| 10 | 2 | Header size 112 |
| 12 | 4 | Exact resource byte count |
| 16 | 32 | Original source SHA-256 |
| 48 | 32 | Named profile SHA-256 |
| 80 | 4 | Selection count, exactly 5 |
| 84 | 4 | Reference count |
| 88 | 4 | Unique lookup count |
| 92 | 4 | Original source glyph-count ceiling, at most 65536 |
| 96 | 4 | IEEE CRC32 of bytes from offset 112 to end |
| 100 | 12 | Three reserved zero words |

Immediately follow selection records of 16 bytes, references of 8 bytes, lookup
records of 16 bytes, then contiguous lookup payloads. Directory locations are
derived from counts with 64-bit arithmetic. Gaps, overlaps, aliases, trailing
bytes and partially consumed payloads refuse.

Selections contain script[4], language[4], first-reference u16, count u16,
reserved-zero u32. The exact ordered tuples are `latn/    `, `latn/ZHS `,
`hani/    `, `hani/ZHS `, `bopo/ZHT `. Spaces identify DefaultLangSys.
An eventual request adapter must admit only the exact profile mapping
en to default, zh-cn to ZHS and zh-tw to ZHT for the listed script tuples.
Unknown languages and scripts require explicit refusal. No prefix matching or
implicit script fallback is specified. Selections partition all references in
order, including zero-length selections if a future fixture has no feature plan.

References contain feature[4], lookup-index u16 and reserved-zero u16. GSUB
references precede GPOS. Feature and lookup order within each phase is retained
exactly from the source selection plan. Allowed GSUB features are ccmp, locl,
liga. GPOS features are kern and mark. The narrow profile requires each lookup
index occur at most once in a selection. All current pinned plans satisfy this
restriction. Fonts with cross-feature repeated lookup execution refuse this
version pending an explicit execution rule. Distinct lookups under the same
feature remain ordered. Storage deduplication does not determine execution order.

Lookup records contain table u8 (1 GSUB, 2 GPOS), lookup-type u8,
subtable-count u16, original lookup-ID u16, flags u16, payload-offset u32,
payload-length u32. Flags must be zero. Duplicate table/original-ID records
refuse. Subtables retain source order. Each starts with lookup-type u8,
format u8 and body-length u32. Nested extension/contextual lookup framing is
unsupported and refuses.

The five body forms use the spike scalar/value encoding, now fully traversed
before success. Every GID must be below the independently expected glyph ceiling.

- GSUB single type 1, format 1 has row-count u32 then source/target u16 pairs.
  Source GIDs are strictly increasing.
- GSUB ligature type 4, format 4 has row-count u32 then component-count u8,
  output-GID u16 and component GIDs u16. Component count is at least 2.
  First component GIDs are nondecreasing. Original row order within a first-GID
  group supplies matching precedence, including shared prefixes.
- GPOS pair type 2, format 1 has row-count u32 then first/second GID u16 and
  two value records. Pair keys are strictly increasing.
- GPOS pair type 2, format 2 has coverage-count u16 and strictly increasing
  coverage GIDs, two nonzero class counts u16, then for each side assignment-count
  u16 and strictly increasing GID/class u16 pairs. Class IDs are in range.
  Missing assignments mean class zero. The dense class1-by-class2 matrix holds
  two value records per cell in row-major order.
- GPOS mark/base type 4, format 1 has nonzero class-count u16, mark-count u16,
  mark GID/class u16 and mandatory anchor per mark, then base-count u16 and base
  GID u16 plus one optional anchor for each class. Mark/base GIDs are strictly
  increasing within their own arrays. Mark classes are in range.

Value records have a four-bit presence mask u8 followed by signed i16 values
for present X placement, Y placement, X advance and Y advance components in that
order. Unknown mask bits refuse. Anchors have presence u8, restricted to 0 or 1,
followed by signed i16 X/Y when present. Device/contour anchors are unsupported.
ccmp permits single/ligature, locl single, liga ligature, kern pair and mark
mark/base. Unknown feature/table/type/format combinations refuse.

## Costs and transactional behavior

The reader allocates nothing. Limits default to 256 KiB bytes, 32 selections,
256 references, 64 lookups, 256 subtables and 131072 cumulative semantic records.
Records charge each row, coverage GID, sparse class assignment and dense class
pair/anchor slot. Ligature components and scalar fields additionally consume work.
The caller passes a cumulative remaining-work ledger by reference. Validation
charges inspected header bytes, every CRC payload byte, integer reads and
selection/reference/directory scans. Each CRC-byte unit has a fixed eight-bit
update cost. Failed operations keep their charges. Insufficient charge refuses
before traversal. Callers choose the ledger allowance appropriate to their target.
The tests use 4194304 work units. No MCU latency acceptance is implied.

Only the final successful assignment changes the caller's output view. Every
failure preserves the old view. Resource bytes and identity inputs are read-only.
Future binding must combine this preflight with catalog admission transactionally.

## Fixtures and validation

`tools/reframe_shaping_reader_fixture.py` wraps the actual pinned three-face
JFSHAP1 encoded evidence, preserving the selected references and subtable bytes.
It inserts explicit zero lookup flags and updates offsets, framing and identity.
It uses original glyph counts/source hashes from the GID evidence report.
This adapter remains separate from the historical spike encoder. Fixture hashes,
source hashes, profile descriptor and input-spike hashes are in
`tests/fixtures/shaping_resource/manifest.json`.

The normal CMake library builds the private source. CTest registers
jellyframe_shaping_resource_tests before the Windows test diagnostics bootstrap.
Tests accept all three true fixtures, unaligned storage, allocation-forbidden validation and exact counts. They
reject every truncated prefix, every single-byte header mutation, semantic tags,
unknown lookup flags/types/formats, oversized offsets/lengths/counts, source/pair/coverage/class duplicates and order violations, out-of-range GIDs/classes, missing/invalid anchors, unknown value masks, duplicate selected references/original lookup IDs, phase inversion, feature/type mismatch, trailing
bytes and exhausted cumulative work/record/subtable allowances. Failure checks
preserve a sentinel output. Malformed payload mutations recompute the CRC so
semantic validation is exercised beyond checksum rejection. Deep mutations traverse every subtable of every lookup, including PairPos format 2 after PairPos format 1 in the same lookup. Each face must assert coverage of all five supported forms, preventing a missing fixture form from silently skipping its negative tests.

Focused Debug CMake build and CTest passed on 2026-11-16 using Clang 22.1.8.
Standalone -Wall -Wextra -Werror compilation and AddressSanitizer execution of
the same fixture/malformed tests passed. Builds use the user-authorized D:
workspace because E: has no free capacity. The implementation was handed off without a commit for parent review.

## Remaining integration

A production generator must bind the complete admitted Unicode alphabet,
normalization policy and GSUB reachable GID closure to an exact raster catalog.
Normalization can expose scalars absent from the font, requiring explicit
unmapped-input refusal. A catalog count alone does not establish membership.
Reader success does not implement lookup execution, normalized input admission,
cluster construction, resource registration, TextSystem shaping, shaped packet
encoding, layout integration, transport or device qualification. All of these
remain separate work and current scalar behavior continues through its existing
entry points.

## Exact focused verification commands

The following commands completed successfully. TEMP and TMP were set to
`D:/CodexTemp/Temp` before build commands. E:/Start-Development.ps1 was sourced.

```powershell
cmake -S D:/CodexTemp/Core-shaping-resource-20261116 -B D:/CodexTemp/Core-shaping-resource-20261116-build -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DJELLYFRAME_BUILD_TESTS=ON
cmake --build D:/CodexTemp/Core-shaping-resource-20261116-build --target jellyframe_shaping_resource_tests --parallel 6
ctest --test-dir D:/CodexTemp/Core-shaping-resource-20261116-build -R '^jellyframe_shaping_resource_tests$' --output-on-failure
& 'C:/Program Files/LLVM/bin/clang++.exe' -std=c++17 -Wall -Wextra -Werror -fsanitize=address -g D:/CodexTemp/Core-shaping-resource-20261116/src/shaping_resource.cpp D:/CodexTemp/Core-shaping-resource-20261116/tests/unit/shaping_resource_tests.cpp -o D:/CodexTemp/Temp/shaping_resource_asan.exe
& D:/CodexTemp/Temp/shaping_resource_asan.exe D:/CodexTemp/Core-shaping-resource-20261116/tests/fixtures/shaping_resource
```

Latest focused CTest result was 1/1 passed in 0.45 seconds after all-subtable deep mutation traversal and five-form coverage assertions were added. ASan execution exited
zero with no diagnostics. `git diff --check` passed. Full-library regression,
minimal-profile and source-package qualification remain for parent validation.

## Independent integration verification

A fresh Clang Debug full-profile build at `D:/CodexTemp/Core-shaping-resource-20261116-review-build` passed CTest 34/34, including the existing text/layout/paint suites and private-header/source-refresh checks. A separate four-optional-family-disabled build at `D:/CodexTemp/Core-shaping-resource-20261116-cropped-build` compiled the library and passed the focused reader CTest 1/1. These checks qualify the independent reader source and existing integration behavior. Shaping execution and MCU admission remain pending.
