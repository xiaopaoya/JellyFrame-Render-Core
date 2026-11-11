# Cancellable Flex solver prerequisite

Base: `b951751a`, branch `codex/flex-length-budget-prerequisite`.

The internal checked Flex length solver now returns `ResourceLimit` when its
charge callback refuses a scan. Validation, initial sums, target initialization,
free-space scans, factor sums, distribution and freezing all request a charge
before visiting the range. Existing ordinary precise Flex layout supplies an
always-continue charge, preserving its previous behavior.

Cancellation is cooperative at scan boundaries. Previously completed scans may
have changed private solver records. A caller must discard the entire refused
plan before publishing geometry; the solver itself does not roll back records.
The callback owns cumulative accounting and must refuse before its ledger cap
is exceeded. This prerequisite does not enforce a layout-pass budget or add
wrapped-row Flex support.

Independent review found no confirmed correctness defect. Existing dedicated
builds under `E:/Builds/flex-length-budget-{debug,release,cropped}` passed
24/24, 24/24 and 23/23 tests respectively. Regression coverage cancels at
every scan boundary, checks that refusal causes no subsequent mutation,
compares exact-budget with unlimited results, and rejects a one-unit-short
budget. `git diff --check` passes.

The wrapped-row follow-up must charge admission, ordering, intrinsic queries,
line formation and each line solver against a common ledger and account for
live plan records plus query scratch. Initial planning limits should derive
from the existing intrinsic-query defaults (256 boxes, depth32, source8192B,
512 measurements, work65536, scratch32768B), with at most128 in-flow items.
These are proposed planner limits, not measured MCU timing/stack guarantees.
Whole-container legacy fallback after cancellation needs dedicated integration
coverage before the planner is admitted.
