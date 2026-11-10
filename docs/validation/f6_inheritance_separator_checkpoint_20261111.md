# F6 inheritance and separator checkpoint

Acquired 2026-11-11 against Core mainline `57c71fd4a056fbfc7ba903294e96b35178d264e9`
(C5 intrinsic query PR22). Candidate `354d763b8722ea970562e54051d0f85cdbaea0f4`
contains only the F6 changes in `src/render_tree.cpp` and
`tests/unit/render_tree_tests.cpp`. The candidate source manifest is
`ed408be1f46cc4e00774e6f2d71ea36524401c80f970c852b86080b168117f5e`
(145 inputs); baseline is `b0fdf5be500dd56b3032c4ee53ccdc50631708960b000cb9952014102f415bf0`
(145 inputs).

## Change

Inherited Style fields are initialized directly in the destination Style,
avoiding a temporary copied Style for each inherited field group. The layout
tree builder also admits separator-only nodes to an early discard path when no
override style is active. Nonempty override lists retain the existing path.
The change preserves the existing output semantics and does not alter
`sizeof(Style)` (920 bytes in the measured host builds).

## Paired host evidence

The frozen harness and workload design from
`E:/Experiments/f6-inherit-separator-current-base-20261110` were copied to
`E:/Experiments/f6-inherit-separator-after-c5-20261110`; only executable and
manifest paths were updated. Clang 22.1.8, Ninja Release `-O3 -DNDEBUG`, eight
randomized process pairs per workload, 80 measured frames per process after
five warmups, seed 20261110, logical CPU 4. All five 80-frame binary
projections and all 40 timed-pair checksums match. The projection covers
preorder layout geometry and selected inheritance fields, typed command
semantics, clips, full RGBA pixels, hit targets/local coordinates and
diagnostics; it does not serialize every private Style or prepared-glyph field.

| Workload | Base mean us | Candidate mean us | Paired saving us (95% CI) | Exact p | Mean reduction |
| --- | ---: | ---: | ---: | ---: | ---: |
| Cold | 861.989 | 786.409 | 75.580 [60.127, 91.033] | 0.0078125 | 8.77% |
| Warm | 503.512 | 430.450 | 73.061 [46.092, 100.031] | 0.0078125 | 14.51% |
| Sparse | 494.615 | 433.207 | 61.408 [53.431, 69.385] | 0.0078125 | 12.42% |
| Dense | 482.410 | 416.226 | 66.184 [47.181, 85.186] | 0.0078125 | 13.72% |
| Unsupported override fallback | 514.735 | 469.439 | 45.296 [29.539, 61.053] | 0.0078125 | 8.80% |

For cold, warm, sparse and dense workloads, allocations fall by 400/frame and
requested payload bytes by 28,800/frame. Unsupported fallback work is
unchanged at 3,094 allocations and 732,329 requested bytes/frame. Peak
additional live requested bytes and retained live delta are identical per
workload. The harness does not separately measure container capacity; payload
counts exclude allocator metadata, internal C allocations, OS memory and
stack. This is host-fixture evidence, not an MCU timing or universal latency
claim. Raw inputs, binaries, manifests, pair observations, summaries and
projection hashes are retained under
`E:/Experiments/f6-inherit-separator-after-c5-20261110`.

## Regression gates

Fresh build trees under `E:/Builds/f6-inherit-separator-c5-*`, Clang 22.1.8:

| Gate | Result |
| --- | ---: |
| Full Release | 24/24 |
| Cropped Release | 23/23 |
| AddressSanitizer | 24/24 |
| UndefinedBehaviorSanitizer trap mode | 24/24 |

The first ASan invocation lacked the LLVM runtime DLL on `PATH` and reported
Windows loader status `0xc0000135`; rerunning with the development environment
loaded passed all 24 tests. `git diff --check` passes. Device timing and
resource qualification remain open.
