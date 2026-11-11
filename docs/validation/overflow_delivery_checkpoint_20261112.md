# Overflow delivery checkpoint, 2026-11-12

## Source identity

This delivery starts at origin/master f7aee11cfd6c58b89ecc4922a598a2fada97c48b and applies only candidate 272ba1d67567c64080fb27847d797472138c257a as 89d6bbc8. Branch is codex/overflow-delivery-20261112. Production source SHA-256 is 98f89f0fd300e631e701b301023e1510026145efc50eb3c76845843eb1c34086 across 146 inputs, Core 0.9.0-dev / ABI4. The earlier 2026-11-11 checkpoint retains its original candidate identity.

## Dependency review

Hit testing and layer construction include the same private layout_clip_geometry.h. Its four extracted geometry function bodies are byte-identical to this delivery base. The base already supplies elliptical radius geometry, transient-overlay layer reasons, scroll mapping and advanced-form popup fixtures. The source-manifest regression includes private headers. The cherry-pick applied cleanly. No additional production changes were needed.

## Fresh verification

Clang 22.1.8, Ninja and Release builds used canonical E paths. Full CTest passed 25/25 in E:/Builds/core-overflow-delivery-full-20261112. Cropped CTest passed 24/24 in E:/Builds/core-overflow-delivery-cropped-20261112 with Canvas2D, modern paint, flex/grid, advanced forms and image file I/O disabled. Full-feature RelWithDebInfo AddressSanitizer monolithic Core regression passed 1/1 in E:/Builds/core-overflow-delivery-asan-20261112 with -fsanitize=address -fno-omit-frame-pointer and ASAN_OPTIONS=detect_leaks=0. git diff --check passed.

Existing regressions cover LayoutBox and LayerNode overflow traversal, zero-height ancestors, hidden/clip/scroll/auto clipping, scalar/percentage/elliptical rounded clipping, transformed child rejection and lower-sibling hits. The actual scrolled open-select popup regression runs in the full profile. The monolithic suite also covers input, layer, popup and event behavior.

## Boundaries

This checkpoint qualifies host regression behavior. Browser, device, firmware and touch-latency qualification remain outside this evidence. LayoutBox entry points use layout coordinates. Transforms and scroll mapping require LayerNode entry points. An overflow miss can visit a whole subtree, and ownership checks are worst-case O(boxes * direct child layers). Existing layer-budget folding fidelity remains unchanged. AddressSanitizer leak detection was disabled.

## Changed files relative to base

- docs/validation/c5_overflow_hit_checkpoint_20261111.md
- docs/validation/overflow_delivery_checkpoint_20261112.md
- include/render_core/hit_test.h
- src/hit_test.cpp
- src/layer_tree.cpp
- src/layout_clip_geometry.h
- tests/unit/hit_test_tests.cpp

Logs and manifests are in E:/Experiments/core-overflow-delivery-20261112.
