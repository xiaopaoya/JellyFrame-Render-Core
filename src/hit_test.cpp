#include "render_core/hit_test.h"

#include "render_core/dom.h"
#include "render_core/raster_primitives.h"
#include "layout_clip_geometry.h"

#include <algorithm>
#include <cstdint>

namespace jellyframe {
namespace {

bool contains(Rect rect, int x, int y) {
    const std::int64_t right = static_cast<std::int64_t>(rect.x) + static_cast<std::int64_t>(rect.width);
    const std::int64_t bottom = static_cast<std::int64_t>(rect.y) + static_cast<std::int64_t>(rect.height);
    return x >= rect.x && y >= rect.y &&
        static_cast<std::int64_t>(x) < right &&
        static_cast<std::int64_t>(y) < bottom;
}

Rect intersect_rect(Rect left, Rect right) {
    const int x1 = std::max(left.x, right.x);
    const int y1 = std::max(left.y, right.y);
    const int x2 = std::min(safe_edge(left.x, left.width), safe_edge(right.x, right.width));
    const int y2 = std::min(safe_edge(left.y, left.height), safe_edge(right.y, right.height));
    if (x2 <= x1 || y2 <= y1) {
        return Rect{x1, y1, 0, 0};
    }
    return Rect{x1, y1, safe_span(x1, x2), safe_span(y1, y2)};
}

bool empty_rect(Rect rect) {
    return rect.width <= 0 || rect.height <= 0;
}

bool owned_by_child_layer(const LayerNode* owner, const LayoutBox& box) {
    if (owner == nullptr) return false;
    for (const auto& child : owner->children) {
        if (child != nullptr && child->box == &box &&
            (child->reasons & LayerReasonTransientOverlay) == 0U) return true;
    }
    return false;
}

bool outside_overflow_clip(const LayoutBox& box, int x, int y) {
    if (!detail::has_overflow_clip(box.style)) return false;
    if (!contains(box.rect, x, y)) return true;
    const int radius = detail::resolved_border_radius(box);
    const int radius_y = detail::resolved_border_radius_y(box);
    return (has_corner_radius(radius) || has_corner_radius(radius_y)) &&
        rounded_rect_coverage(box.rect, radius, radius_y, x, y) == 0;
}

const Node* event_target_for(const Node* node) {
    if (node == nullptr) {
        return nullptr;
    }
    if (node->type == NodeType::Element) {
        return node;
    }
    for (const Node* ancestor = node->parent; ancestor != nullptr; ancestor = ancestor->parent) {
        if (ancestor->type == NodeType::Element) {
            return ancestor;
        }
    }
    return node;
}

HitTestResult make_result(const LayoutBox& box, int x, int y) {
    HitTestResult result;
    result.box = &box;
    result.node = event_target_for(box.node);
    result.local_x = safe_add(x, safe_negate(box.rect.x));
    result.local_y = safe_add(y, safe_negate(box.rect.y));
    return result;
}

} // namespace

HitTestResult HitTester::hit_test(const LayoutBox& root, int x, int y) const {
    return hit_test_box(root, x, y);
}

HitTestResult HitTester::hit_test(const LayerNode& root, int x, int y) const {
    return hit_test_layer(root, x, y, Rect{}, false, 0, 0);
}

HitTestResult HitTester::hit_test_box(const LayoutBox& box, int x, int y,
                                    const LayerNode* owner) const {
    // A separate layer has already handled its subtree with its own clip,
    // transform and scroll mapping. Do not resurrect a rejected subtree through
    // its ancestor's untransformed layout fallback.
    if (owned_by_child_layer(owner, box) ||
        (!(owner != nullptr && owner->box == &box) && outside_overflow_clip(box, x, y))) {
        return {};
    }

    for (auto it = box.children.rbegin(); it != box.children.rend(); ++it) {
        HitTestResult child_result = hit_test_box(**it, x, y, owner);
        if (child_result) {
            return child_result;
        }
    }

    if (box.node == nullptr || !contains(box.rect, x, y)) {
        return {};
    }
    if (box.style.visibility_hidden) {
        return {};
    }
    return make_result(box, x, y);
}

HitTestResult HitTester::hit_test_layer(const LayerNode& layer,
                                        int x,
                                        int y,
                                        Rect clip,
                                        bool has_clip,
                                        int offset_x,
                                        int offset_y) const {
    const int transform_x = round_float_to_int(layer.transform.translate_x);
    const int transform_y = round_float_to_int(layer.transform.translate_y);
    const int layer_offset_x = safe_add(offset_x, transform_x);
    const int layer_offset_y = safe_add(offset_y, transform_y);
    if (layer.has_clip) {
        Rect translated_clip = layer.clip_rect;
        translated_clip.x = safe_add(translated_clip.x, layer_offset_x);
        translated_clip.y = safe_add(translated_clip.y, layer_offset_y);
        if (has_corner_radius(layer.clip_border_radius) &&
            rounded_rect_coverage(translated_clip, layer.clip_border_radius,
                                  effective_vertical_radius(layer.clip_border_radius,
                                                            layer.clip_border_radius_y),
                                  x, y) == 0) {
            return {};
        }
        clip = has_clip ? intersect_rect(clip, translated_clip) : translated_clip;
        has_clip = true;
        if (empty_rect(clip) || !contains(clip, x, y)) {
            return {};
        }
    } else if (has_clip && !contains(clip, x, y)) {
        return {};
    }

    for (auto it = layer.children.rbegin(); it != layer.children.rend(); ++it) {
        HitTestResult child_result = hit_test_layer(**it, x, y, clip, has_clip, layer_offset_x, layer_offset_y);
        if (child_result) {
            return child_result;
        }
    }

    Rect visual_bounds = layer.bounds;
    visual_bounds.x = safe_add(visual_bounds.x, layer_offset_x);
    visual_bounds.y = safe_add(visual_bounds.y, layer_offset_y);
    if (layer.box == nullptr) {
        return {};
    }
    const int document_x = safe_add(x, safe_negate(layer_offset_x));
    const int document_y = safe_add(safe_add(y, safe_negate(layer_offset_y)), layer.scroll_y);
    // Popup paint shares the control's box but owns only its separate bounds.
    // Walking that box here would reuse unscrolled normal-flow coordinates.
    if ((layer.reasons & LayerReasonTransientOverlay) != 0U) {
        return contains(visual_bounds, x, y) && !layer.box->style.visibility_hidden
            ? make_result(*layer.box, document_x, document_y) : HitTestResult{};
    }
    if (layer.scroll_y > 0) {
        for (auto it = layer.box->children.rbegin(); it != layer.box->children.rend(); ++it) {
            HitTestResult child_result = hit_test_box(**it, document_x, document_y, &layer);
            if (child_result) {
                return child_result;
            }
        }
        if (contains(visual_bounds, x, y) && !layer.box->style.visibility_hidden)
            return make_result(*layer.box, document_x, safe_add(document_y, safe_negate(layer.scroll_y)));
        return {};
    }
    HitTestResult result = hit_test_box(*layer.box, document_x, document_y, &layer);
    if (result) {
        return result;
    }
    // No descendant hit: retain the visible layer owner as the final target.
    if (contains(visual_bounds, x, y) && !layer.box->style.visibility_hidden)
        return make_result(*layer.box, document_x, document_y);
    return {};
}

} // namespace jellyframe
