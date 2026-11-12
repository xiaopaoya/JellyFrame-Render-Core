#include "shared_inline.h"
#include "render_core/layer_tree.h"

#include "render_core/animation_timeline.h"
#include "render_core/form_control.h"
#include "render_core/feature_config.h"
#include "render_core/flex_grid_paint.h"
#include "render_core/text_normalization.h"
#include "render_core/text_scan.h"
#include "layout_clip_geometry.h"
#include "background_image_geometry.h"
#include "background_gradient_geometry.h"
#include "background_repeat_geometry.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

namespace jellyframe {
namespace {

using detail::has_overflow_clip;
using detail::resolve_border_radii;
using detail::resolved_border_radius;
using detail::resolved_border_radius_y;

constexpr std::size_t kMaxImageResolutionAttemptsPerBuild = 128;
struct ImageResolutionBuildState {
    std::size_t attempts = 0;
    bool limit_reported = false;
};
thread_local ImageResolutionBuildState* active_image_resolution_state = nullptr;


#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
constexpr int kConicGradientAreaWarningPixels = 65536;
constexpr int kRadialGradientAreaWarningPixels = 32768;
constexpr int kBoxShadowAreaWarningPixels = 98304;
#endif

bool has_border(const EdgeSizes& border) {
    return border.top > 0 || border.right > 0 || border.bottom > 0 || border.left > 0;
}

bool is_visible_background(Color color) {
    return color.a != 0;
}

std::uint32_t trace_owner_token_for(const LayoutBox& box, const LayerTreeBuilderOptions& options) {
    if (box.node == nullptr || options.trace_owner_resolver.resolve == nullptr) {
        return 0;
    }
    return options.trace_owner_resolver.resolve(*box.node, options.trace_owner_resolver.context);
}

void stamp_trace_owner(DisplayList& display_list, std::size_t begin, std::uint32_t token) {
    if (token == 0) {
        return;
    }
    begin = std::min(begin, display_list.size());
    for (std::size_t index = begin; index < display_list.size(); ++index) {
        display_list[index].trace_owner_token = token;
    }
}

Rect union_rect(Rect left, Rect right) {
    if (left.width <= 0 || left.height <= 0) {
        return right;
    }
    if (right.width <= 0 || right.height <= 0) {
        return left;
    }
    const int x1 = std::min(left.x, right.x);
    const int y1 = std::min(left.y, right.y);
    const int x2 = std::max(safe_edge(left.x, left.width), safe_edge(right.x, right.width));
    const int y2 = std::max(safe_edge(left.y, left.height), safe_edge(right.y, right.height));
    return Rect{x1, y1, safe_span(x1, x2), safe_span(y1, y2)};
}

Rect paint_rect_for(const LayoutBox& box) {
    if (box.style.display != Display::Inline || box.children.empty()) {
        return box.rect;
    }
    Rect rect{};
    for (const auto& child : box.children) {
        rect = union_rect(rect, child->rect);
    }
    return rect.width > 0 && rect.height > 0 ? rect : box.rect;
}


bool has_scrollable_overflow(const Style& style) {
    return style.overflow == "scroll" || style.overflow == "auto";
}

bool is_positioned(const Style& style) {
    return style.position_type != PositionType::Static;
}

bool has_transform(const Style& style) {
    return !style.transform.empty();
}

Transform2D parsed_transform_or_identity(const Style& style, DiagnosticSink* diagnostics) {
    Transform2D transform;
    if (style.transform.empty()) {
        return transform;
    }
    if (!parse_css_transform_2d(style.transform, transform)) {
        report_diagnostic(diagnostics,
                          DiagnosticStage::LayerTree,
                          DiagnosticSeverity::Warning,
                          "layer-transform-unsupported",
                          "Transform could not be applied by the supported 2D subset",
                          style.transform);
        return Transform2D{};
    }
    return transform;
}

bool has_shadow(const Style& style) {
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return style.box_shadow.enabled;
#else
    (void)style;
    return false;
#endif
}

bool has_text_shadow(const Style& style) {
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return style.text_shadow.enabled;
#else
    (void)style;
    return false;
#endif
}


void push_fill_rect(DisplayList& display_list, Rect rect, Color color, int border_radius = 0,
                    int border_radius_y = -1) {
    if (rect.width <= 0 || rect.height <= 0 || color.a == 0) {
        return;
    }
    DisplayCommand command;
    command.set_type(DisplayCommandType::FillRect);
    command.rect = rect;
    command.color = color;
    command.color2 = color;
    command.basic_paint().border_radius = border_radius;
    command.basic_paint().border_radius_y = border_radius_y;
    display_list.push_back(std::move(command));
}

#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
void push_linear_gradient(DisplayList& display_list,
                          Rect rect,
                          Color first,
                          Color second,
                          GradientAxis axis,
                          int border_radius = 0, int border_radius_y = -1) {
    if (rect.width <= 0 || rect.height <= 0 || (first.a == 0 && second.a == 0)) {
        return;
    }
    DisplayCommand command;
    command.set_type(DisplayCommandType::LinearGradient);
    command.rect = rect;
    command.color = first;
    command.color2 = second;
    command.basic_paint().gradient_axis = axis;
    command.basic_paint().border_radius = border_radius;
    command.basic_paint().border_radius_y = border_radius_y;
    display_list.push_back(std::move(command));
}

void push_conic_gradient(DisplayList& display_list,
                         Rect rect,
                         Color first,
                         Color second,
                         int stop_percent,
                         int border_radius = 0, int border_radius_y = -1) {
    if (rect.width <= 0 || rect.height <= 0 || (first.a == 0 && second.a == 0)) {
        return;
    }
    DisplayCommand command;
    command.set_type(DisplayCommandType::ConicGradient);
    command.rect = rect;
    command.color = first;
    command.color2 = second;
    command.basic_paint().gradient_stop_percent = std::max(0, std::min(100, stop_percent));
    command.basic_paint().border_radius = border_radius;
    command.basic_paint().border_radius_y = border_radius_y;
    display_list.push_back(std::move(command));
}

void push_box_shadow(DisplayList& display_list,
                     Rect rect,
                     Color color,
                     int border_radius,
                     int extent,
                     int blur) {
    if (rect.width <= 0 || rect.height <= 0 || color.a == 0 || extent <= 0) {
        return;
    }
    const std::int64_t extent64 = static_cast<std::int64_t>(extent);
    const std::int64_t twice_extent64 = extent64 * 2;
    const int twice_extent = static_cast<int>(std::clamp(twice_extent64,
                                                         static_cast<std::int64_t>(std::numeric_limits<int>::min()),
                                                         static_cast<std::int64_t>(std::numeric_limits<int>::max())));
    DisplayCommand command;
    command.set_type(DisplayCommandType::BoxShadow);
    command.rect = Rect{
        safe_edge(rect.x, -extent),
        safe_edge(rect.y, -extent),
        safe_edge(rect.width, twice_extent),
        safe_edge(rect.height, twice_extent),
    };
    command.color = color;
    command.color2 = color;
    command.basic_paint().border_radius = expand_corner_radii(border_radius, extent);
    command.basic_paint().stroke_width = extent;
    command.basic_paint().gradient_stop_percent = blur;
    display_list.push_back(std::move(command));
}

void push_radial_gradient(DisplayList& display_list,
                          Rect rect,
                          Color center,
                          Color edge,
                          GradientAxis axis,
                          int packed_position,
                          int border_radius = 0, int border_radius_y = -1) {
    if (rect.width <= 0 || rect.height <= 0 || (center.a == 0 && edge.a == 0)) {
        return;
    }
    DisplayCommand command;
    command.set_type(DisplayCommandType::RadialGradient);
    command.rect = rect;
    command.color = center;
    command.color2 = edge;
    command.basic_paint().gradient_axis = axis;
    command.basic_paint().gradient_stop_percent = packed_position;
    command.basic_paint().border_radius = border_radius;
    command.basic_paint().border_radius_y = border_radius_y;
    display_list.push_back(std::move(command));
}
#endif

void paint_background_paint(const BackgroundPaint& paint,
                            Rect rect,
                            int border_radius,
                            DisplayList& display_list,
                            const LayerTreeBuilderOptions& options,
                            int border_radius_y = -1) {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    (void)options;
#endif
    if (paint.kind == BackgroundPaintKind::LinearGradient) {
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
        push_linear_gradient(display_list, rect, paint.color, paint.color2, paint.axis, border_radius, border_radius_y);
#else
        push_fill_rect(display_list, rect, paint.color, border_radius, border_radius_y);
#endif
    } else if (paint.kind == BackgroundPaintKind::ConicGradient) {
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
        const long long area = static_cast<long long>(std::max(0, rect.width)) * static_cast<long long>(std::max(0, rect.height));
        if (area > kConicGradientAreaWarningPixels) {
            report_diagnostic(options.diagnostics, DiagnosticStage::LayerTree, DiagnosticSeverity::Warning,
                              "layer-conic-gradient-area-budget", "conic-gradient() area is above the embedded progress-ring budget",
                              "area=" + std::to_string(area) + "px limit=" + std::to_string(kConicGradientAreaWarningPixels) + "px");
        }
        push_conic_gradient(display_list, rect, paint.color, paint.color2, paint.stop_percent, border_radius, border_radius_y);
#else
        push_fill_rect(display_list, rect, paint.color, border_radius, border_radius_y);
#endif
    } else if (paint.kind == BackgroundPaintKind::RadialGradient) {
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
        const long long area = static_cast<long long>(std::max(0, rect.width)) * static_cast<long long>(std::max(0, rect.height));
        if (area > kRadialGradientAreaWarningPixels) {
            report_diagnostic(options.diagnostics, DiagnosticStage::LayerTree, DiagnosticSeverity::Warning,
                              "layer-radial-gradient-area-budget", "radial-gradient() area is above the embedded highlight budget",
                              "area=" + std::to_string(area) + "px limit=" + std::to_string(kRadialGradientAreaWarningPixels) + "px");
        }
        push_radial_gradient(display_list, rect, paint.color, paint.color2, paint.axis, paint.stop_percent, border_radius, border_radius_y);
#else
        push_fill_rect(display_list, rect, paint.color, border_radius, border_radius_y);
#endif
    } else if (is_visible_background(paint.color)) {
        push_fill_rect(display_list, rect, paint.color, border_radius, border_radius_y);
    }
}

bool equal_border_widths(const EdgeSizes& border) {
    return border.top == border.right && border.top == border.bottom && border.top == border.left;
}

detail::BackgroundBoxAreasRequest background_box_request(
    const LayoutBox& box, const BackgroundImageStyleValues& style) {
    detail::BackgroundBoxAreasRequest request;
    request.border_box = box.rect;
    request.border = {box.style.border_width.top, box.style.border_width.right,
                       box.style.border_width.bottom, box.style.border_width.left};
    request.padding = {box.style.padding.top, box.style.padding.right,
                        box.style.padding.bottom, box.style.padding.left};
    request.origin = static_cast<detail::BackgroundBoxArea>(style.origin);
    request.clip = static_cast<detail::BackgroundBoxArea>(style.clip);
    request.border_radius = resolved_border_radius(box);
    request.border_radius_y = resolved_border_radius_y(box);
    return request;
}

bool background_color_clip(const LayoutBox& box, BackgroundBox clip,
                           Rect& area, int& radius, int& radius_y) {
    const auto inset = [](BackgroundBox selected, const EdgeSizes& border,
                          const EdgeSizes& padding, int& top, int& right,
                          int& bottom, int& left) {
        top = right = bottom = left = 0;
        if (selected == BackgroundBox::BorderBox) return true;
        if (selected != BackgroundBox::PaddingBox && selected != BackgroundBox::ContentBox)
            return false;
        const auto add = [selected](int border_value, int padding_value) {
            return static_cast<std::int64_t>(border_value) +
                (selected == BackgroundBox::ContentBox ? padding_value : 0);
        };
        const auto t = add(border.top, padding.top);
        const auto r = add(border.right, padding.right);
        const auto b = add(border.bottom, padding.bottom);
        const auto l = add(border.left, padding.left);
        if (std::min({t, r, b, l}) < 0 ||
            std::max({t, r, b, l}) > std::numeric_limits<int>::max()) return false;
        top = static_cast<int>(t);
        right = static_cast<int>(r);
        bottom = static_cast<int>(b);
        left = static_cast<int>(l);
        return true;
    };

    if (clip != BackgroundBox::BorderBox && clip != BackgroundBox::PaddingBox &&
        clip != BackgroundBox::ContentBox) return false;
    int top = 0, right = 0, bottom = 0, left = 0;
    if (!inset(clip, box.style.border_width, box.style.padding, top, right, bottom, left))
        return false;
    const Rect border_box = paint_rect_for(box);
    const auto x = static_cast<std::int64_t>(border_box.x) + left;
    const auto y = static_cast<std::int64_t>(border_box.y) + top;
    const auto width = static_cast<std::int64_t>(border_box.width) - left - right;
    const auto height = static_cast<std::int64_t>(border_box.height) - top - bottom;
    if (width < 0 || height < 0) return false;
    const auto max_int = static_cast<std::int64_t>(std::numeric_limits<int>::max());
    const auto min_int = static_cast<std::int64_t>(std::numeric_limits<int>::min());
    if (x < min_int || x > max_int || y < min_int || y > max_int ||
        width > max_int || height > max_int || x + width > max_int || y + height > max_int)
        return false;
    area = {static_cast<int>(x), static_cast<int>(y),
            static_cast<int>(width), static_cast<int>(height)};
    return width > 0 && height > 0 && detail::inset_background_radii(
        resolved_border_radius(box), resolved_border_radius_y(box), {top, right, bottom, left},
        radius, radius_y);
}

bool paint_background_gradient(const BackgroundPaint& paint, const LayoutBox& box,
                               DisplayList& display_list, const LayerTreeBuilderOptions& options,
                               std::size_t remaining_commands, std::size_t box_command_begin,
                               const BackgroundImageStyleValues* layer_style = nullptr) {
    if (paint.kind != BackgroundPaintKind::LinearGradient &&
        paint.kind != BackgroundPaintKind::ConicGradient &&
        paint.kind != BackgroundPaintKind::RadialGradient) return false;
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    (void)box;
    (void)display_list;
    (void)options;
    (void)remaining_commands;
    (void)box_command_begin;
    (void)layer_style;
    return false;
#else
    const Rect bounds = paint_rect_for(box);
    const long long area = static_cast<long long>(std::max(0, bounds.width)) *
        static_cast<long long>(std::max(0, bounds.height));
    if (paint.kind == BackgroundPaintKind::ConicGradient && area > kConicGradientAreaWarningPixels) {
        report_diagnostic(options.diagnostics, DiagnosticStage::LayerTree, DiagnosticSeverity::Warning,
                          "layer-conic-gradient-area-budget",
                          "conic-gradient() area is above the embedded progress-ring budget",
                          "area=" + std::to_string(area) + "px limit=" +
                              std::to_string(kConicGradientAreaWarningPixels) + "px");
    } else if (paint.kind == BackgroundPaintKind::RadialGradient && area > kRadialGradientAreaWarningPixels) {
        report_diagnostic(options.diagnostics, DiagnosticStage::LayerTree, DiagnosticSeverity::Warning,
                          "layer-radial-gradient-area-budget",
                          "radial-gradient() area is above the embedded highlight budget",
                          "area=" + std::to_string(area) + "px limit=" +
                              std::to_string(kRadialGradientAreaWarningPixels) + "px");
    }
    detail::BackgroundGradientGeometryRequest request;
    request.boxes = background_box_request(box, layer_style ? *layer_style : BackgroundImageStyleValues{});
    if (layer_style != nullptr) {
        request.size_mode = static_cast<detail::BackgroundGradientSizeMode>(layer_style->size);
        request.position_x_percent_q1000 = static_cast<std::int32_t>(layer_style->position_x_percent) * 1000;
        request.position_y_percent_q1000 = static_cast<std::int32_t>(layer_style->position_y_percent) * 1000;
        request.repeat_x = static_cast<detail::BackgroundAxisRepeat>(layer_style->repeat_x);
        request.repeat_y = static_cast<detail::BackgroundAxisRepeat>(layer_style->repeat_y);
    }
    const std::size_t used = display_list.size() - box_command_begin;
    request.max_tiles = used < remaining_commands ? remaining_commands - used : 0;
    detail::BackgroundGradientGeometry geometry;
    const auto status = detail::resolve_background_gradient_geometry(request, geometry);
    if (status == detail::BackgroundGradientGeometryStatus::Empty) return true;
    if (status != detail::BackgroundGradientGeometryStatus::Ready) {
        report_diagnostic(options.diagnostics, DiagnosticStage::LayerTree, DiagnosticSeverity::Warning,
                          status == detail::BackgroundGradientGeometryStatus::TileLimit
                              ? "layer-background-gradient-limit" : "layer-background-gradient-geometry",
                          "Background gradient pattern was refused; the complete gradient layer was omitted.");
        return true;
    }
    for (std::size_t i = 0; i < geometry.sampling_tiles.count; ++i) {
        BackgroundPaintClip clip;
        if (!detail::make_background_paint_clip(geometry.sampling_tiles.tiles[i], geometry.painting_area, clip)) {
            report_diagnostic(options.diagnostics, DiagnosticStage::LayerTree, DiagnosticSeverity::Warning,
                              "layer-background-gradient-geometry",
                              "Background gradient pattern was refused; its shared painting clip cannot be represented.");
            return true;
        }
    }
    const std::size_t begin = display_list.size();
    try {
        display_list.reserve(begin + geometry.sampling_tiles.count);
        for (std::size_t i = 0; i < geometry.sampling_tiles.count; ++i) {
            const Rect tile = geometry.sampling_tiles.tiles[i];
            BackgroundPaintClip clip;
            detail::make_background_paint_clip(tile, geometry.painting_area, clip);
            if (paint.kind == BackgroundPaintKind::LinearGradient) {
                push_linear_gradient(display_list, tile, paint.color, paint.color2, paint.axis,
                                     geometry.painting_radius, geometry.painting_radius_y);
            } else if (paint.kind == BackgroundPaintKind::ConicGradient) {
                push_conic_gradient(display_list, tile, paint.color, paint.color2, paint.stop_percent,
                                    geometry.painting_radius, geometry.painting_radius_y);
            } else {
                push_radial_gradient(display_list, tile, paint.color, paint.color2, paint.axis,
                                     paint.stop_percent, geometry.painting_radius, geometry.painting_radius_y);
            }
            if (display_list.size() > begin + i) {
                display_list.back().basic_paint().background_paint_clip = clip;
            }
        }
    } catch (const std::bad_alloc&) {
        display_list.resize(begin);
        report_diagnostic(options.diagnostics, DiagnosticStage::LayerTree, DiagnosticSeverity::Warning,
                          "layer-background-gradient-limit",
                          "Background gradient pattern was refused because command storage was unavailable.");
    }
    return true;
#endif
}

void push_stroke_rect(DisplayList& display_list, Rect rect, Color color, int stroke_width, int border_radius,
                      int border_radius_y = -1) {
    if (rect.width <= 0 || rect.height <= 0 || stroke_width <= 0 || color.a == 0) {
        return;
    }
    DisplayCommand command;
    command.set_type(DisplayCommandType::StrokeRect);
    command.rect = rect;
    command.color = color;
    command.color2 = color;
    command.basic_paint().stroke_width = stroke_width;
    command.basic_paint().border_radius = border_radius;
    command.basic_paint().border_radius_y = border_radius_y;
    display_list.push_back(std::move(command));
}

void push_border_rects(DisplayList& display_list, Rect rect, const EdgeSizes& border, Color color, int border_radius,
                       int border_radius_y = -1) {
    if (!has_border(border) || rect.width <= 0 || rect.height <= 0 || color.a == 0) {
        return;
    }
    if (has_corner_radius(border_radius) && equal_border_widths(border)) {
        push_stroke_rect(display_list, rect, color, border.top, border_radius, border_radius_y);
        return;
    }
    push_fill_rect(display_list, Rect{rect.x, rect.y, rect.width, border.top}, color);
    push_fill_rect(display_list, Rect{rect.x, safe_edge(rect.y, safe_edge(rect.height, -border.bottom)), rect.width, border.bottom}, color);
    push_fill_rect(display_list, Rect{rect.x, rect.y, border.left, rect.height}, color);
    push_fill_rect(display_list, Rect{safe_edge(rect.x, safe_edge(rect.width, -border.right)), rect.y, border.right, rect.height}, color);
}

void push_text(DisplayList& display_list,
               Rect rect,
               Color color,
               const std::string& text,
               int font_size,
               int font_weight,
               std::uint32_t font_family_hash,
               TextCommandAlign align,
               bool single_line) {
    if (rect.width <= 0 || rect.height <= 0 || text.empty() || color.a == 0) {
        return;
    }
    DisplayCommand command;
    command.set_type(DisplayCommandType::Text);
    command.rect = rect;
    command.color = color;
    command.color2 = color;
    command.text = text;
    command.text_paint().font_size = font_size;
    command.text_paint().font_weight = font_weight;
    command.text_paint().font_family_hash = font_family_hash;
    command.text_paint().text_align = align;
    command.text_paint().text_single_line = single_line;
    display_list.push_back(std::move(command));
}

std::string ellipsize_single_line_text(const std::string& text,
                                       const Style& style,
                                       int available_width,
                                       const TextMeasureProvider& text_measure) {
    if (!style.text_overflow_ellipsis || !style.white_space_nowrap || available_width <= 0 || text.empty()) {
        return text;
    }
    const auto measure_width = [&](std::string_view value) {
        return measure_text_with_letter_spacing(text_measure,
                                                value,
                                                style.font_size,
                                                style.font_weight,
                                                style.font_family_hash,
                                                style.letter_spacing).width;
    };
    if (measure_width(text) <= available_width) {
        return text;
    }

    static constexpr std::string_view marker = "...";
    if (measure_width(marker) >= available_width) {
        return std::string(marker);
    }

    std::string prefix;
    prefix.reserve(text.size());
    for (std::size_t index = 0; index < text.size();) {
        const std::size_t begin = index;
        consume_utf8_codepoint(text, index);
        std::string candidate = prefix;
        candidate.append(text, begin, index - begin);
        candidate.append(marker);
        if (measure_width(candidate) > available_width) {
            break;
        }
        prefix.assign(candidate, 0, candidate.size() - marker.size());
    }
    prefix.append(marker);
    return prefix;
}

bool same_text_measure_provider(const TextMeasureProvider& left,
                                const TextMeasureProvider& right) {
    return left.measure == right.measure &&
        left.context == right.context &&
        left.measure_family == right.measure_family &&
        left.additive_measurement_supported == right.additive_measurement_supported &&
        left.measure_range == right.measure_range &&
        left.measure_range_family == right.measure_range_family;
}

const TextLayoutCache* valid_text_layout_cache(const LayoutBox& box,
                                               const LayerTreeBuilderOptions& options) {
    const TextLayoutCache& cache = box.text_layout_cache;
    bool ancestor_dirty = false;
    for (const Node* current = box.node == nullptr ? nullptr : box.node->parent;
         current != nullptr;
         current = current->parent) {
        if ((current->local_dirty_flags & (DomDirtyText | DomDirtyStyle | DomDirtyLayout |
                                           DomDirtyAttributes | DomDirtyTree)) != 0U) {
            ancestor_dirty = true;
            break;
        }
    }
    if (!cache.valid || box.node == nullptr || box.node->type != NodeType::Text ||
        ancestor_dirty ||
        (box.node->local_dirty_flags & (DomDirtyText | DomDirtyStyle | DomDirtyLayout |
                                        DomDirtyAttributes | DomDirtyTree)) != 0U ||
        cache.source_text != box.node->text ||
        cache.viewport_width != box.viewport_width ||
        cache.viewport_height != box.viewport_height ||
        cache.rect_width != box.rect.width ||
        cache.rect_height != box.rect.height ||
        cache.rect_x != box.rect.x ||
        cache.rect_y != box.rect.y ||
        cache.font_size != box.style.font_size ||
        cache.font_weight != box.style.font_weight ||
        cache.font_family_hash != box.style.font_family_hash ||
        cache.line_height != (box.style.line_height > 0
            ? box.style.line_height
            : cache.line_height) ||
        cache.text_indent != box.style.text_indent ||
        cache.letter_spacing != box.style.letter_spacing ||
        cache.text_transform != box.style.text_transform ||
        cache.overflow_wrap_anywhere != box.style.overflow_wrap_anywhere ||
        cache.white_space_nowrap != box.style.white_space_nowrap ||
        cache.text_overflow_ellipsis != box.style.text_overflow_ellipsis ||
        cache.text_align != box.style.text_align ||
        cache.text_system != options.text_system ||
        cache.text_system_generation != (options.text_system != nullptr
            ? options.text_system->generation() : 0) ||
        (cache.text_system == nullptr && !same_text_measure_provider(cache.text_measure, options.text_measure))) {
        return nullptr;
    }
    return &cache;
}

void push_prepared_text(DisplayList& display_list, Rect rect, Color color,
                        const TextLayoutCache& cache, int offset_x = 0, int offset_y = 0) {
    if (!cache.prepared_text || rect.width <= 0 || rect.height <= 0 || color.a == 0) return;
    const auto push = [&](const std::string& packet, int origin_x, int baseline_y) {
        if (packet.empty()) return;
        DisplayCommand command;
        command.set_type(DisplayCommandType::PreparedText);
        command.rect = rect;
        command.color = color;
        command.color2 = color;
        command.text = packet;
        set_prepared_text_origin(command, safe_add(origin_x, offset_x), safe_add(baseline_y, offset_y));
        display_list.push_back(std::move(command));
    };
    if (!cache.prepared_lines.empty()) {
        for (const auto& line : cache.prepared_lines)
            push(line.packet, line.origin_x, line.baseline_y);
    } else {
        push(cache.prepared_packet, cache.prepared_origin_x, cache.prepared_baseline_y);
    }
}

void push_text_with_layout(DisplayList& display_list,
                           Rect rect,
                           Color color,
                           const std::string& text,
                           const Style& style,
                           TextCommandAlign align,
                           const TextMeasureProvider& text_measure,
                           const TextLayoutCache* cached_layout = nullptr) {
    if (rect.width <= 0 || rect.height <= 0 || text.empty() || color.a == 0) {
        return;
    }
    const std::string rendered_text = ellipsize_single_line_text(
        cached_layout != nullptr ? cached_layout->rendered_text : text,
        style,
        rect.width,
        text_measure);
    const int line_height = style.line_height > 0
        ? style.line_height
        : style.font_size + std::max(6, style.font_size / 3);
    const bool wrap_anywhere = style.overflow_wrap_anywhere && !style.white_space_nowrap;
    const bool wrap_at_opportunities = !wrap_anywhere && !style.white_space_nowrap &&
        has_text_wrap_opportunity(rendered_text);
    const bool split_scalars = style.letter_spacing != 0;
    if (!wrap_anywhere && !wrap_at_opportunities && !split_scalars) {
        push_text(display_list, rect, color, rendered_text, style.font_size, style.font_weight,
                  style.font_family_hash, align, true);
        return;
    }

    const std::vector<std::string> lines = cached_layout != nullptr && !cached_layout->lines.empty() &&
            !(style.text_overflow_ellipsis && style.white_space_nowrap)
        ? cached_layout->lines
        : wrap_anywhere
        ? wrap_text_anywhere(text_measure,
                             rendered_text,
                             style.font_size,
                             style.font_weight,
                             style.font_family_hash,
                             style.letter_spacing,
                             rect.width)
        : wrap_at_opportunities
        ? wrap_text_at_opportunities(text_measure,
                                     rendered_text,
                                     style.font_size,
                                     style.font_weight,
                                     style.font_family_hash,
                                     style.letter_spacing,
                                     rect.width)
        : std::vector<std::string>{rendered_text};
    const int bounded_spacing = bounded_letter_spacing(style.font_size, style.letter_spacing);
    for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
        const std::string& line = lines[line_index];
        const std::size_t bounded_line_index = std::min<std::size_t>(
            line_index, static_cast<std::size_t>(std::numeric_limits<int>::max()));
        const int line_offset = clamp_int64_to_int(
            static_cast<std::int64_t>(bounded_line_index) * line_height);
        const int y = safe_add(rect.y, line_offset);
        const int rect_bottom = safe_edge(rect.y, rect.height);
        if (y >= rect_bottom) {
            break;
        }
        Rect line_rect{rect.x, y, rect.width, std::min(line_height, safe_span(y, rect_bottom))};
        if (!split_scalars) {
            push_text(display_list, line_rect, color, line, style.font_size, style.font_weight,
                      style.font_family_hash, align, true);
            continue;
        }
        const int line_width = measure_text_with_letter_spacing(text_measure,
                                                                 line,
                                                                 style.font_size,
                                                                 style.font_weight,
                                                                 style.font_family_hash,
                                                                 style.letter_spacing).width;
        int cursor_x = line_rect.x;
        if (align == TextCommandAlign::Center) {
            cursor_x = safe_add(cursor_x, std::max(0, safe_span(line_width, line_rect.width) / 2));
        } else if (align == TextCommandAlign::End) {
            cursor_x = safe_add(cursor_x, std::max(0, safe_span(line_width, line_rect.width)));
        }
        std::size_t scalar_index = 0;
        while (scalar_index < line.size()) {
            const std::size_t begin = scalar_index;
            consume_utf8_codepoint(line, scalar_index);
            const std::string scalar = line.substr(begin, scalar_index - begin);
            const int scalar_width = std::max(1, measure_text(text_measure,
                                                              scalar,
                                                              style.font_size,
                                                              style.font_weight,
                                                              style.font_family_hash).width);
            push_text(display_list,
                      Rect{cursor_x, line_rect.y, scalar_width, line_rect.height},
                      color,
                      scalar,
                      style.font_size,
                      style.font_weight,
                      style.font_family_hash,
                      TextCommandAlign::Start,
                      true);
            cursor_x = safe_add(cursor_x, scalar_width);
            if (scalar_index < line.size()) {
                cursor_x = safe_add(cursor_x, bounded_spacing);
            }
        }
    }
}

void push_image(DisplayList& display_list,
                Rect rect,
                std::uint32_t image_handle,
                ObjectFit object_fit,
                ObjectPosition object_position,
                ImageRendering image_rendering,
                int border_radius = 0, int border_radius_y = -1) {
    if (rect.width <= 0 || rect.height <= 0 || image_handle == 0) {
        return;
    }
    DisplayCommand command;
    command.set_type(DisplayCommandType::Image);
    command.rect = rect;
    command.image_paint().image_handle = image_handle;
    command.image_paint().object_fit = object_fit;
    command.image_paint().object_position = object_position;
    command.image_paint().image_rendering = image_rendering;
    command.image_paint().border_radius = border_radius;
    command.image_paint().border_radius_y = border_radius_y;
    command.color = Color{255, 255, 255, 255};
    command.color2 = command.color;
    display_list.push_back(std::move(command));
}

void push_text_decorations(DisplayList& display_list, const LayoutBox& box, Rect rect) {
    if (!box.style.text_decoration_underline && !box.style.text_decoration_line_through) {
        return;
    }
    const int thickness = std::max(1, box.style.font_size / 12);
    const int inset = std::max(0, box.style.font_size / 12);
    const Rect line_rect_base{
        safe_add(rect.x, inset),
        rect.y,
        std::max(0, safe_add(rect.width, safe_negate(safe_add(inset, inset)))),
        thickness,
    };
    if (line_rect_base.width <= 0) {
        return;
    }
    if (box.style.text_decoration_line_through) {
        Rect strike = line_rect_base;
        strike.y = safe_add(rect.y, std::max(0, safe_span(thickness, rect.height) / 2));
        push_fill_rect(display_list, strike, box.style.color);
    }
    if (box.style.text_decoration_underline) {
        Rect underline = line_rect_base;
        underline.y = safe_add(
            rect.y,
            std::max(0, safe_span(std::max(thickness + 1, box.style.font_size / 5), rect.height)));
        push_fill_rect(display_list, underline, box.style.color);
    }
}

TextCommandAlign text_command_align(TextAlign align) {
    switch (align) {
    case TextAlign::Center:
        return TextCommandAlign::Center;
    case TextAlign::End:
        return TextCommandAlign::End;
    case TextAlign::Start:
    default:
        return TextCommandAlign::Start;
    }
}

int estimate_marker_width(const std::string& text, int font_size) {
    std::int64_t units = 0;
    for (char ch : text) {
        units = std::min<std::int64_t>(std::numeric_limits<int>::max(),
                                       units + (ch == '.' || ch == ' ' ? 4 : 8));
    }
    const std::int64_t width = (static_cast<std::int64_t>(std::max(0, font_size)) * units + 7) / 14;
    return std::max(font_size, clamp_int64_to_int(width));
}

int list_item_ordinal(const Node& node) {
    if (node.parent == nullptr) {
        return 1;
    }
    int ordinal = 0;
    for (const auto& sibling : node.parent->children) {
        if (sibling->type == NodeType::Element && sibling->tag_name == "li") {
            ++ordinal;
        }
        if (sibling.get() == &node) {
            return std::max(1, ordinal);
        }
    }
    return std::max(1, ordinal);
}

Rect content_rect_for(const LayoutBox& box);

std::string generated_before_text(const LayoutBox& box) {
    if (box.style.before_content_kind == GeneratedContentKind::Text) {
        return box.style.before_content_text;
    }
    if (box.style.before_content_kind == GeneratedContentKind::Counter && box.node != nullptr) {
        return std::to_string(list_item_ordinal(*box.node)) + box.style.before_counter_suffix;
    }
    return {};
}

std::string generated_after_text(const LayoutBox& box) {
    if (box.style.after_content_kind == GeneratedContentKind::Text) {
        return box.style.after_content_text;
    }
    if (box.style.after_content_kind == GeneratedContentKind::Counter && box.node != nullptr) {
        return std::to_string(list_item_ordinal(*box.node)) + box.style.after_counter_suffix;
    }
    return {};
}

std::string native_marker_text(const LayoutBox& box) {
    if (box.node == nullptr || box.node->type != NodeType::Element || box.node->tag_name != "li") {
        return {};
    }
    if (box.style.list_style_type == ListStyleType::Decimal) {
        return std::to_string(list_item_ordinal(*box.node)) + ".";
    }
    if (box.style.list_style_type == ListStyleType::Disc) {
        return "*";
    }
    return {};
}

void paint_list_marker(const LayoutBox& box, DisplayList& display_list) {
    if (box.node == nullptr || box.node->type != NodeType::Element || box.node->tag_name != "li") {
        return;
    }
    std::string marker = generated_before_text(box);
    bool generated = !marker.empty();
    if (!generated) {
        marker = native_marker_text(box);
    }
    if (marker.empty()) {
        return;
    }

    const int font_weight = generated && box.style.before_font_weight_specified
        ? box.style.before_font_weight
        : box.style.font_weight;
    const Color color = generated && box.style.before_color_specified ? box.style.before_color : box.style.color;
    const int width = estimate_marker_width(marker, box.style.font_size);
    const int marker_x = generated
        ? box.rect.x + (box.style.before_left_specified ? box.style.before_left : 0)
        : box.rect.x - width - 4;
    push_text(display_list,
              Rect{marker_x, box.rect.y, width, std::max(box.rect.height, box.style.font_size + 4)},
              color,
              marker,
              box.style.font_size,
              font_weight,
              box.style.font_family_hash,
              TextCommandAlign::Start,
              true);
}

void paint_generated_inline_content(const LayoutBox& box,
                                    DisplayList& display_list,
                                    CssPseudoElement pseudo_element) {
    if (box.node == nullptr || box.node->type != NodeType::Element || box.node->tag_name == "li") {
        return;
    }
    const Rect content = content_rect_for(box);
    const auto paint_one = [&](const std::string& text,
                               bool after,
                               TextCommandAlign align) {
        if (text.empty()) {
            return;
        }
        const int font_weight = after
            ? (box.style.after_font_weight_specified ? box.style.after_font_weight : box.style.font_weight)
            : (box.style.before_font_weight_specified ? box.style.before_font_weight : box.style.font_weight);
        const Color color = after
            ? (box.style.after_color_specified ? box.style.after_color : box.style.color)
            : (box.style.before_color_specified ? box.style.before_color : box.style.color);
        Rect rect = content;
        if (!after && box.style.before_left_specified) {
            rect.x = safe_add(rect.x, box.style.before_left);
            rect.width = std::max(0, rect.width - box.style.before_left);
        } else if (after && box.style.after_left_specified) {
            rect.x = safe_add(rect.x, box.style.after_left);
            rect.width = std::max(0, rect.width - box.style.after_left);
        }
        push_text(display_list,
                  rect,
                  color,
                  text,
                  box.style.font_size,
                  font_weight,
                  box.style.font_family_hash,
                  align,
                  true);
    };

    if (pseudo_element == CssPseudoElement::Before) {
        paint_one(generated_before_text(box), false, TextCommandAlign::Start);
    } else if (pseudo_element == CssPseudoElement::After) {
        paint_one(generated_after_text(box), true, TextCommandAlign::End);
    }
}

bool parse_float_attribute(const Node& node, const char* name, float& output) {
    const std::string& value = node.attribute(name);
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || errno == ERANGE || !std::isfinite(parsed)) {
        return false;
    }
    while (end != nullptr && std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    if (end == nullptr || *end != '\0') {
        return false;
    }
    output = parsed;
    return true;
}

void paint_box_shadow(const LayoutBox& box,
                      DisplayList& display_list,
                      const LayerTreeBuilderOptions& options) {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    (void)box;
    (void)display_list;
    (void)options;
    return;
#else
    if (!has_shadow(box.style)) {
        return;
    }
    if (has_elliptical_difference(resolve_border_radii(box))) {
        report_diagnostic(options.diagnostics, DiagnosticStage::LayerTree,
                          DiagnosticSeverity::Warning, "layer-box-shadow-ellipse-unsupported",
                          "box-shadow was refused for elliptical border-radius because this profile supports circular shadow geometry only.");
        return;
    }
    const BoxShadowStyle& shadow = box.style.box_shadow;
    const Color shadow_color = shadow.uses_current_color ? box.style.color : shadow.color;
    int blur = shadow.blur;
    constexpr int kMaxSoftShadowBlur = 24;
    if (blur > kMaxSoftShadowBlur) {
        report_diagnostic(options.diagnostics,
                          DiagnosticStage::LayerTree,
                          DiagnosticSeverity::Warning,
                          "layer-box-shadow-blur-clamped",
                          "box-shadow blur radius is above the embedded soft-shadow budget",
                          "blur=" + std::to_string(blur) + "px limit=" + std::to_string(kMaxSoftShadowBlur) + "px");
        blur = kMaxSoftShadowBlur;
    }
    const int extent = std::max(1, safe_add(std::max(1, blur), shadow.spread));
    const int spread = shadow.spread;
    const Rect shadow_rect{
        safe_edge(box.rect.x, safe_add(shadow.offset_x, -extent)),
        safe_edge(box.rect.y, safe_add(shadow.offset_y, -extent)),
        safe_add(box.rect.width, safe_add(extent, extent)),
        safe_add(box.rect.height, safe_add(extent, extent)),
    };
    const long long shadow_area = static_cast<long long>(std::max(0, shadow_rect.width)) *
        static_cast<long long>(std::max(0, shadow_rect.height));
    if (shadow_area > kBoxShadowAreaWarningPixels) {
        report_diagnostic(options.diagnostics,
                          DiagnosticStage::LayerTree,
                          DiagnosticSeverity::Warning,
                          "layer-box-shadow-area-budget",
                          "box-shadow paint area is above the embedded shadow budget",
                          "area=" + std::to_string(shadow_area) +
                              "px limit=" + std::to_string(kBoxShadowAreaWarningPixels) + "px");
    }
    push_box_shadow(display_list,
                    Rect{safe_edge(box.rect.x, safe_add(shadow.offset_x, safe_negate(spread))),
                         safe_edge(box.rect.y, safe_add(shadow.offset_y, safe_negate(spread))),
                         safe_add(box.rect.width, safe_add(spread, spread)),
                         safe_add(box.rect.height, safe_add(spread, spread))},
                    shadow_color,
                    expand_corner_radii(resolved_border_radius(box), spread),
                    extent,
                    blur);
#endif
}

void paint_outline(const LayoutBox& box, DisplayList& display_list) {
    if (box.style.outline_width <= 0 || box.style.outline_color.a == 0) {
        return;
    }
    const int width = box.style.outline_width;
    const int extent = std::max(0, safe_add(width, box.style.outline_offset));
    const Rect outline_rect{
        safe_edge(box.rect.x, -extent),
        safe_edge(box.rect.y, -extent),
        safe_edge(box.rect.width, safe_add(extent, extent)),
        safe_edge(box.rect.height, safe_add(extent, extent)),
    };
    const int border_radius = resolved_border_radius(box);
    push_stroke_rect(display_list,
                     outline_rect,
                     box.style.outline_color,
                     width,
                     has_corner_radius(border_radius) ? expand_corner_radii(border_radius, extent) : 0,
                     has_corner_radius(resolved_border_radius_y(box))
                         ? expand_corner_radii(resolved_border_radius_y(box), extent) : 0);
}

void paint_meter_bar(const LayoutBox& box, DisplayList& display_list) {
    if (box.node == nullptr || box.node->type != NodeType::Element) {
        return;
    }
    const bool is_progress = box.node->tag_name == "progress";
    const bool is_meter = box.node->tag_name == "meter";
    if (!is_progress && !is_meter) {
        return;
    }

    float min_value = 0.0F;
    float max_value = 1.0F;
    float value = 0.0F;
    if (is_progress) {
        max_value = 1.0F;
        parse_float_attribute(*box.node, "max", max_value);
        if (!parse_float_attribute(*box.node, "value", value)) {
            value = 0.0F;
        }
    } else {
        parse_float_attribute(*box.node, "min", min_value);
        parse_float_attribute(*box.node, "max", max_value);
        if (!parse_float_attribute(*box.node, "value", value)) {
            value = min_value;
        }
    }
    if (max_value <= min_value) {
        return;
    }

    const float ratio = std::max(0.0F, std::min(1.0F, (value - min_value) / (max_value - min_value)));
    Rect inner{
        box.rect.x + box.style.border_width.left + 1,
        box.rect.y + box.style.border_width.top + 1,
        std::max(0, box.rect.width - box.style.border_width.left - box.style.border_width.right - 2),
        std::max(0, box.rect.height - box.style.border_width.top - box.style.border_width.bottom - 2),
    };
    inner.width = static_cast<int>(static_cast<float>(inner.width) * ratio + 0.5F);
    const Color fill = is_progress ? Color{37, 99, 235, 255} : Color{22, 163, 74, 255};
    push_fill_rect(display_list, inner, fill, expand_corner_radii(resolved_border_radius(box), -1),
                   expand_corner_radii(resolved_border_radius_y(box), -1));
}

int range_state_value(const FormControlState& state) {
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(state.value.c_str(), &end, 10);
    if (end == state.value.c_str() || errno == ERANGE || end == nullptr || *end != '\0' ||
        parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
        return state.min;
    }
    return static_cast<int>(parsed);
}

void paint_form_control(const LayoutBox& box, DisplayList& display_list) {
    if (box.node == nullptr || box.node->type != NodeType::Element || !is_form_control(*box.node)) {
        return;
    }
    const FormControlState& state = ensure_form_control_state(*box.node);
    const Rect inner{
        box.rect.x + box.style.border_width.left + box.style.padding.left,
        box.rect.y + box.style.border_width.top + box.style.padding.top,
        std::max(0, box.rect.width - box.style.border_width.left - box.style.border_width.right -
                    box.style.padding.left - box.style.padding.right),
        std::max(0, box.rect.height - box.style.border_width.top - box.style.border_width.bottom -
                    box.style.padding.top - box.style.padding.bottom),
    };

    if (state.kind == FormControlKind::Checkbox || state.kind == FormControlKind::Radio) {
        if (state.checked) {
            const Rect mark{
                box.rect.x + std::max(3, box.rect.width / 4),
                box.rect.y + std::max(3, box.rect.height / 4),
                std::max(4, box.rect.width / 2),
                std::max(4, box.rect.height / 2),
            };
            push_fill_rect(display_list, mark, Color{37, 99, 235, 255}, state.kind == FormControlKind::Radio ? 99 : 1);
        }
        return;
    }

    if (state.kind == FormControlKind::Range) {
        const int track_height = 4;
        const Rect track{
            inner.x,
            inner.y + std::max(0, (inner.height - track_height) / 2),
            inner.width,
            track_height,
        };
        push_fill_rect(display_list, track, Color{203, 213, 225, 255}, 2);
        const std::int64_t min_value = state.min;
        const std::int64_t max_value = state.max;
        const std::int64_t denom = std::max<std::int64_t>(1, max_value - min_value);
        const std::int64_t value = std::max<std::int64_t>(min_value,
                                                          std::min<std::int64_t>(range_state_value(state),
                                                                                 max_value));
        const std::int64_t fill_width64 =
            (static_cast<std::int64_t>(inner.width) * (value - min_value) + denom / 2) / denom;
        const int fill_width = static_cast<int>(std::max<std::int64_t>(
            0, std::min<std::int64_t>(fill_width64, inner.width)));
        push_fill_rect(display_list, Rect{track.x, track.y, fill_width, track.height}, Color{37, 99, 235, 255}, 2);
        const int thumb_size = std::max(10, std::min(18, box.rect.height - 2));
        const int thumb_x = inner.x + fill_width - thumb_size / 2;
        push_fill_rect(display_list,
                       Rect{thumb_x, box.rect.y + std::max(0, (box.rect.height - thumb_size) / 2),
                            thumb_size, thumb_size},
                       Color{37, 99, 235, 255},
                       thumb_size / 2);
        return;
    }

    if (state.kind == FormControlKind::Text || state.kind == FormControlKind::TextArea ||
        state.kind == FormControlKind::Date || state.kind == FormControlKind::Time ||
        state.kind == FormControlKind::Color || state.kind == FormControlKind::File ||
        state.kind == FormControlKind::Select) {
        std::string text = form_control_display_text(*box.node);
        Color text_color = box.style.color;
        if (text.empty()) {
            text = box.node->attribute("placeholder");
            text_color = Color{100, 116, 139, 255};
        }
        if (state.kind == FormControlKind::File && text.empty()) {
            text = "Choose file";
        }
        const int arrow_width = state.kind == FormControlKind::Select ? 14 : 0;
        push_text(display_list,
                  Rect{inner.x + 2, inner.y, std::max(0, inner.width - arrow_width - 4), inner.height},
                  text_color,
                  text,
                  box.style.font_size,
                  box.style.font_weight,
                  box.style.font_family_hash,
                  text_command_align(box.style.text_align),
                  true);
        if (state.kind == FormControlKind::Select) {
            push_text(display_list,
                      Rect{inner.x + std::max(0, inner.width - 12), inner.y, 12, inner.height},
                      Color{15, 23, 42, 255},
                      "v",
                      box.style.font_size,
                      box.style.font_weight,
                      box.style.font_family_hash,
                      TextCommandAlign::Center,
                      true);
        }
    }
}

bool resolve_image_handle(const LayoutBox& box,
                          ImageResolveKind kind,
                          std::uint16_t background_resource_id,
                          const LayerTreeBuilderOptions& options,
                          ResolvedImageResource& resource) {
    resource = {};
    if (box.node == nullptr || box.node->type != NodeType::Element || options.image_resolver.resolve == nullptr) {
        return false;
    }
    if (kind == ImageResolveKind::Content &&
        (box.node->tag_name != "img" && box.node->tag_name != "canvas")) {
        return false;
    }
    if (kind == ImageResolveKind::Content && box.node->tag_name == "img" && box.node->attribute("src").empty()) {
        return false;
    }
    if (kind == ImageResolveKind::Background && background_resource_id == 0) {
        return false;
    }
    if (active_image_resolution_state == nullptr ||
        active_image_resolution_state->attempts >= kMaxImageResolutionAttemptsPerBuild) {
        if (active_image_resolution_state != nullptr && !active_image_resolution_state->limit_reported) {
            report_diagnostic(options.diagnostics, DiagnosticStage::LayerTree,
                              DiagnosticSeverity::Warning, "layer-image-resolution-limit",
                              "Image resolution attempt limit reached; later image layers were omitted",
                              "Reduce the number of image resources in this build.");
            active_image_resolution_state->limit_reported = true;
        }
        return false;
    }
    ++active_image_resolution_state->attempts;
    if (!options.image_resolver.resolve(*box.node, kind, background_resource_id,
                                         resource, options.image_resolver.context) ||
        resource.image_handle == 0) {
        return false;
    }
    // Natural size is metadata for background geometry, not a new content
    // decoder limit. Legacy content images continue to use their valid handle.
    if (kind == ImageResolveKind::Background) {
        const auto& size = resource.intrinsic_size;
        if ((size.state != ImageIntrinsicSizeState::Unknown &&
             size.state != ImageIntrinsicSizeState::Known) ||
            (size.state == ImageIntrinsicSizeState::Known &&
             (size.width == 0 || size.height == 0 || size.width > 8191 || size.height > 8191))) {
            report_diagnostic(options.diagnostics, DiagnosticStage::LayerTree,
                              DiagnosticSeverity::Warning, "layer-background-image-size-invalid",
                              "Background image natural size is invalid; image layer was omitted");
            return false;
        }
    }
    return true;
}

Rect content_rect_for(const LayoutBox& box) {
    const int horizontal_insets = safe_add(
        safe_add(box.style.border_width.left, box.style.border_width.right),
        safe_add(box.style.padding.left, box.style.padding.right));
    const int vertical_insets = safe_add(
        safe_add(box.style.border_width.top, box.style.border_width.bottom),
        safe_add(box.style.padding.top, box.style.padding.bottom));
    return Rect{
        safe_add(box.rect.x, safe_add(box.style.border_width.left, box.style.padding.left)),
        safe_add(box.rect.y, safe_add(box.style.border_width.top, box.style.padding.top)),
        std::max(0, safe_add(box.rect.width, safe_negate(horizontal_insets))),
        std::max(0, safe_add(box.rect.height, safe_negate(vertical_insets))),
    };
}

int scrollable_content_height(const LayoutBox& box) {
    const Rect content = content_rect_for(box);
    int bottom = content.y;
    for (const auto& child : box.children) {
        bottom = std::max(bottom, safe_add(safe_edge(child->rect.y, child->rect.height),
                                           child->style.margin.bottom));
    }
    return safe_span(content.y, bottom);
}

int max_scroll_y_for(const LayoutBox& box) {
    if (!has_scrollable_overflow(box.style)) {
        return 0;
    }
    const Rect content = content_rect_for(box);
    return std::max(0, safe_add(scrollable_content_height(box), safe_negate(std::max(0, content.height))));
}

int resolved_scroll_y_for(const LayoutBox& box, const LayerTreeBuilderOptions& options) {
    if (box.node == nullptr || options.scroll_resolver.resolve_y == nullptr) {
        return 0;
    }
    const int max_scroll = max_scroll_y_for(box);
    if (max_scroll <= 0) {
        return 0;
    }
    const int requested = options.scroll_resolver.resolve_y(*box.node, max_scroll, options.scroll_resolver.context);
    return std::max(0, std::min(requested, max_scroll));
}

bool paint_scroll_indicator(const LayoutBox& box, int scroll_y, int max_scroll_y, DisplayList& display_list) {
    if (max_scroll_y <= 0 || scroll_y < 0) {
        return false;
    }
    const Rect content = content_rect_for(box);
    if (content.width < 16 || content.height < 18) {
        return false;
    }
    constexpr int kTrackWidth = 3;
    constexpr int kTrackInset = 2;
    constexpr int kMinThumbHeight = 12;
    const int track_height = std::max(0, content.height - kTrackInset * 2);
    if (track_height < kMinThumbHeight) {
        return false;
    }
    const std::int64_t scrollable_height = std::max<std::int64_t>(
        1, static_cast<std::int64_t>(content.height) + max_scroll_y);
    const std::int64_t thumb_numerator = static_cast<std::int64_t>(track_height) * content.height;
    int thumb_height = std::max(kMinThumbHeight, clamp_int64_to_int(
        thumb_numerator / scrollable_height));
    thumb_height = std::min(track_height, thumb_height);
    const int travel = std::max(0, track_height - thumb_height);
    const int clamped_scroll = std::max(0, std::min(scroll_y, max_scroll_y));
    const int thumb_offset = max_scroll_y > 0
        ? clamp_int64_to_int(static_cast<std::int64_t>(travel) * clamped_scroll / max_scroll_y)
        : 0;
    const int thumb_y = safe_add(safe_add(content.y, kTrackInset), thumb_offset);
    const int track_x = safe_add(safe_edge(content.x, content.width), -(kTrackWidth + kTrackInset));
    const Rect track{track_x, safe_add(content.y, kTrackInset), kTrackWidth, track_height};
    const Rect thumb{track_x, thumb_y, kTrackWidth, thumb_height};
    push_fill_rect(display_list, track, Color{255, 255, 255, 48}, 2);
    push_fill_rect(display_list, thumb, Color{255, 255, 255, 176}, 2);
    return true;
}

void translate_display_commands(DisplayList& display_list, std::size_t begin, int dx, int dy) {
    if (dx == 0 && dy == 0) {
        return;
    }
    for (std::size_t index = begin; index < display_list.size(); ++index) {
        translate_display_command(display_list[index], dx, dy);
    }
}

#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
int select_popup_row_height(const LayoutBox& box) {
    return std::max(20, box.style.line_height > 0
        ? box.style.line_height
        : box.style.font_size + std::max(6, box.style.font_size / 3));
}
#endif

#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
bool paint_select_popup(const LayoutBox& box,
                        const Rect& viewport,
                        int scroll_y,
                        const LayerTreeBuilderOptions& options,
                        DisplayList& display_list,
                        Rect& popup_bounds) {
    if (box.node == nullptr || !select_popup_is_open(*box.node)) {
        return false;
    }
    std::vector<const Node*> options_list;
    form_control_collect_options(*box.node, options_list);
    const int option_count = static_cast<int>(options_list.size());
    const SelectPopupGeometry geometry = select_popup_geometry(
        box.rect, viewport, option_count, select_popup_row_height(box));
    if (geometry.visible_option_count <= 0 || geometry.rect.width <= 0 || geometry.rect.height <= 0) {
        return false;
    }
    if (geometry.visible_option_count < option_count) {
        report_diagnostic(options.diagnostics,
                          DiagnosticStage::LayerTree,
                          DiagnosticSeverity::Warning,
                          "select-popup-option-limit",
                          "The select popup cannot show every option inside the current viewport",
                          "Use a shorter option list or a page-level picker until popup scrolling is enabled.");
    }

    popup_bounds = geometry.rect;
    popup_bounds.y = safe_add(popup_bounds.y, safe_negate(scroll_y));
    push_fill_rect(display_list, popup_bounds, Color{248, 250, 252, 255}, 2);
    const EdgeSizes border{1, 1, 1, 1};
    push_border_rects(display_list, popup_bounds, border, box.style.border_color, 2);
    const FormControlState& state = ensure_form_control_state(*box.node);
    for (int visible_index = 0; visible_index < geometry.visible_option_count; ++visible_index) {
        const int option_index = safe_add(geometry.first_option_index, visible_index);
        const int row_offset = clamp_int64_to_int(
            static_cast<std::int64_t>(visible_index) * geometry.row_height);
        const Rect row{popup_bounds.x,
                       safe_add(popup_bounds.y, row_offset),
                       popup_bounds.width,
                       geometry.row_height};
        if (option_index == state.selected_index) {
            push_fill_rect(display_list, row, Color{219, 234, 254, 255}, 0);
        }
        const Node* option = option_index >= 0 &&
                static_cast<std::size_t>(option_index) < options_list.size()
            ? options_list[static_cast<std::size_t>(option_index)]
            : nullptr;
        const Color text_color = option != nullptr && form_control_option_is_disabled_node(*option)
            ? Color{148, 163, 184, 255}
            : box.style.color;
        push_text_with_layout(display_list,
                              Rect{safe_add(row.x, 5), row.y, std::max(0, safe_add(row.width, -10)), row.height},
                              text_color,
                              option != nullptr ? form_control_option_text_from_node(*option) : std::string{},
                              box.style,
                              TextCommandAlign::Start,
                              options.text_measure);
    }
    return true;
}
#endif

void paint_background_image(const LayoutBox& box, const BackgroundImageStyleValues& background_style,
                            const detail::BackgroundBoxAreas& background_areas,
                            detail::BackgroundBoxAreasStatus background_areas_status,
                            DisplayList& display_list, const LayerTreeBuilderOptions& options,
                            std::size_t remaining_commands, std::size_t box_command_begin) {
    ResolvedImageResource background_resource;
    if (resolve_image_handle(box,
                             ImageResolveKind::Background,
                             background_style.resource_id,
                             options,
                             background_resource)) {
        detail::BackgroundImageGeometryRequest geometry_request;
        geometry_request.positioning_area = background_areas.positioning_area;
        geometry_request.painting_area = background_areas.painting_area;
        geometry_request.intrinsic_size_known = background_resource.intrinsic_size.state == ImageIntrinsicSizeState::Known;
        geometry_request.intrinsic_width = background_resource.intrinsic_size.width;
        geometry_request.intrinsic_height = background_resource.intrinsic_size.height;
        geometry_request.size_mode = static_cast<detail::BackgroundImageSizeMode>(background_style.size);
        const auto to_q1000 = [](int percent) {
            const auto scaled = static_cast<std::int64_t>(percent) * 1000;
            return static_cast<std::int32_t>(std::clamp<std::int64_t>(
                scaled, std::numeric_limits<std::int32_t>::min(),
                std::numeric_limits<std::int32_t>::max()));
        };
        geometry_request.position_x_percent_q1000 = to_q1000(background_style.position_x_percent);
        geometry_request.position_y_percent_q1000 = to_q1000(background_style.position_y_percent);
        const bool intrinsic_state_valid =
            background_resource.intrinsic_size.state == ImageIntrinsicSizeState::Unknown ||
            background_resource.intrinsic_size.state == ImageIntrinsicSizeState::Known;
        detail::BackgroundRepeatRequest repeat_request;
        repeat_request.image = geometry_request;
        repeat_request.repeat_x = static_cast<detail::BackgroundAxisRepeat>(background_style.repeat_x);
        repeat_request.repeat_y = static_cast<detail::BackgroundAxisRepeat>(background_style.repeat_y);
        const auto prior_commands = display_list.size() - box_command_begin;
        repeat_request.max_tiles = prior_commands < remaining_commands
            ? remaining_commands - prior_commands : 0;
        detail::BackgroundRepeatPlan plan;
        auto status = background_areas_status == detail::BackgroundBoxAreasStatus::Ready && intrinsic_state_valid
            ? detail::plan_background_image_tiles(repeat_request, plan)
            : detail::BackgroundRepeatStatus::InvalidGeometry;
        // Validate every compact clip before publishing any member of this layer.
        for (std::size_t i = 0; status == detail::BackgroundRepeatStatus::Ready && i < plan.count; ++i) {
            BackgroundPaintClip clip;
            if (!detail::make_background_paint_clip(plan.tiles[i], background_areas.painting_area, clip))
                status = detail::BackgroundRepeatStatus::CoordinateOverflow;
        }
        if (status == detail::BackgroundRepeatStatus::Ready) {
            const auto image_begin = display_list.size();
            try {
                display_list.reserve(image_begin + plan.count);
                for (std::size_t i = 0; i < plan.count; ++i) {
                    BackgroundPaintClip clip;
                    detail::make_background_paint_clip(plan.tiles[i], background_areas.painting_area, clip);
                    push_image(display_list, plan.tiles[i], background_resource.image_handle,
                               ObjectFit::Fill, {}, box.style.image_rendering, background_areas.painting_radius,
                               background_areas.painting_radius_y);
                    display_list.back().image_paint().background_paint_clip = clip;
                }
            } catch (const std::bad_alloc&) {
                display_list.resize(image_begin);
                status = detail::BackgroundRepeatStatus::TileLimit;
            }
        }
        if (status != detail::BackgroundRepeatStatus::Ready && status != detail::BackgroundRepeatStatus::Empty) {
            const char* code = status == detail::BackgroundRepeatStatus::TileLimit
                ? "layer-background-repeat-limit"
                : status == detail::BackgroundRepeatStatus::FractionalGeometry
                    ? "layer-background-repeat-fractional"
                    : status == detail::BackgroundRepeatStatus::UnsupportedCombination
                        ? "layer-background-repeat-unsupported" : "layer-background-image-geometry";
            report_diagnostic(options.diagnostics, DiagnosticStage::LayerTree,
                              DiagnosticSeverity::Warning, code,
                              "Background image pattern was refused; the complete image layer was omitted",
                              "Use integral supported geometry within the 64-tile and remaining display-command budgets.");
        }
    }
}

void paint_canonical_background(const LayoutBox& box, DisplayList& display_list,
                                const LayerTreeBuilderOptions& options,
                                std::size_t remaining_commands, std::size_t box_command_begin) {
    const bool valid_count = box.style.background_layer_count <= kMaxBackgroundLayers;
    const std::size_t count = valid_count ? box.style.background_layer_count : 0;
    if (!valid_count) {
        report_diagnostic(options.diagnostics, DiagnosticStage::LayerTree, DiagnosticSeverity::Warning,
                          "layer-background-layer-limit",
                          "Background image list was refused; its layer count exceeds the four-slot value boundary.");
    }
    const BackgroundImageStyleValues color_style = count == 0
        ? BackgroundImageStyleValues{} : box.style.background_layers[count - 1].image;
    Rect color_area;
    int color_radius = 0;
    int color_radius_y = 0;
    bool color_clip_ready = background_color_clip(box, color_style.clip, color_area, color_radius, color_radius_y);
    if (!color_clip_ready && color_style.clip == BackgroundBox::BorderBox) {
        color_area = paint_rect_for(box);
        color_radius = resolved_border_radius(box);
        color_radius_y = resolved_border_radius_y(box);
        color_clip_ready = color_area.width > 0 && color_area.height > 0;
    }
    if (color_clip_ready) {
        push_fill_rect(display_list, color_area, box.style.background_color, color_radius, color_radius_y);
    } else {
        report_diagnostic(options.diagnostics, DiagnosticStage::LayerTree, DiagnosticSeverity::Warning,
                          "layer-background-color-clip-geometry",
                          "Background color was omitted because its clip geometry is empty or unsupported.");
    }
    for (std::size_t index = count; index > 0; --index) {
        const auto& layer = box.style.background_layers[index - 1];
        if (layer.kind == BackgroundLayerKind::None) continue;
        detail::BackgroundBoxAreas areas;
        const auto status = detail::resolve_background_box_areas(
            background_box_request(box, layer.image), areas);
        if (layer.kind == BackgroundLayerKind::Url) {
            paint_background_image(box, layer.image, areas, status, display_list,
                                   options, remaining_commands, box_command_begin);
        } else if (layer.kind == BackgroundLayerKind::Gradient) {
            if (!paint_background_gradient(layer.paint, box, display_list, options,
                                           remaining_commands, box_command_begin, &layer.image)) {
                // Cropped modern-paint profiles retain their existing solid fallback.
                if (status == detail::BackgroundBoxAreasStatus::Ready)
                    paint_background_paint(layer.paint, areas.painting_area, areas.painting_radius,
                                           display_list, options, areas.painting_radius_y);
            }
        }
    }
}

void paint_box_self(const LayoutBox& box, DisplayList& display_list,
                    const LayerTreeBuilderOptions& options, std::size_t remaining_commands) {
    const std::size_t box_command_begin = display_list.size();
    const Rect paint_rect = paint_rect_for(box);
    const int border_radius = resolved_border_radius(box);
    const auto radii = resolve_border_radii(box);
    if (box.style.border_radius_axes_specified) {
        const bool has_authored_radius = std::any_of(
            box.style.border_radius_x_values.begin(), box.style.border_radius_x_values.end(),
            [](int value) { return value != 0; }) ||
            std::any_of(box.style.border_radius_y_values.begin(), box.style.border_radius_y_values.end(),
                        [](int value) { return value != 0; });
        const auto representable = [](const CornerRadii& axis) {
            return std::max({axis.top_left, axis.top_right, axis.bottom_right, axis.bottom_left}) <= 127 ||
                (axis.top_left == axis.top_right && axis.top_left == axis.bottom_right &&
                 axis.top_left == axis.bottom_left);
        };
        if (has_authored_radius && (!representable(radii.horizontal) || !representable(radii.vertical))) {
            report_diagnostic(options.diagnostics, DiagnosticStage::LayerTree, DiagnosticSeverity::Warning,
                              "layer-border-radius-range",
                              "Elliptical border-radius was refused because a used corner radius exceeds the supported 127px axis limit.");
        }
    }
    paint_box_shadow(box, display_list, options);
    paint_canonical_background(box, display_list, options, remaining_commands, box_command_begin);

    if (has_border(box.style.border_width)) {
        push_border_rects(display_list,
                          paint_rect,
                          box.style.border_width,
                          box.style.border_color,
                          border_radius, resolved_border_radius_y(box));
    }
    paint_outline(box, display_list);

    paint_meter_bar(box, display_list);
    paint_form_control(box, display_list);
    paint_list_marker(box, display_list);
    paint_generated_inline_content(box, display_list, CssPseudoElement::Before);

    ResolvedImageResource content_resource;
    const bool emitted_by_shared_inline = box.text_layout_cache.shared_inline &&
        box.text_layout_cache.shared_inline->member != 0 &&
        detail::shared_inline_current(*box.text_layout_cache.shared_inline, options.text_system);
    if (!emitted_by_shared_inline &&
        resolve_image_handle(box, ImageResolveKind::Content, 0, options, content_resource)) {
        push_image(display_list,
                   content_rect_for(box),
                   content_resource.image_handle,
                   box.style.object_fit,
                   box.style.object_position,
                   box.style.image_rendering,
                   resolved_border_radius(box), resolved_border_radius_y(box));
    }

    if (box.text_layout_cache.shared_inline) {
        const auto& binding = *box.text_layout_cache.shared_inline;
        const auto used = display_list.size()-box_command_begin;
        const auto available = remaining_commands-std::min(used, remaining_commands);
        const auto image_count = binding.snapshot->atomic_placements.size();
        if (binding.member == 0 && binding.snapshot->paragraph.fragments.size() <= available &&
            image_count <= available - std::min(available, binding.snapshot->paragraph.fragments.size()) &&
            detail::shared_inline_current(binding, options.text_system)) {
            struct SharedImageContext { const LayerTreeBuilderOptions* options; } image_context{&options};
            const auto emit_image = [](const LayoutBox& image_box, DisplayList& draft, void* context) -> bool {
                const auto& builder_options = *static_cast<SharedImageContext*>(context)->options;
                ResolvedImageResource resource;
                if (!resolve_image_handle(image_box, ImageResolveKind::Content, 0, builder_options, resource))
                    return true;
                const Rect rect = content_rect_for(image_box);
                if (rect.width <= 0 || rect.height <= 0) return true;
                const auto command_count = draft.size();
                push_image(draft, rect, resource.image_handle, image_box.style.object_fit,
                    image_box.style.object_position, image_box.style.image_rendering,
                    resolved_border_radius(image_box), resolved_border_radius_y(image_box));
                return draft.size() == command_count + 1 &&
                    draft.back().type() == DisplayCommandType::Image;
            };
            detail::emit_shared_inline(box, display_list, emit_image, &image_context);
        }
        return;
    }
    if (box.node != nullptr && box.node->type == NodeType::Text) {
        const TextLayoutCache* cached_layout = valid_text_layout_cache(box, options);
        const std::string text = cached_layout != nullptr
            ? cached_layout->rendered_text
            : transformed_render_text(*box.node, box.style.text_transform);
        if (cached_layout != nullptr && cached_layout->prepared_text) {
            if (has_text_shadow(box.style)) {
                const TextShadowStyle& shadow = box.style.text_shadow;
                Rect shadow_rect = box.rect;
                shadow_rect.x = safe_add(shadow_rect.x, shadow.offset_x);
                shadow_rect.y = safe_add(shadow_rect.y, shadow.offset_y);
                push_prepared_text(display_list, shadow_rect,
                    shadow.uses_current_color ? box.style.color : shadow.color,
                    *cached_layout, shadow.offset_x, shadow.offset_y);
            }
            push_prepared_text(display_list, box.rect, box.style.color, *cached_layout);
        } else if (options.text_system == nullptr) {
            if (has_text_shadow(box.style)) {
                const TextShadowStyle& shadow = box.style.text_shadow;
                Rect shadow_rect = box.rect;
                shadow_rect.x = safe_add(shadow_rect.x, shadow.offset_x);
                shadow_rect.y = safe_add(shadow_rect.y, shadow.offset_y);
                push_text_with_layout(display_list,
                                      shadow_rect,
                                      shadow.uses_current_color ? box.style.color : shadow.color,
                                      text,
                                      box.style,
                                      text_command_align(box.style.text_align),
                                      options.text_measure,
                                      cached_layout);
            }
            push_text_with_layout(display_list,
                                  box.rect,
                                  box.style.color,
                                  text,
                                  box.style,
                                  text_command_align(box.style.text_align),
                                  options.text_measure,
                                  cached_layout);
        }
        push_text_decorations(display_list, box, box.rect);
    }
}

LayerReasons layer_reasons_for(const LayoutBox& box, bool root) {
    LayerReasons reasons = LayerReasonNone;
    if (root) {
        reasons |= LayerReasonRoot;
    }
    if (has_overflow_clip(box.style)) {
        reasons |= LayerReasonOverflowClip;
    }
    if (box.style.opacity < 0.999F) {
        reasons |= LayerReasonOpacity;
    }
    if (has_transform(box.style)) {
        reasons |= LayerReasonTransform;
    }
    if (is_positioned(box.style)) {
        reasons |= LayerReasonPositioned;
    }
    if (!box.style.z_index_auto) {
        reasons |= LayerReasonZIndex;
    }
    if (has_shadow(box.style)) {
        reasons |= LayerReasonShadow;
    }
    if ((has_corner_radius(box.style.border_radius) || box.style.border_radius_percent >= 0 ||
         box.style.border_radius_axes_specified) && has_overflow_clip(box.style)) {
        reasons |= LayerReasonRoundedClip;
    }
    return reasons;
}

LayerType layer_type_for(LayerReasons reasons) {
    if ((reasons & LayerReasonRoot) != 0U) {
        return LayerType::Root;
    }
    if ((reasons & (LayerReasonOpacity | LayerReasonTransform)) != 0U) {
        return LayerType::Composited;
    }
    if ((reasons & (LayerReasonPositioned | LayerReasonZIndex)) != 0U) {
        return LayerType::Stacking;
    }
    if ((reasons & (LayerReasonOverflowClip | LayerReasonRoundedClip)) != 0U) {
        return LayerType::Clip;
    }
    return LayerType::Paint;
}

bool needs_own_layer(LayerReasons reasons) {
    return (reasons & ~static_cast<LayerReasons>(LayerReasonRoot)) != 0U;
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

Color with_opacity(Color color, float opacity) {
    const int alpha = static_cast<int>(static_cast<float>(color.a) * std::max(0.0F, std::min(1.0F, opacity)));
    color.a = static_cast<std::uint8_t>(std::max(0, std::min(255, alpha)));
    return color;
}

struct FlattenAffineTransform {
    float xx = 1.0F;
    float xy = 0.0F;
    float yx = 0.0F;
    float yy = 1.0F;
    float tx = 0.0F;
    float ty = 0.0F;
};

FlattenAffineTransform multiply_transform(const FlattenAffineTransform& outer,
                                          const FlattenAffineTransform& inner) {
    return {
        outer.xx * inner.xx + outer.xy * inner.yx,
        outer.xx * inner.xy + outer.xy * inner.yy,
        outer.yx * inner.xx + outer.yy * inner.yx,
        outer.yx * inner.xy + outer.yy * inner.yy,
        outer.xx * inner.tx + outer.xy * inner.ty + outer.tx,
        outer.yx * inner.tx + outer.yy * inner.ty + outer.ty,
    };
}

FlattenAffineTransform layer_affine_transform(const LayerNode& layer,
                                              int translated_x,
                                              int translated_y) {
    constexpr float kPi = 3.14159265358979323846F;
    const float radians = layer.transform.rotate_degrees * kPi / 180.0F;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const float origin_x = static_cast<float>(safe_add(layer.bounds.x, translated_x)) +
        static_cast<float>(layer.bounds.width) * static_cast<float>(layer.transform_origin_x_percent) / 100.0F;
    const float origin_y = static_cast<float>(safe_add(layer.bounds.y, translated_y)) +
        static_cast<float>(layer.bounds.height) * static_cast<float>(layer.transform_origin_y_percent) / 100.0F;
    const float xx = cosine * layer.transform.scale_x;
    const float xy = -sine * layer.transform.scale_y;
    const float yx = sine * layer.transform.scale_x;
    const float yy = cosine * layer.transform.scale_y;
    return {xx, xy, yx, yy, origin_x - xx * origin_x - xy * origin_y,
            origin_y - yx * origin_x - yy * origin_y};
}

bool identity_transform(const FlattenAffineTransform& transform) {
    return std::abs(transform.xx - 1.0F) < 0.0001F && std::abs(transform.xy) < 0.0001F &&
        std::abs(transform.yx) < 0.0001F && std::abs(transform.yy - 1.0F) < 0.0001F &&
        std::abs(transform.tx) < 0.0001F && std::abs(transform.ty) < 0.0001F;
}

std::int32_t fixed_transform_value(float value) {
    constexpr float kScale = 1024.0F;
    constexpr float kMaximumFixed = 64.0F * 1024.0F * 1024.0F;
    if (!std::isfinite(value)) {
        return 0;
    }
    const float scaled = std::max(-kMaximumFixed, std::min(kMaximumFixed, std::round(value * kScale)));
    return static_cast<std::int32_t>(scaled);
}

void append_flattened_command(DisplayList& output,
                              const DisplayCommand& command,
                              Rect clip,
                              bool has_clip,
                              std::uint32_t transform_source_clip_index,
                              float opacity,
                              int translate_x,
                              int translate_y,
                              const FlattenAffineTransform& transform,
                              std::size_t max_display_commands,
                              std::vector<std::uint32_t>* display_clip_indices = nullptr,
                              std::uint32_t clip_index = kNoFlattenedClip) {
    if (output.size() >= max_display_commands) {
        return;
    }
    DisplayCommand flattened = command;
    translate_display_command(flattened, translate_x, translate_y);
    const bool has_affine_transform = !identity_transform(transform);
    // A frame clip is applied in destination space by the value-frame
    // Frame clips are consumed in destination space. A v4 source-clip index
    // separately preserves the current transformed layer's pre-transform
    // overflow clip without cropping away its transparent sampling fringe.
    if (has_clip && !has_affine_transform && display_clip_indices == nullptr) {
        flattened.rect = intersect_rect(flattened.rect, clip);
        if (empty_rect(flattened.rect)) {
            return;
        }
    }
    flattened.color = with_opacity(flattened.color, opacity);
    flattened.color2 = with_opacity(flattened.color2, opacity);
    if (has_affine_transform) {
        flattened.transform.enabled = true;
        flattened.transform.xx_1024 = fixed_transform_value(transform.xx);
        flattened.transform.xy_1024 = fixed_transform_value(transform.xy);
        flattened.transform.yx_1024 = fixed_transform_value(transform.yx);
        flattened.transform.yy_1024 = fixed_transform_value(transform.yy);
        flattened.transform.tx_1024 = fixed_transform_value(transform.tx);
        flattened.transform.ty_1024 = fixed_transform_value(transform.ty);
        flattened.transform.source_clip_index = transform_source_clip_index > std::numeric_limits<std::uint16_t>::max()
            ? 0xffffU : static_cast<std::uint16_t>(transform_source_clip_index);
    }
    if (flattened.color.a == 0 &&
        ((flattened.type() != DisplayCommandType::LinearGradient &&
          flattened.type() != DisplayCommandType::ConicGradient &&
          flattened.type() != DisplayCommandType::RadialGradient) || flattened.color2.a == 0)) {
        return;
    }
    output.push_back(std::move(flattened));
    if (display_clip_indices != nullptr) {
        display_clip_indices->push_back(clip_index);
    }
}

void flatten_layer(const LayerNode& layer,
                   DisplayList& output,
                   Rect clip,
                   bool has_clip,
                   float opacity,
                   int translate_x,
                   int translate_y,
                   FlattenAffineTransform transform,
                   std::size_t max_display_commands,
                   DiagnosticSink* diagnostics,
                   bool& display_budget_reported,
                   FlattenedLayerTree* metadata = nullptr) {
    struct PendingLayer {
        const LayerNode* layer = nullptr;
        Rect clip;
        bool has_clip = false;
        float opacity = 1.0F;
        int translate_x = 0;
        int translate_y = 0;
        FlattenAffineTransform transform;
        std::uint32_t transform_source_clip_index = kNoFlattenedClip;
        std::uint32_t clip_index = kNoFlattenedClip;
    };

    std::vector<PendingLayer> pending;
    pending.push_back(PendingLayer{&layer, clip, has_clip, opacity, translate_x, translate_y, transform,
                                   kNoFlattenedClip, kNoFlattenedClip});
    while (!pending.empty()) {
        const PendingLayer current = pending.back();
        pending.pop_back();
        if (output.size() >= max_display_commands) {
            if (!display_budget_reported) {
                report_diagnostic(diagnostics,
                                  DiagnosticStage::LayerTree,
                                  DiagnosticSeverity::Warning,
                                  "display-command-limit",
                                  "Flattened display command budget was reached; remaining paint commands were skipped",
                                  "Increase max_display_commands for complex pages.");
                display_budget_reported = true;
            }
            return;
        }
        const LayerNode& current_layer = *current.layer;
        const float layer_opacity = current.opacity * current_layer.opacity;
        const int layer_translate_x = safe_add(current.translate_x,
                                               round_float_to_int(current_layer.transform.translate_x));
        const int layer_translate_y = safe_add(current.translate_y,
                                               round_float_to_int(current_layer.transform.translate_y));
        const FlattenAffineTransform layer_transform = multiply_transform(
            current.transform, layer_affine_transform(current_layer, layer_translate_x, layer_translate_y));
        Rect current_clip = current.clip;
        bool current_has_clip = current.has_clip;
        std::uint32_t transform_source_clip_index = current.transform_source_clip_index;
        std::uint32_t current_clip_index = current.clip_index;
        if (current_layer.has_clip) {
            const Rect translated_layer_clip{
                safe_add(current_layer.clip_rect.x, layer_translate_x),
                safe_add(current_layer.clip_rect.y, layer_translate_y),
                current_layer.clip_rect.width,
                current_layer.clip_rect.height,
            };
            current_clip = current_has_clip ? intersect_rect(current_clip, translated_layer_clip) : translated_layer_clip;
            current_has_clip = true;
            if (empty_rect(current_clip)) {
                continue;
            }
            if (metadata != nullptr) {
                // The layer that introduces a transform is clipped again in
                // destination space by SoftwareCompositor. Clips introduced
                // below that transform have already been rasterized into its
                // source surface and must not be reapplied after the affine
                // mapping.
                if (identity_transform(current.transform)) {
                    current_clip_index = static_cast<std::uint32_t>(metadata->clips.size());
                    metadata->clips.push_back(FlattenedClip{
                        translated_layer_clip,
                        current_layer.clip_border_radius,
                        current.clip_index,
                        effective_vertical_radius(current_layer.clip_border_radius,
                                                  current_layer.clip_border_radius_y)});
                }
                if (!identity_transform(layer_transform)) {
                    transform_source_clip_index = static_cast<std::uint32_t>(metadata->clips.size());
                    metadata->clips.push_back(FlattenedClip{
                        translated_layer_clip,
                        current_layer.clip_border_radius,
                        current.transform_source_clip_index,
                        effective_vertical_radius(current_layer.clip_border_radius,
                                                  current_layer.clip_border_radius_y)});
                }
            }
        }
        for (const DisplayCommand& command : current_layer.display_list) {
            append_flattened_command(output,
                                     command,
                                     current_clip,
                                     current_has_clip,
                                     transform_source_clip_index,
                                     layer_opacity,
                                     layer_translate_x,
                                     layer_translate_y,
                                     layer_transform,
                                     max_display_commands,
                                     metadata != nullptr ? &metadata->display_clip_indices : nullptr,
                                     current_clip_index);
            if (output.size() >= max_display_commands) {
                break;
            }
        }
        for (auto it = current_layer.children.rbegin(); it != current_layer.children.rend(); ++it) {
            pending.push_back(PendingLayer{it->get(),
                                           current_clip,
                                           current_has_clip,
                                           layer_opacity,
                                            layer_translate_x,
                                            layer_translate_y,
                                            layer_transform,
                                            transform_source_clip_index,
                                            current_clip_index});
        }
    }
}

void sort_layer_children(LayerNode& layer) {
    std::vector<LayerNode*> pending;
    pending.push_back(&layer);
    while (!pending.empty()) {
        LayerNode* current = pending.back();
        pending.pop_back();
        std::stable_sort(current->children.begin(), current->children.end(),
            [](const LayerNodePtr& left, const LayerNodePtr& right) {
                if (left->z_index != right->z_index) {
                    return left->z_index < right->z_index;
                }
                return left->source_order < right->source_order;
            });
        for (const auto& child : current->children) {
            pending.push_back(child.get());
        }
    }
}

} // namespace

void LayerNodeDeleter::operator()(LayerNode* layer) const {
    if (!arena_owned) {
        delete layer;
    }
}

LayerTreeBuilder::LayerTreeBuilder(LayerTreeBuilderOptions options)
    : options_(options) {}

LayerNodePtr LayerTreeBuilder::build(const LayoutBox& root) const {
    return build_with_arena(root, nullptr);
}

LayerNodePtr LayerTreeBuilder::build(const LayoutBox& root, MonotonicArena& arena) const {
    return build_with_arena(root, &arena);
}

LayerNodePtr LayerTreeBuilder::build_with_arena(const LayoutBox& root, MonotonicArena* arena) const {
    ImageResolutionBuildState image_resolution_state;
    ImageResolutionBuildState* previous_image_resolution_state = active_image_resolution_state;
    active_image_resolution_state = &image_resolution_state;
    struct RestoreImageResolutionState {
        ImageResolutionBuildState*& active;
        ImageResolutionBuildState* previous;
        ~RestoreImageResolutionState() { active = previous; }
    } restore_image_resolution_state{active_image_resolution_state, previous_image_resolution_state};

    std::size_t remaining_commands = std::max<std::size_t>(1, options_.max_display_commands);
    bool display_budget_reported = false;
    auto root_layer = make_layer_node(arena);
    root_layer->type = LayerType::Root;
    root_layer->reasons = layer_reasons_for(root, true);
    root_layer->box = &root;
    root_layer->bounds = root.rect;
    root_layer->clip_rect = root.rect;
    root_layer->has_clip = has_overflow_clip(root.style);
    root_layer->clip_border_radius = root_layer->has_clip ? resolved_border_radius(root) : 0;
    root_layer->clip_border_radius_y = root_layer->has_clip ? resolved_border_radius_y(root) : 0;
    root_layer->opacity = root.style.opacity;
    root_layer->transform = parsed_transform_or_identity(root.style, options_.diagnostics);
    root_layer->transform_origin_x_percent = root.style.transform_origin_x_percent;
    root_layer->transform_origin_y_percent = root.style.transform_origin_y_percent;
    root_layer->has_transform = has_transform(root.style);
    root_layer->scroll_y = resolved_scroll_y_for(root, options_);
    root_layer->max_scroll_y = max_scroll_y_for(root);
    root_layer->z_index = root.style.z_index;
    root_layer->source_order = 0;

    if (!root.style.visibility_hidden) {
        const std::size_t command_begin = root_layer->display_list.size();
        paint_box_self(root, root_layer->display_list, options_, remaining_commands);
        trim_display_list(root_layer->display_list, command_begin, remaining_commands, display_budget_reported);
        stamp_trace_owner(root_layer->display_list, command_begin, trace_owner_token_for(root, options_));
    }
    build_children(root, *root_layer, arena, root.rect, remaining_commands, display_budget_reported);
    if (!root.style.visibility_hidden) {
        const std::size_t command_begin = root_layer->display_list.size();
        paint_generated_inline_content(root, root_layer->display_list, CssPseudoElement::After);
        trim_display_list(root_layer->display_list, command_begin, remaining_commands, display_budget_reported);
        stamp_trace_owner(root_layer->display_list, command_begin, trace_owner_token_for(root, options_));
    }
    sort_layer_children(*root_layer);
    return root_layer;
}

DisplayList LayerTreeBuilder::flatten(const LayerNode& root) const {
    DisplayList output;
    flatten_into(root, output);
    return output;
}

void LayerTreeBuilder::flatten_into(const LayerNode& root, DisplayList& output) const {
    output.clear();
    const std::size_t max_display_commands = std::max<std::size_t>(1, options_.max_display_commands);
    const std::size_t required_capacity = std::min(count_layer_display_commands(root), max_display_commands);
    if (output.capacity() < required_capacity) {
        output.reserve(required_capacity);
    }
    bool display_budget_reported = false;
    flatten_layer(root, output, Rect{}, false, 1.0F, 0, 0, {}, max_display_commands,
                  options_.diagnostics, display_budget_reported);
}

FlattenedLayerTree LayerTreeBuilder::flatten_with_clip_metadata(const LayerNode& root) const {
    FlattenedLayerTree output;
    const std::size_t max_display_commands = std::max<std::size_t>(1, options_.max_display_commands);
    const std::size_t required_capacity = std::min(count_layer_display_commands(root), max_display_commands);
    output.display_list.reserve(required_capacity);
    output.display_clip_indices.reserve(required_capacity);
    output.clips.reserve(count_layers(root));
    bool display_budget_reported = false;
    flatten_layer(root,
                  output.display_list,
                  Rect{},
                  false,
                  1.0F,
                  0,
                  0,
                  {},
                  max_display_commands,
                  options_.diagnostics,
                  display_budget_reported,
                  &output);
    return output;
}

void LayerTreeBuilder::trim_display_list(DisplayList& display_list,
                                         std::size_t command_begin,
                                         std::size_t& remaining_commands,
                                         bool& budget_reported) const {
    command_begin = std::min(command_begin, display_list.size());
    const std::size_t added_commands = display_list.size() - command_begin;
    const std::size_t kept_commands = std::min(added_commands, remaining_commands);
    if (kept_commands < added_commands) {
        display_list.resize(command_begin + kept_commands);
        if (!budget_reported) {
        report_diagnostic(options_.diagnostics,
                          DiagnosticStage::LayerTree,
                          DiagnosticSeverity::Warning,
                          "display-command-limit",
                          "Retained-tree display command budget was reached; later commands were clipped",
                          "Increase max_display_commands for complex pages.");
            budget_reported = true;
        }
    }
    remaining_commands -= kept_commands;
}

void LayerTreeBuilder::build_children(const LayoutBox& box,
                                      LayerNode& layer,
                                      MonotonicArena* arena,
                                      const Rect& viewport,
                                      std::size_t& remaining_commands,
                                      bool& display_budget_reported) const {
#if !JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
    (void)viewport;
#endif
    struct PendingBox {
        const LayoutBox* box = nullptr;
        LayerNode* layer = nullptr;
        int scroll_y = 0;
        bool exit = false;
    };

    std::size_t next_source_order = layer.source_order + 1;
    std::size_t layer_count = 1;
    bool layer_budget_reported = false;
    const std::size_t max_layers = std::max<std::size_t>(1, options_.max_layers);

    std::vector<PendingBox> pending;
    pending.reserve(box.children.size());
    const auto enqueue_children = [&pending](const LayoutBox& parent,
                                             LayerNode* parent_layer,
                                             int parent_scroll_y) {
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        const std::vector<const LayoutBox*> ordered = ordered_flex_paint_children(parent);
#else
        const std::vector<const LayoutBox*> ordered;
#endif
        if (ordered.empty()) {
            for (auto it = parent.children.rbegin(); it != parent.children.rend(); ++it) {
                pending.push_back(PendingBox{it->get(), parent_layer, parent_scroll_y, false});
            }
            return;
        }
        for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
            pending.push_back(PendingBox{*it, parent_layer, parent_scroll_y, false});
        }
    };
    enqueue_children(box, &layer, layer.scroll_y);

    while (!pending.empty()) {
        const PendingBox current = pending.back();
        pending.pop_back();
        const LayoutBox* current_box = current.box;
        LayerNode& current_layer = *current.layer;
        if (current.exit) {
            if (!current_box->style.visibility_hidden) {
                const std::size_t command_begin = current_layer.display_list.size();
                paint_generated_inline_content(*current_box, current_layer.display_list, CssPseudoElement::After);
                translate_display_commands(current_layer.display_list, command_begin, 0, -current.scroll_y);
                trim_display_list(current_layer.display_list,
                                  command_begin,
                                  remaining_commands,
                                  display_budget_reported);
                stamp_trace_owner(current_layer.display_list,
                                  command_begin,
                                  trace_owner_token_for(*current_box, options_));
            }
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
            if (layer_count < max_layers && current_box->node != nullptr &&
                form_control_kind(*current_box->node) == FormControlKind::Select &&
                select_popup_is_open(*current_box->node)) {
                DisplayList popup_commands;
                popup_commands.reserve(2 + static_cast<std::size_t>(
                    std::max(0, form_control_option_count(*current_box->node)) * 2));
                Rect popup_bounds;
                if (paint_select_popup(*current_box,
                                       viewport,
                                       current.scroll_y,
                                       options_,
                                       popup_commands,
                                       popup_bounds)) {
                    auto popup_layer = make_layer_node(arena);
                    popup_layer->type = LayerType::Stacking;
                    popup_layer->reasons = LayerReasonZIndex | LayerReasonTransientOverlay;
                    popup_layer->box = current_box;
                    popup_layer->bounds = popup_bounds;
                    popup_layer->opacity = 1.0F;
                    popup_layer->z_index = current_layer.z_index;
                    popup_layer->source_order = next_source_order++;
                    popup_layer->display_list = std::move(popup_commands);
                    trim_display_list(popup_layer->display_list,
                                      0,
                                      remaining_commands,
                                      display_budget_reported);
                    stamp_trace_owner(popup_layer->display_list,
                                      0,
                                      trace_owner_token_for(*current_box, options_));
                    current_layer.children.push_back(std::move(popup_layer));
                    ++layer_count;
                }
            }
#endif
            if (options_.paint_scroll_indicators &&
                current_layer.box == current_box &&
                current_layer.max_scroll_y > 0 &&
                layer_count < max_layers) {
                DisplayList indicator_commands;
                indicator_commands.reserve(2);
                if (paint_scroll_indicator(*current_box,
                                           current_layer.scroll_y,
                                           current_layer.max_scroll_y,
                                           indicator_commands)) {
                    auto indicator_layer = make_layer_node(arena);
                    indicator_layer->type = LayerType::Paint;
                    indicator_layer->box = nullptr;
                    indicator_layer->bounds = current_box->rect;
                    indicator_layer->opacity = 1.0F;
                    indicator_layer->source_order = next_source_order++;
                    indicator_layer->display_list = std::move(indicator_commands);
                    trim_display_list(indicator_layer->display_list,
                                      0,
                                      remaining_commands,
                                      display_budget_reported);
                    current_layer.children.push_back(std::move(indicator_layer));
                    ++layer_count;
                }
            }
            continue;
        }

        const LayerReasons reasons = layer_reasons_for(*current_box, false);
        LayerNode* target_layer = &current_layer;
        const int own_scroll_y = resolved_scroll_y_for(*current_box, options_);
        if (needs_own_layer(reasons) && layer_count < max_layers) {
            auto child_layer = make_layer_node(arena);
            target_layer = child_layer.get();
            ++layer_count;
            child_layer->type = layer_type_for(reasons);
            child_layer->reasons = reasons;
            child_layer->box = current_box;
            child_layer->bounds = current_box->rect;
            child_layer->clip_rect = current_box->rect;
            child_layer->has_clip = (reasons & LayerReasonOverflowClip) != 0U;
            child_layer->clip_border_radius = (reasons & LayerReasonRoundedClip) != 0U
                ? resolved_border_radius(*current_box)
                : 0;
            child_layer->clip_border_radius_y = (reasons & LayerReasonRoundedClip) != 0U
                ? resolved_border_radius_y(*current_box)
                : 0;
            child_layer->opacity = current_box->style.opacity;
            child_layer->transform = parsed_transform_or_identity(current_box->style, options_.diagnostics);
            child_layer->transform.translate_y -= static_cast<float>(current.scroll_y);
            child_layer->transform_origin_x_percent = current_box->style.transform_origin_x_percent;
            child_layer->transform_origin_y_percent = current_box->style.transform_origin_y_percent;
            child_layer->has_transform = has_transform(current_box->style);
            child_layer->scroll_y = own_scroll_y;
            child_layer->max_scroll_y = max_scroll_y_for(*current_box);
            child_layer->z_index = current_box->style.z_index_auto ? 0 : current_box->style.z_index;
            child_layer->source_order = next_source_order++;
            current_layer.children.push_back(std::move(child_layer));
        } else if (needs_own_layer(reasons) && !layer_budget_reported) {
            report_diagnostic(options_.diagnostics,
                              DiagnosticStage::LayerTree,
                              DiagnosticSeverity::Warning,
                              "layer-limit",
                              "Layer budget was reached; later stacking/clip/composited boxes were folded into parent layers",
                              "This preserves paint output where possible but may reduce clipping or stacking fidelity.");
            layer_budget_reported = true;
        }

        if (!current_box->style.visibility_hidden) {
            const std::size_t command_begin = target_layer->display_list.size();
            paint_box_self(*current_box, target_layer->display_list, options_, remaining_commands);
            if (target_layer == &current_layer) {
                translate_display_commands(target_layer->display_list, command_begin, 0, -current.scroll_y);
            }
            trim_display_list(target_layer->display_list,
                              command_begin,
                              remaining_commands,
                              display_budget_reported);
            stamp_trace_owner(target_layer->display_list,
                              command_begin,
                              trace_owner_token_for(*current_box, options_));
        }
        const int child_scroll_y = target_layer == &current_layer
            ? safe_add(current.scroll_y, own_scroll_y)
            : own_scroll_y;
        pending.push_back(PendingBox{current_box, target_layer, child_scroll_y, true});
        enqueue_children(*current_box, target_layer, child_scroll_y);
    }
}

LayerNodePtr LayerTreeBuilder::make_layer_node(MonotonicArena* arena) const {
    if (arena == nullptr) {
        return LayerNodePtr(new LayerNode, LayerNodeDeleter{false});
    }
    return LayerNodePtr(&arena->create<LayerNode>(), LayerNodeDeleter{true});
}

bool apply_opacity_overrides_to_layer_tree(LayerNode& root,
                                           const std::vector<StyleOverride>& overrides,
                                           LayerTreeOverrideScratch& scratch) {
    if (overrides.empty()) {
        return false;
    }

    auto find_layer = [&](const Node* node) -> LayerNode* {
        scratch.clear();
        scratch.pending.push_back(&root);
        while (!scratch.pending.empty()) {
            LayerNode* current = scratch.pending.back();
            scratch.pending.pop_back();
            if (current->box != nullptr && current->box->node == node) {
                return current;
            }
            for (const LayerNodePtr& child : current->children) {
                scratch.pending.push_back(child.get());
            }
        }
        return nullptr;
    };

    // Validate every override first so a rejected batch cannot leave a partial frame.
    for (const StyleOverride& override : overrides) {
        if (override.node == nullptr || !override.has_opacity || override.has_color ||
            override.has_background_color || override.has_transform || find_layer(override.node) == nullptr) {
            scratch.clear();
            return false;
        }
    }
    for (const StyleOverride& override : overrides) {
        LayerNode* layer = find_layer(override.node);
        layer->opacity = override.opacity;
    }
    scratch.clear();
    return true;
}

std::size_t count_layers(const LayerNode& layer) {
    std::size_t count = 0;
    std::vector<const LayerNode*> pending;
    pending.push_back(&layer);
    while (!pending.empty()) {
        const LayerNode* current = pending.back();
        pending.pop_back();
        ++count;
        for (const auto& child : current->children) {
            pending.push_back(child.get());
        }
    }
    return count;
}

std::size_t count_layer_display_commands(const LayerNode& layer) {
    std::size_t count = 0;
    std::vector<const LayerNode*> pending;
    pending.push_back(&layer);
    while (!pending.empty()) {
        const LayerNode* current = pending.back();
        pending.pop_back();
        count += current->display_list.size();
        for (const auto& child : current->children) {
            pending.push_back(child.get());
        }
    }
    return count;
}

} // namespace jellyframe
