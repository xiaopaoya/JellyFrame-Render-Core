# Bounded wrapped-row Flex checkpoint (2026-11-11)

Local candidate: `codex/c5-wrapped-row-flex`, based on `a60166eb`.
Production source manifest: `ba819bd2077207f0763137f392eecc3122c6431b1809af2f49f65625b06ecfde` / 145 files, ABI4.
This checkpoint does not qualify general wrapped Flex or device deployment.

## Admitted behavior

Ordinary horizontal row/wrap, automatic container height, start justification
and start line packing. Items are static empty Blocks or Blocks containing one
direct Core Text run. Intrinsic source widths determine hypothetical outer
sizes and line breaks; each line runs the cancellable precision solver, then
queries height at its selected width. Grow, shrink, min/max, decorations,
nonnegative margins, order, gaps and start/center/end/stretch cross alignment
are covered. Fractional Q1000 origins and line thresholds remain precise.
Selected fractional text inline widths are refused because paragraph publication
currently uses integer inline widths.

Unsupported content or any planning/query/solver/resource refusal falls back
for the whole container. Planning does not publish geometry. A mutation journal
captures geometry, precise content geometry and caches before child publication;
late cancellation or publication mismatch rolls all of them back before fallback.
The journal uses raw Slot storage with counted construction/destruction, avoiding
an unaccounted array destructor cookie. Exact-cap scratch and allocation failure
regressions exercise this storage. Review corrected allocation hooks in both test
executables to include array new/delete; ASan had otherwise bypassed the failure
counter for the traversal array.

## Accounting boundary

The shared public-layout-pass ledger charges planner scans, ordering, solver
iterations, source queries and journal capture. Defaults are 256 source boxes,
32 query depth, 8192 source bytes, 512 measurements, 65536 work and 32768 bytes
of simultaneous query/planner payload. Planner admission also caps 128 items;
the aggregate defaults can refuse earlier. Exact item/line arrays, traversal
pointers and journal Slots are reserved against the same scratch peak. Allocator
metadata, persistent text cache allocations, ordinary layout/paint work and
fallback work are outside this payload/work accounting. Cancellation is checked
at charged boundaries, not inside every ordinary text publication operation.

## Validation and provenance

- Existing fresh full Release, Debug and UBSan runs passed 25/25; cropped Release
  passed 24/24. Those logs postdated production source edits. The final wrapped
  test edit postdated those runs, so both wrapped/journal targets were rebuilt
  and passed 2/2 in each profile during independent final review.
- Existing ASan run passed 24/25 with the journal allocation-hook assertion as
  its sole failure. After correcting the hooks, both affected targets rebuilt
  and passed 2/2, including mid-publication cancellation and late-work rollback.
  No production source changed during this final review.
- Builds: `E:/Builds/c5-wrapped-row-flex-{release,debug,cropped,ubsan,asan}`.
  CTest temporary logs contain the latest focused results.
- Exact installed full/cropped ABI4 consumers passed 1/1 each at the manifest
  above. Evidence: `E:/Builds/c5-wrapped-row-flex-package-{full,cropped}` and
  `E:/Builds/c5-wrapped-row-flex-install-{full,cropped}`. Full profile is
  `render-core-default`; cropped behavior retains ordinary fallback.
- Fresh localhost Chrome and Edge semantic observations passed 7/7 each:
  `E:/Temp/c5-wrapped-row-oracle-20261111/results.json`. These are geometry
  observations with the supplied font, distinct from Core regression evidence.
- Xtensa ESP32-S3 optimized individual-frame audit:
  `E:/Builds/2026-11-11-wrapped-layout-stack-audit/stack-usage.json`.
  All four recorded translation-unit SHA256 values match current source.
  Wrapped planner 448 B, precise Flex 464 B, public build 384 B (old 224 B),
  source width/height queries 1104/1120 B. These are compiler frames only;
  whole call-chain, recursion and device stack high-water marks remain open.

Regression cases include exact/one-Q1000-below line thresholds, two/three rows,
independent line grow, max-width freezing, single oversized-item shrink and
automatic minimum overflow, order, cross alignment, decoration/margins,
fractional containing origin, unsupported fourth sibling, aggregate source-byte
exhaustion, exact/one-short work and scratch caps, cancellation before and
during publication, depth refusal, 32-item admission and 129-item refusal.

PR/CI, target archive/link deltas, whole-call-chain stack, heap/cache peaks,
device runtime/UI and product reserve qualification remain open. No merge,
push, release, flash or Runtime lock/default change is part of this checkpoint.
