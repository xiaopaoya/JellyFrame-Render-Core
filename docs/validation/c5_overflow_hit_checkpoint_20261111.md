# C5 overflow hit traversal checkpoint

This local candidate extends joined Core `c941f69a`. Production source SHA-256
is `5a4b527c18a0385e50fda501ca4c702be74e3b689c3e51d3a35d12496b063d19`
over146 inputs, Core0.9.0-dev/ABI4. It is not a mainline delivery or device
qualification. The earlier joined firmware and package evidence retain their
original source scope.

## Behavior

Layout hit traversal now searches visible-overflow descendants outside their
ancestor rectangles, including zero-height ancestors. The ancestor itself is
returned only inside its own rectangle. Hidden/clip/scroll/auto overflow keeps
rectangular and rounded clipping; the radius resolution helpers are shared with
layer painting without changing their bodies.

Layer fallback excludes subtrees already handled by a direct child layer, so a
rejected transformed or clipped child cannot reappear at untransformed layout
coordinates. Rounded rejection no longer suppresses unrelated lower siblings.
Native popup overlays are auxiliary bounds-only targets; they share a select's
LayoutBox without exclusively owning its ordinary layout subtree. An actual
scrolled open-select fixture checks popup hits, original-control coordinates,
and the underlying sibling at the select's stale unscrolled position.

## Verification

- `E:/Builds/c5-overflow-hit-release-20261111`: complete Clang Release25/25.
- `E:/Builds/c5-overflow-hit-cropped-20261111`: complete cropped Release24/24.
  Optional features match the earlier joined cropped profile.
- `E:/Builds/c5-overflow-hit-asan-20261111`: rebuilt Clang AddressSanitizer
  monolithic Core suite1/1, including hit, input, layer, popup and Event groups.
  `ASAN_OPTIONS=detect_leaks=0`; no leak qualification.
- New regressions cover both LayoutBox/LayerNode entry points where applicable:
  visible overflow, all four clip modes, scalar/percentage/elliptical rounded
  corners, translated layers, and actual scrolled select popups. The popup
  regression runs with advanced forms enabled in the full profile.
- Independent review verified the four extracted geometry helper bodies are
  identical to the predecessor. Private-header source hashing includes the new
  header. `git diff --check` passed.

No hit-helper allocation or persistent index is added. An overflow miss can
visit the complete layout subtree; matching ordinary boxes against direct child
layers is worst-case O(boxes * direct child layers). This is functional evidence,
not a touch-latency improvement claim. Hit testing only from LayoutBox still
uses layout coordinates; layer transforms/scroll mapping require the layer
entry point. Existing layer-budget folding can lose scroll/clip/stack fidelity;
this change does not establish parity after such folding. No browser or device
observation is claimed for this source.
