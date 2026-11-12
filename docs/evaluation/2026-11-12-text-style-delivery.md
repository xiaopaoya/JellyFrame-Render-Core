# Text style delivery checkpoint (2026-11-12)

Base: Core master 70e076f1. Adaptation: 238bbf35 cherry-picked as 1d808da0.
Text StyleResolver overloads return default Text style before context preparation,
custom-property ancestor walking and rule/cascade processing. RenderTree retains
inherited style and explicit override ownership. Elements retain existing paths.
Test-only instrumentation follows existing private JELLYFRAME_RENDER_CORE_TEST_HOOKS
configuration; thread-local counters record fast-path calls and actual candidate-rule
lookup attempts without cross-thread writes. Both direct Text overloads make zero lookup attempts; both Element
control overloads exercise lookups. Inheritance/custom-property isolation and
explicit override regression cases pass.

Validation: MSVC 19.41 x64 Release jellyframe_render_core_tests passes in full
and cropped profiles (Canvas2D, modern paint, flex/grid, advanced forms OFF).
Targeted build only; the other CTest executable targets were not built/run.
Raw build/test logs: E:/Experiments/text-style-delivery-20261112.

Current paired desktop measurement uses separate tests-OFF Release executables
against baseline 70e076f1 and candidate, same MSVC/compiler configuration. Harness
and raw paired.json are in that evidence directory. Each process parses once,
warms 20 RenderTree builds, times 200 builds; 20 alternating AB/BA pairs per
fixture. Text fixture: 200 paragraphs, 600 Text nodes, inherited/custom-property
CSS and selectors. Mean elapsed baseline 243.807 ms, candidate 200.039 ms;
mean paired reduction 17.939%, sample SD 1.434 percentage points, paired t 95%
interval [17.267%,18.610%]. All-element control: mean baseline 180.439 ms,
candidate 179.166 ms; mean paired reduction 0.662%, SD 2.737 points, interval
[-0.619%,1.944%], consistent with no change. This synthetic desktop fixture
establishes current benefit under text-rich selector/inheritance work, not a
whole-application or MCU latency guarantee. No hardware/board writes, push or PR.

Final counter cleanup removes inferred skipped-work totals. Incremental full and
cropped Release core suites pass again; tests-OFF timing binaries are unaffected.
