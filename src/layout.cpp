#include "render_core/layout.h"

#include "render_core/form_control.h"
#include "computed_size_geometry.h"
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
#include "intrinsic_source_query.h"
#endif
#include "layout_mutation_journal.h"
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
#include "flex_length_resolution.h"
#include "flex_auto_min.h"
#endif
#include "render_core/prepared_text.h"
#include "render_core/text_normalization.h"
#include "render_core/text_scan.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <sstream>
#include <vector>

namespace jellyframe {

#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
std::vector<const LayoutBox*> ordered_flex_children_for_order(const LayoutBox& box,
                                                              bool include_out_of_flow);
#endif

namespace {

int clamp_layout_value(std::int64_t value) {
    return static_cast<int>(std::clamp<std::int64_t>(value,
                                                     std::numeric_limits<int>::min(),
                                                     std::numeric_limits<int>::max()));
}

int q1000_ceil_pixels(std::int32_t value) {
    return value <= 0 ? 0 : clamp_layout_value((static_cast<std::int64_t>(value) + 999) / 1000);
}

TextSystemStatus measure_core_text(const TextSystem& text_system, std::string_view text,
                                   const Style& style, TextRunMetrics& metrics) {
    if (style.letter_spacing != 0) return TextSystemStatus::UnsupportedStyle;
    if (style.font_size < 0 || style.font_size > std::numeric_limits<std::int32_t>::max() / 1000)
        return TextSystemStatus::InvalidRequest;
    std::uint32_t family_id = 0;
    if (!text_system.resolve_family_hash(style.font_family_hash, family_id))
        return TextSystemStatus::InvalidFontSet;
    TextFontRequest request;
    request.families = &family_id;
    request.family_count = 1;
    request.size_q1000 = style.font_size * 1000;
    request.weight = static_cast<std::uint16_t>(std::clamp(style.font_weight, 1, 1000));
    return text_system.measure(text, request, metrics);
}

int bounded_add(int left, int right) {
    return clamp_layout_value(static_cast<std::int64_t>(left) + right);
}

int bounded_subtract(int left, int right) {
    return clamp_layout_value(static_cast<std::int64_t>(left) - right);
}

int bounded_non_negative_add(int left, int right) {
    return std::max(0, bounded_add(left, right));
}

int bounded_non_negative_subtract(int left, int right) {
    return std::max(0, bounded_subtract(left, right));
}

[[maybe_unused]] int bounded_non_negative_multiply(int left, int right) {
    if (left <= 0 || right <= 0) {
        return 0;
    }
    return clamp_layout_value(static_cast<std::int64_t>(left) * right);
}

int horizontal_edges(const EdgeSizes& edges) {
    return bounded_add(edges.left, edges.right);
}

int vertical_edges(const EdgeSizes& edges) {
    return bounded_add(edges.top, edges.bottom);
}

int resolve_percent(int basis, int percent) {
    const std::int64_t non_negative_basis = std::max(0, basis);
    const std::int64_t scaled = non_negative_basis * std::max(0, percent) + 50;
    return static_cast<int>(std::clamp<std::int64_t>(scaled / 100, 0,
                                                     std::numeric_limits<int>::max()));
}

std::string quote_detail_value(const std::string& value, std::size_t max_chars = 48) {
    std::string output;
    output.reserve(std::min(value.size(), max_chars) + 2);
    output.push_back('"');
    std::size_t emitted = 0;
    for (char ch : value) {
        if (emitted >= max_chars) {
            output += "...";
            break;
        }
        if (ch == '"' || ch == '\\') {
            output.push_back('\\');
            output.push_back(ch);
        } else if (ch == '\n' || ch == '\r' || ch == '\t') {
            output.push_back(' ');
        } else {
            output.push_back(ch);
        }
        ++emitted;
    }
    output.push_back('"');
    return output;
}

std::string text_overflow_detail(const LayoutBox& box,
                                 const std::string& text,
                                 int measured_width,
                                 int available_width,
                                 int content_width,
                                 int text_indent) {
    std::ostringstream detail;
    detail << "text=" << quote_detail_value(text)
           << " measuredWidth=" << measured_width
           << " availableWidth=" << available_width
           << " contentWidth=" << content_width
           << " textIndent=" << text_indent
           << " fontSize=" << box.style.font_size
           << " fontWeight=" << box.style.font_weight
           << " node=" << quote_detail_value(dom_node_label(box.node), 48)
           << " path=" << quote_detail_value(dom_node_path(box.node), 160);
    return detail.str();
}

int resolved_content_width(const Style& style, int containing_width) {
    if (style.width_percent >= 0) {
        const int border_box_width = resolve_percent(containing_width, style.width_percent);
        return style.box_sizing_border_box
            ? bounded_non_negative_subtract(
                  bounded_non_negative_subtract(border_box_width, horizontal_edges(style.border_width)),
                  horizontal_edges(style.padding))
            : border_box_width;
    }
    if (style.width >= 0) {
        return style.box_sizing_border_box
            ? bounded_non_negative_subtract(
                  bounded_non_negative_subtract(style.width, horizontal_edges(style.border_width)),
                  horizontal_edges(style.padding))
            : style.width;
    }
    return bounded_non_negative_subtract(
        bounded_non_negative_subtract(containing_width, horizontal_edges(style.border_width)),
        horizontal_edges(style.padding));
}

int resolved_min_width(const Style& style, int containing_width) {
    if (style.min_width_percent >= 0) {
        return resolve_percent(containing_width, style.min_width_percent);
    }
    return style.min_width;
}

int resolved_max_content_width(const Style& style, int containing_width) {
    int max_width = style.max_width;
    if (style.max_width_percent >= 0) {
        max_width = resolve_percent(containing_width, style.max_width_percent);
    }
    if (max_width < 0) {
        return -1;
    }
    return style.box_sizing_border_box
        ? bounded_non_negative_subtract(
              bounded_non_negative_subtract(max_width, horizontal_edges(style.border_width)),
              horizontal_edges(style.padding))
        : max_width;
}

int specified_content_height(const Style& style, int containing_height) {
    int height = style.height;
    if (style.height_percent >= 0) {
        height = resolve_percent(containing_height, style.height_percent);
    }
    if (height < 0) {
        return -1;
    }
    return style.box_sizing_border_box
        ? bounded_non_negative_subtract(
              bounded_non_negative_subtract(height, vertical_edges(style.border_width)),
              vertical_edges(style.padding))
        : height;
}

int specified_content_min_height(const Style& style, int containing_height) {
    int min_height = style.min_height;
    if (style.min_height_percent >= 0) {
        min_height = resolve_percent(containing_height, style.min_height_percent);
    }
    if (min_height < 0) {
        return -1;
    }
    return style.box_sizing_border_box
        ? bounded_non_negative_subtract(
              bounded_non_negative_subtract(min_height, vertical_edges(style.border_width)),
              vertical_edges(style.padding))
        : min_height;
}

int resolved_max_content_height(const Style& style, int containing_height) {
    int max_height = style.max_height;
    if (style.max_height_percent >= 0) {
        max_height = resolve_percent(containing_height, style.max_height_percent);
    }
    if (max_height < 0) {
        return -1;
    }
    return style.box_sizing_border_box
        ? bounded_non_negative_subtract(
              bounded_non_negative_subtract(max_height, vertical_edges(style.border_width)),
              vertical_edges(style.padding))
        : max_height;
}

bool has_aspect_ratio(const Style& style) {
    return style.aspect_ratio_width > 0 && style.aspect_ratio_height > 0;
}

bool is_out_of_flow_positioned(const Style& style) {
    return style.position_type == PositionType::Absolute ||
        style.position_type == PositionType::Fixed;
}

void shift_box(LayoutBox& box, int dx, int dy);

struct ResolvedInset {
    std::int64_t units = 0;
    bool specified = false;
};

ResolvedInset resolve_inset(const Style& style, detail::SizeProperty property,
                            std::int64_t basis, bool definite = true) {
    constexpr int Style::* pixels[] = {&Style::inset_top, &Style::inset_right,
        &Style::inset_bottom, &Style::inset_left};
    constexpr bool Style::* specified[] = {&Style::inset_top_specified, &Style::inset_right_specified,
        &Style::inset_bottom_specified, &Style::inset_left_specified};
    const auto index = static_cast<unsigned>(property) - static_cast<unsigned>(detail::SizeProperty::InsetTop);
    if (index >= 4 || !(style.*specified[index])) return {};
    detail::SizeKind kind;
    std::int64_t length = 0, percentage = 0;
    if (!style.computed_sizes.get(property, kind, length, percentage))
        return {static_cast<std::int64_t>(style.*pixels[index]) * 1000, true};
    if (kind == detail::SizeKind::Pixels) return {length, true};
    // Relative vertical percentages compute to auto when the containing
    // height is indefinite; absolute/fixed containing padding boxes are definite.
    if (!definite) return {};
    if (kind == detail::SizeKind::Percent) { percentage = length; length = 0; }
    basis = std::clamp<std::int64_t>(basis, 0, detail::kDimensionPrecisionMaximum);
    const auto contribution = basis * percentage / 100000;
    return {std::clamp<std::int64_t>(length + contribution,
        -detail::kDimensionPrecisionMaximum, detail::kDimensionPrecisionMaximum), true};
}

#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
struct FlexLayoutItem {
    LayoutBox* child = nullptr;
    int base_width = 0;
    int minimum_width = 0;
    int intrinsic_minimum_width = 0;
    int target_width = 0;
    bool force_width = false;
    bool shrink_frozen = false;
    int probe_width = 0;
    bool probe_forced_width = false;
};

int flex_grow_factor(const FlexLayoutItem& item) {
    return item.child == nullptr ? 0 : std::max(0, item.child->style.flex_grow);
}

std::int64_t flex_shrink_weight(const FlexLayoutItem& item) {
    if (item.child == nullptr) {
        return 0;
    }
    const int shrink = std::max(0, item.child->style.flex_shrink);
    return static_cast<std::int64_t>(shrink) * std::max(1, item.base_width);
}

void distribute_flex_row_widths(std::vector<FlexLayoutItem>& items,
                                int free_space,
                                int total_grow,
                                std::int64_t total_shrink_weight) {
    if (free_space > 0 && total_grow > 0) {
        int remaining_space = free_space;
        int remaining_grow = total_grow;
        for (FlexLayoutItem& item : items) {
            const int grow = flex_grow_factor(item);
            if (grow <= 0) {
                continue;
            }
            const int share = remaining_grow == grow
                ? remaining_space
                : static_cast<int>((static_cast<std::int64_t>(remaining_space) * grow) / remaining_grow);
            remaining_space -= share;
            remaining_grow -= grow;
            item.target_width = std::max(0, bounded_add(item.base_width, share));
            item.force_width = true;
        }
    } else if (free_space < 0 && total_shrink_weight > 0) {
        int remaining_deficit = bounded_subtract(0, free_space);
        std::int64_t remaining_weight = total_shrink_weight;
        while (remaining_deficit > 0 && remaining_weight > 0) {
            int proposed_deficit = remaining_deficit;
            std::int64_t proposed_weight = remaining_weight;
            bool froze_item = false;

            for (FlexLayoutItem& item : items) {
                if (item.shrink_frozen) continue;
                const std::int64_t weight = flex_shrink_weight(item);
                if (weight <= 0) continue;

                const int share = proposed_weight == weight
                    ? proposed_deficit
                    : static_cast<int>((static_cast<std::int64_t>(proposed_deficit) * weight) /
                        proposed_weight);
                proposed_deficit -= share;
                proposed_weight -= weight;

                const int shrink_capacity = std::max(0, bounded_subtract(item.base_width,
                    item.minimum_width));
                if (share > shrink_capacity) {
                    item.target_width = item.minimum_width;
                    item.force_width = true;
                    item.shrink_frozen = true;
                    remaining_deficit -= shrink_capacity;
                    remaining_weight -= weight;
                    froze_item = true;
                }
            }

            if (froze_item) continue;

            int final_deficit = remaining_deficit;
            std::int64_t final_weight = remaining_weight;
            for (FlexLayoutItem& item : items) {
                if (item.shrink_frozen) continue;
                const std::int64_t weight = flex_shrink_weight(item);
                if (weight <= 0) continue;
                const int share = final_weight == weight
                    ? final_deficit
                    : static_cast<int>((static_cast<std::int64_t>(final_deficit) * weight) /
                        final_weight);
                final_deficit -= share;
                final_weight -= weight;
                item.target_width = bounded_subtract(item.base_width, share);
                item.force_width = true;
            }
            break;
        }
    }
}

struct FlexJustifyPlacement {
    int cursor = 0;
    int extra_gap = 0;
};

FlexJustifyPlacement flex_justify_placement(JustifyContent justify_content,
                                            int main_start,
                                            int main_size,
                                            int total_child_size,
                                            int in_flow_count) {
    FlexJustifyPlacement placement;
    placement.cursor = main_start;
    const int free_space = bounded_non_negative_subtract(main_size, total_child_size);
    if (justify_content == JustifyContent::End) {
        placement.cursor = bounded_add(placement.cursor, free_space);
    } else if (justify_content == JustifyContent::Center) {
        placement.cursor = bounded_add(placement.cursor, free_space / 2);
    } else if (justify_content == JustifyContent::SpaceAround) {
        placement.extra_gap = in_flow_count == 0 ? 0 : free_space / in_flow_count;
        placement.cursor = bounded_add(placement.cursor, placement.extra_gap / 2);
    } else if (justify_content == JustifyContent::SpaceBetween && in_flow_count > 1) {
        placement.extra_gap = free_space / (in_flow_count - 1);
    } else if (justify_content == JustifyContent::SpaceEvenly && in_flow_count > 0) {
        placement.extra_gap = free_space / (in_flow_count + 1);
        placement.cursor = bounded_add(placement.cursor, placement.extra_gap);
    }
    return placement;
}

int flex_aligned_y(AlignItems align_items, int content_y, int container_height, int child_height) {
    if (align_items == AlignItems::Center) {
        return bounded_add(content_y,
                           std::max(0, bounded_subtract(container_height, child_height) / 2));
    }
    if (align_items == AlignItems::End) {
        return bounded_add(content_y, std::max(0, bounded_subtract(container_height, child_height)));
    }
    return content_y;
}

int flex_aligned_x(AlignItems align_items, int content_x, int container_width, int child_width) {
    if (align_items == AlignItems::Center) {
        return bounded_add(content_x,
                           std::max(0, bounded_subtract(container_width, child_width) / 2));
    }
    if (align_items == AlignItems::End) {
        return bounded_add(content_x, std::max(0, bounded_subtract(container_width, child_width)));
    }
    return content_x;
}

// Legacy contexts project the same computed basis at their existing pixel boundary.
int resolved_flex_basis_pixels(const Style& style, int main_size) {
    const auto units=detail::computed_size_units(style,detail::SizeProperty::FlexBasis,
        main_size<0 ? -1 : static_cast<std::int64_t>(main_size)*1000);
    return units<0 ? -1 : detail::project_size_edge(units);
}

AlignItems flex_cross_axis_alignment(const LayoutBox& child, AlignItems parent_alignment) {
    return child.style.align_self == AlignItems::Auto ? parent_alignment : child.style.align_self;
}

bool flex_cross_size_is_auto(const Style& style, bool row_direction) {
    return row_direction
        ? style.height < 0 && style.height_percent < 0
        : style.width < 0 && style.width_percent < 0;
}

int flex_bounded_sum(int first, int second) {
    return static_cast<int>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(first) + second,
        std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max()));
}

int flex_stretch_dimension(const Style& style, int outer_size, bool vertical) {
    const int margins = vertical
        ? flex_bounded_sum(style.margin.top, style.margin.bottom)
        : flex_bounded_sum(style.margin.left, style.margin.right);
    const int edges = vertical
        ? flex_bounded_sum(vertical_edges(style.border_width), vertical_edges(style.padding))
        : flex_bounded_sum(horizontal_edges(style.border_width), horizontal_edges(style.padding));
    const int target_outer_size = static_cast<int>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(outer_size) - margins, 0, std::numeric_limits<int>::max()));
    return style.box_sizing_border_box
        ? target_outer_size
        : bounded_non_negative_subtract(target_outer_size, edges);
}

void set_flex_stretch_cross_size(Style& style, int outer_size, bool row_direction) {
    if (row_direction) {
        style.height = flex_stretch_dimension(style, outer_size, true);
        style.height_percent = -1;
    } else {
        style.width = flex_stretch_dimension(style, outer_size, false);
        style.width_percent = -1;
    }
}

int flex_outer_cross_size(const LayoutBox& child, bool row_direction) {
    const int margins = row_direction
        ? flex_bounded_sum(child.style.margin.top, child.style.margin.bottom)
        : flex_bounded_sum(child.style.margin.left, child.style.margin.right);
    return flex_bounded_sum(row_direction ? child.rect.height : child.rect.width, margins);
}

std::vector<LayoutBox*> ordered_flex_children(LayoutBox& box) {
    const std::vector<const LayoutBox*> ordered_const = ordered_flex_children_for_order(box, false);
    std::vector<LayoutBox*> ordered;
    ordered.reserve(ordered_const.size());
    for (const LayoutBox* child : ordered_const) {
        ordered.push_back(const_cast<LayoutBox*>(child));
    }
    return ordered;
}

template <typename LayoutChildForWidth>
int layout_wrapped_flex_children(LayoutBox& box,
                                 int content_x,
                                 int content_y,
                                 int content_width,
                                 const LayoutChildForWidth& layout_child_for_width,
                                 const std::vector<LayoutBox*>& ordered_children) {
    struct FlexWrapLine {
        int y = 0;
        int height = 0;
        std::vector<LayoutBox*> children;
    };
    const int fixed_height = specified_content_height(box.style, 0);
    const int minimum_height = std::max(0, specified_content_min_height(box.style, 0));
    const bool distribute_lines = box.style.align_content != JustifyContent::Start &&
        (fixed_height >= 0 || minimum_height > 0);
    std::vector<FlexWrapLine> lines;
    lines.reserve(box.children.size());
    lines.push_back(FlexWrapLine{content_y, 0, {}});
    int cursor_x = content_x;
    int line_y = content_y;
    int line_height = 0;
    const int max_line_width = std::max(1, content_width);
    const auto place_child = [&](LayoutBox& child) {
        const int basis=resolved_flex_basis_pixels(child.style,content_width);
        const bool use_basis = basis >= 0;
        layout_child_for_width(child, use_basis ? basis : content_width, use_basis);
        const int child_width = bounded_add(
            bounded_add(child.rect.width, child.style.margin.left), child.style.margin.right);
        const int child_height = bounded_add(
            bounded_add(child.rect.height, child.style.margin.top), child.style.margin.bottom);
        const bool should_wrap = cursor_x > content_x &&
            bounded_add(cursor_x, child_width) > bounded_add(content_x, max_line_width);
        if (should_wrap) {
            lines.back().height = line_height;
            const int line_advance = bounded_add(std::max(1, line_height), box.style.row_gap);
            lines.push_back(FlexWrapLine{bounded_add(line_y, line_advance), 0, {}});
            line_y = bounded_add(line_y, line_advance);
            cursor_x = content_x;
            line_height = 0;
        }
        const int dx = bounded_subtract(bounded_add(cursor_x, child.style.margin.left), child.rect.x);
        const int dy = bounded_subtract(bounded_add(line_y, child.style.margin.top), child.rect.y);
        shift_box(child, dx, dy);
        lines.back().children.push_back(&child);
        cursor_x = bounded_add(cursor_x, bounded_add(child_width, box.style.column_gap));
        line_height = std::max(line_height, child_height);
    };
    if (ordered_children.empty()) {
        for (const LayoutBoxPtr& child : box.children) {
            if (!is_out_of_flow_positioned(child->style)) {
                place_child(*child);
            }
        }
    } else {
        for (LayoutBox* child : ordered_children) {
            place_child(*child);
        }
    }
    const int natural_height = bounded_add(bounded_subtract(line_y, content_y), std::max(0, line_height));
    lines.back().height = line_height;

    // A wrapped line has its own cross-axis alignment context. Relayout only
    // auto-sized stretch items; explicit dimensions remain authoritative.
    for (FlexWrapLine& line : lines) {
        const int line_cross_size = std::max(1, line.height);
        for (LayoutBox* child : line.children) {
            const AlignItems alignment = flex_cross_axis_alignment(*child, box.style.align_items);
            if (alignment == AlignItems::Stretch && flex_cross_size_is_auto(child->style, true)) {
                const int old_height = child->style.height;
                const int old_height_percent = child->style.height_percent;
                set_flex_stretch_cross_size(child->style, line_cross_size, true);
                layout_child_for_width(*child, std::max(0, child->rect.width), false);
                child->style.height = old_height;
                child->style.height_percent = old_height_percent;
            }
            const int child_outer_height = flex_outer_cross_size(*child, true);
            int target_y = line.y;
            if (alignment == AlignItems::Center) {
                target_y = bounded_add(target_y,
                                       std::max(0, bounded_subtract(line_cross_size, child_outer_height) / 2));
            } else if (alignment == AlignItems::End) {
                target_y = bounded_add(target_y,
                                       std::max(0, bounded_subtract(line_cross_size, child_outer_height)));
            }
            const int dy = bounded_subtract(bounded_add(target_y, child->style.margin.top), child->rect.y);
            if (dy != 0) {
                shift_box(*child, 0, dy);
            }
        }
    }
    if (!distribute_lines) {
        return natural_height;
    }

    const int container_height = fixed_height >= 0 ? fixed_height : std::max(minimum_height, natural_height);
    const FlexJustifyPlacement placement = flex_justify_placement(box.style.align_content,
                                                                  content_y,
                                                                  container_height,
                                                                  natural_height,
                                                                  static_cast<int>(lines.size()));
    int target_y = placement.cursor;
    for (FlexWrapLine& line : lines) {
        const int dy = bounded_subtract(target_y, line.y);
        if (dy != 0) {
            for (LayoutBox* child : line.children) {
                shift_box(*child, 0, dy);
            }
        }
        target_y = bounded_add(target_y,
                               bounded_add(std::max(1, line.height),
                                           bounded_add(box.style.row_gap, placement.extra_gap)));
    }
    return container_height;
}

constexpr int kMaxGridColumns = 32;
constexpr int kMaxGridRows = 128;

struct GridColumns {
    int count = 1;
    int gap = 0;
    std::vector<int> widths;
    std::vector<bool> auto_sized;
    std::vector<bool> flexible;
};

GridColumns resolve_grid_columns(const Style& style, int content_width, int in_flow_count) {
    GridColumns columns;
    columns.gap = std::max(0, style.column_gap);
    if (style.grid_template_column_count > 0) {
        columns.count = std::min(style.grid_template_column_count, kMaxGridColumns);
        columns.widths.assign(static_cast<std::size_t>(columns.count), 0);
        columns.auto_sized.assign(static_cast<std::size_t>(columns.count), false);
        columns.flexible.assign(static_cast<std::size_t>(columns.count), false);
        for (int column = 0; column < columns.count; ++column) {
            const int width = style.grid_template_column_widths[static_cast<std::size_t>(column)];
            if (width > 0) {
                columns.widths[static_cast<std::size_t>(column)] = width;
            } else if (width < 0) {
                columns.auto_sized[static_cast<std::size_t>(column)] = true;
            } else {
                columns.flexible[static_cast<std::size_t>(column)] = true;
            }
        }
    } else {
        const int min_track = std::max(1, style.grid_min_track_width > 0 ? style.grid_min_track_width : content_width);
        const int available_with_gap = bounded_add(content_width, columns.gap);
        const int track_with_gap = std::max(1, bounded_add(min_track, columns.gap));
        columns.count = std::max(1, available_with_gap / track_with_gap);
        columns.count = std::min(columns.count, in_flow_count);
        columns.count = std::min(columns.count, kMaxGridColumns);
        const int total_gap_width = bounded_non_negative_multiply(
            columns.gap, std::max(0, columns.count - 1));
        const int column_width = std::max(1,
            bounded_subtract(content_width, total_gap_width) / columns.count);
        columns.widths.assign(static_cast<std::size_t>(columns.count), column_width);
        columns.auto_sized.assign(static_cast<std::size_t>(columns.count), false);
        columns.flexible.assign(static_cast<std::size_t>(columns.count), false);
    }
    return columns;
}

int grid_item_width(const GridColumns& columns, int column, int span) {
    int width = 0;
    for (int offset = 0; offset < span; ++offset) {
        width = bounded_add(width, columns.widths[static_cast<std::size_t>(column + offset)]);
    }
    width = bounded_add(width,
                        bounded_non_negative_multiply(columns.gap, std::max(0, span - 1)));
    return std::max(1, width);
}

int grid_column_x(const GridColumns& columns, int content_x, int column) {
    int offset = 0;
    for (int i = 0; i < column; ++i) {
        offset = bounded_add(offset,
                             bounded_add(columns.widths[static_cast<std::size_t>(i)], columns.gap));
    }
    return bounded_add(content_x, offset);
}

struct GridPlacement {
    LayoutBox* child = nullptr;
    int row = 0;
    int column = 0;
    int column_span = 1;
    int row_span = 1;
    bool fallback_block = false;
};

int grid_intrinsic_outer_width(const LayoutBox& box, const TextMeasureProvider& text_measure,
                               const TextSystem* text_system) {
    if (box.style.width >= 0) {
        return box.style.box_sizing_border_box
            ? std::max(0, box.style.width)
            : bounded_non_negative_add(
                  bounded_non_negative_add(box.style.width, horizontal_edges(box.style.padding)),
                  horizontal_edges(box.style.border_width));
    }
    if (box.node != nullptr && box.node->type == NodeType::Text) {
        const std::string text = transformed_render_text(*box.node, box.style.text_transform);
        if (text_system != nullptr) {
            TextRunMetrics metrics;
            return measure_core_text(*text_system, text, box.style, metrics) == TextSystemStatus::Ok
                ? q1000_ceil_pixels(metrics.advance) : 0;
        }
        return measure_text_with_letter_spacing(text_measure,
                                                text,
                                                box.style.font_size,
                                                box.style.font_weight,
                                                box.style.font_family_hash,
                                                box.style.letter_spacing).width;
    }

    int child_width = 0;
    bool all_inline = true;
    for (const auto& child : box.children) {
        if (is_out_of_flow_positioned(child->style)) {
            continue;
        }
        const int outer_width = bounded_non_negative_add(
            bounded_non_negative_add(grid_intrinsic_outer_width(*child, text_measure, text_system), child->style.margin.left),
            child->style.margin.right);
        const bool inline_child = (child->node != nullptr && child->node->type == NodeType::Text) ||
            child->style.display == Display::Inline || child->style.display == Display::InlineBlock;
        if (all_inline && inline_child) {
            child_width = bounded_add(child_width, outer_width);
        } else {
            all_inline = false;
            child_width = std::max(child_width, outer_width);
        }
    }
    return std::max(
        std::max(0, box.style.min_width),
        bounded_non_negative_add(
            bounded_non_negative_add(child_width, horizontal_edges(box.style.padding)),
            horizontal_edges(box.style.border_width)));
}

bool grid_source_query_has_unconstrained_root(const LayoutBox& box) {
    using detail::SizeKind;
    using detail::SizeProperty;
    if (box.style.display != Display::Block || box.style.width >= 0 || box.style.width_percent >= 0 ||
        box.style.min_width >= 0 || box.style.min_width_percent >= 0 ||
        box.style.max_width >= 0 || box.style.max_width_percent >= 0) return false;
    for (SizeProperty property : {SizeProperty::Width, SizeProperty::MinWidth, SizeProperty::MaxWidth}) {
        SizeKind kind = SizeKind::Pixels;
        std::int64_t value = 0;
        if (box.style.computed_sizes.get(property, kind, value)) return false;
    }
    return true;
}

void finalize_grid_columns(GridColumns& columns,
                           const std::vector<GridPlacement>& placements,
                           const TextMeasureProvider& text_measure,
                           const TextSystem* text_system,
                           int content_width) {
    std::vector<int> source_widths(placements.size(), -1);
    detail::IntrinsicSourceQueryLimits source_limits;
    detail::IntrinsicSourceQueryUsage source_usage;
    bool source_queries_complete = text_system != nullptr;
    for (std::size_t index = 0; index < placements.size(); ++index) {
        const GridPlacement& placement = placements[index];
        if (placement.fallback_block || placement.column_span != 1 ||
            placement.column < 0 || placement.column >= columns.count ||
            !columns.auto_sized[static_cast<std::size_t>(placement.column)]) {
            continue;
        }
        if (!source_queries_complete || !grid_source_query_has_unconstrained_root(*placement.child)) {
            source_queries_complete = false;
            continue;
        }
        auto remaining = source_limits;
        remaining.max_boxes -= source_usage.boxes;
        remaining.max_source_bytes -= source_usage.source_bytes;
        remaining.max_measurements -= source_usage.measurements;
        remaining.max_work -= source_usage.work;
        const auto source = detail::query_intrinsic_source_width(*placement.child, *text_system,
            {detail::IntrinsicPercentageBasisKind::CyclicSameAxis, -1}, remaining);
        if (!source_usage.consume(source, source_limits) ||
            source.status != detail::IntrinsicSourceStatus::Complete) {
            source_queries_complete = false;
            break;
        }
        source_widths[index] = clamp_layout_value((source.max_content_border_box_units + 999) / 1000);
    }

    for (std::size_t index = 0; index < placements.size(); ++index) {
        const GridPlacement& placement = placements[index];
        if (placement.fallback_block || placement.column_span != 1 ||
            placement.column < 0 || placement.column >= columns.count ||
            !columns.auto_sized[static_cast<std::size_t>(placement.column)]) continue;
        const int intrinsic_width = source_queries_complete && source_widths[index] >= 0
            ? source_widths[index]
            : grid_intrinsic_outer_width(*placement.child, text_measure, text_system);
        columns.widths[static_cast<std::size_t>(placement.column)] = std::max(
            columns.widths[static_cast<std::size_t>(placement.column)], intrinsic_width);
    }

    int occupied_width = bounded_non_negative_multiply(columns.gap, std::max(0, columns.count - 1));
    int flexible_count = 0;
    for (int column = 0; column < columns.count; ++column) {
        occupied_width = bounded_add(occupied_width, columns.widths[static_cast<std::size_t>(column)]);
        if (columns.flexible[static_cast<std::size_t>(column)]) {
            ++flexible_count;
        }
    }
    int remaining_width = bounded_non_negative_subtract(content_width, occupied_width);
    int remaining_flexible = flexible_count;
    for (int column = 0; column < columns.count && remaining_flexible > 0; ++column) {
        if (!columns.flexible[static_cast<std::size_t>(column)]) {
            continue;
        }
        const int share = remaining_width / remaining_flexible;
        columns.widths[static_cast<std::size_t>(column)] = std::max(1, share);
        remaining_width = bounded_non_negative_subtract(remaining_width, share);
        --remaining_flexible;
    }
}

struct GridPlacementState {
    std::vector<std::uint64_t> occupied;
    std::vector<int> row_heights;
    std::vector<bool> row_fixed;
};

std::uint64_t grid_occupancy_mask(int column, int column_span) {
    return ((std::uint64_t{1} << column_span) - 1U) << column;
}

void ensure_grid_rows(GridPlacementState& state, int rows, int min_row_height) {
    const int bounded_rows = std::min(rows, kMaxGridRows);
    while (static_cast<int>(state.occupied.size()) < bounded_rows) {
        state.occupied.push_back(0);
        state.row_heights.push_back(std::max(0, min_row_height));
        state.row_fixed.push_back(false);
    }
}

bool can_place_grid_item(const GridPlacementState& state,
                         int column_count,
                         int row,
                         int column,
                         int column_span,
                         int row_span) {
    if (column + column_span > column_count) {
        return false;
    }
    const std::uint64_t mask = grid_occupancy_mask(column, column_span);
    for (int r = row; r < row + row_span; ++r) {
        if (r < static_cast<int>(state.occupied.size()) &&
            (state.occupied[static_cast<std::size_t>(r)] & mask) != 0) {
            return false;
        }
    }
    return true;
}

void mark_grid_item_occupied(GridPlacementState& state,
                             int row,
                             int column,
                             int column_span,
                             int row_span) {
    const std::uint64_t mask = grid_occupancy_mask(column, column_span);
    for (int r = row; r < row + row_span; ++r) {
        state.occupied[static_cast<std::size_t>(r)] |= mask;
    }
}

GridPlacement place_grid_item(GridPlacementState& state,
                              int column_count,
                              int column_span,
                              int row_span,
                              int min_row_height,
                              int requested_column,
                              int requested_row) {
    GridPlacement placement;
    placement.column_span = column_span;
    placement.row_span = row_span;

    // The occupancy grid has a fixed, explicit memory bound. Do not clamp a
    // later row back onto the final tracked row: that creates overlapping
    // boxes and may index past the retained row vectors for large spans.
    if (row_span > kMaxGridRows ||
        (requested_row >= 0 &&
         (requested_row >= kMaxGridRows || requested_row > kMaxGridRows - row_span))) {
        placement.fallback_block = true;
        return placement;
    }

    if (requested_column >= 0) {
        placement.column = std::min(requested_column, std::max(0, column_count - column_span));
    }
    if (requested_row >= 0) {
        placement.row = requested_row;
    }

    if (requested_row >= 0 && requested_column >= 0) {
        ensure_grid_rows(state, placement.row + row_span, min_row_height);
        mark_grid_item_occupied(state, placement.row, placement.column, column_span, row_span);
        return placement;
    }

    if (requested_row >= 0) {
        ensure_grid_rows(state, placement.row + row_span, min_row_height);
        for (int column = 0; column <= column_count - column_span; ++column) {
            if (can_place_grid_item(state, column_count, placement.row, column, column_span, row_span)) {
                placement.column = column;
                mark_grid_item_occupied(state, placement.row, placement.column, column_span, row_span);
                return placement;
            }
        }
        mark_grid_item_occupied(state, placement.row, placement.column, column_span, row_span);
        return placement;
    }

    bool placed = false;
    while (!placed) {
        if (placement.row + row_span > kMaxGridRows) {
            placement.fallback_block = true;
            return placement;
        }
        ensure_grid_rows(state, placement.row + row_span, min_row_height);
        const int column_begin = requested_column >= 0 ? placement.column : 0;
        const int column_end = requested_column >= 0 ? placement.column : column_count - column_span;
        for (int column = column_begin; column <= column_end; ++column) {
            if (can_place_grid_item(state, column_count, placement.row, column, column_span, row_span)) {
                placement.column = column;
                placed = true;
                break;
            }
        }
        if (!placed) {
            ++placement.row;
        }
    }
    ensure_grid_rows(state, placement.row + row_span, min_row_height);
    mark_grid_item_occupied(state, placement.row, placement.column, column_span, row_span);
    return placement;
}
#endif

bool participates_in_inline_flow(const LayoutBox& box) {
    return (box.node != nullptr && box.node->type == NodeType::Text) ||
        box.style.display == Display::Inline || box.style.display == Display::InlineBlock;
}

bool has_only_inline_children(const LayoutBox& box) {
    if (box.children.empty()) {
        return false;
    }
    bool has_in_flow_child = false;
    for (const auto& child : box.children) {
        if (is_out_of_flow_positioned(child->style)) {
            continue;
        }
        has_in_flow_child = true;
        if (!participates_in_inline_flow(*child)) {
            return false;
        }
    }
    return has_in_flow_child;
}

void shift_text_cache(LayoutBox& box, int dx, int dy) {
    if (box.text_layout_cache.valid) {
        box.text_layout_cache.rect_x = bounded_add(box.text_layout_cache.rect_x, dx);
        box.text_layout_cache.rect_y = bounded_add(box.text_layout_cache.rect_y, dy);
        if (box.text_layout_cache.prepared_text) {
            box.text_layout_cache.prepared_origin_x = bounded_add(box.text_layout_cache.prepared_origin_x, dx);
            box.text_layout_cache.prepared_baseline_y = bounded_add(box.text_layout_cache.prepared_baseline_y, dy);
            for (auto& line : box.text_layout_cache.prepared_lines) {
                line.origin_x = bounded_add(line.origin_x, dx);
                line.baseline_y = bounded_add(line.baseline_y, dy);
            }
        }
    }
}

void shift_box(LayoutBox& box, int dx, int dy) {
    box.rect.x = bounded_add(box.rect.x, dx);
    box.rect.y = bounded_add(box.rect.y, dy);
    if (box.content_geometry.width >= 0 || box.content_geometry.width == -2) {
        box.content_geometry.x += static_cast<std::int64_t>(dx) * 1000;
        box.content_geometry.y += static_cast<std::int64_t>(dy) * 1000;
    }
    shift_text_cache(box, dx, dy);
    for (auto& child : box.children) {
        shift_box(*child, dx, dy);
    }
}

void shift_box_precise(LayoutBox& box, std::int64_t dx, std::int64_t dy) {
    const auto old = box.rect;
    auto& geometry = box.content_geometry;
    if (geometry.width >= 0 && geometry.height() >= 0) {
        geometry.x += dx; geometry.y += dy;
        const auto x = geometry.x -
            (static_cast<std::int64_t>(box.style.padding.left)+box.style.border_width.left)*1000;
        const auto y = geometry.y -
            (static_cast<std::int64_t>(box.style.padding.top)+box.style.border_width.top)*1000;
        const auto width = geometry.width +
            (static_cast<std::int64_t>(horizontal_edges(box.style.padding))+horizontal_edges(box.style.border_width))*1000;
        const auto height = geometry.height() +
            (static_cast<std::int64_t>(vertical_edges(box.style.padding))+vertical_edges(box.style.border_width))*1000;
        box.rect = {detail::project_size_edge(x), detail::project_size_edge(y),
            detail::project_size_extent(x, width), detail::project_size_extent(y, height)};
    } else {
        if (geometry.width == -2) { geometry.x += dx; geometry.y += dy; }
        box.rect.x = detail::project_size_edge(static_cast<std::int64_t>(old.x)*1000+dx);
        box.rect.y = detail::project_size_edge(static_cast<std::int64_t>(old.y)*1000+dy);
    }
    shift_text_cache(box, bounded_subtract(box.rect.x, old.x), bounded_subtract(box.rect.y, old.y));
    for (auto& child : box.children) shift_box_precise(*child, dx, dy);
}

} // namespace

void LayoutBoxDeleter::operator()(LayoutBox* box) const {
    if (!arena_owned) {
        delete box;
    }
}

struct LayoutEngine::PreciseBoxContext {
    std::int64_t width = 0;
    std::int64_t height = -1;
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t allocated_width = -1;
    std::int64_t allocated_height = -1;
    bool border_origin = false;
};

struct LayoutEngine::PositionedContext {
    LayoutContentGeometry containing;
    LayoutContentGeometry parent;
};

LayoutEngine::LayoutEngine(const StyleResolver& style_resolver,
                           TextMeasureProvider text_measure,
                           LayoutEngineOptions options)
    : style_resolver_(style_resolver), text_measure_(text_measure), options_(options) {}

LayoutBoxPtr LayoutEngine::layout(const Node& root, int viewport_width) const {
    return layout(root, viewport_width, 240);
}

LayoutBoxPtr LayoutEngine::layout(const Node& root, int viewport_width, int viewport_height) const {
    RenderTreeOptions render_options;
    render_options.diagnostics = options_.diagnostics;
    render_options.viewport_width = viewport_width;
    render_options.viewport_height = viewport_height;
    RenderTreeBuilder render_tree_builder(style_resolver_, render_options);
    auto render_tree = render_tree_builder.build(root);
    return layout(*render_tree, viewport_width, viewport_height);
}

LayoutBoxPtr LayoutEngine::layout(const Node& root, int viewport_width, MonotonicArena& arena) const {
    return layout(root, viewport_width, 240, arena);
}

LayoutBoxPtr LayoutEngine::layout(const Node& root, int viewport_width, int viewport_height, MonotonicArena& arena) const {
    RenderTreeOptions render_options;
    render_options.diagnostics = options_.diagnostics;
    render_options.viewport_width = viewport_width;
    render_options.viewport_height = viewport_height;
    RenderTreeBuilder render_tree_builder(style_resolver_, render_options);
    auto render_tree = render_tree_builder.build(root, arena);
    return layout(*render_tree, viewport_width, viewport_height, arena);
}

LayoutBoxPtr LayoutEngine::layout(const RenderObject& render_tree, int viewport_width) const {
    return layout(render_tree, viewport_width, 240);
}

LayoutBoxPtr LayoutEngine::layout(const RenderObject& render_tree, int viewport_width, int viewport_height) const {
    return build_with_arena(render_tree, viewport_width, viewport_height, nullptr);
}

LayoutBoxPtr LayoutEngine::layout(const RenderObject& render_tree, int viewport_width, MonotonicArena& arena) const {
    return layout(render_tree, viewport_width, 240, arena);
}

LayoutBoxPtr LayoutEngine::layout(const RenderObject& render_tree,
                                  int viewport_width,
                                  int viewport_height,
                                  MonotonicArena& arena) const {
    return build_with_arena(render_tree, viewport_width, viewport_height, &arena);
}

LayoutBoxPtr LayoutEngine::build_with_arena(const RenderObject& render_tree,
                                            int viewport_width,
                                            int viewport_height,
                                            MonotonicArena* arena) const {
    auto root_box = make_layout_box(arena);
    root_box->node = render_tree.node;
    root_box->style = render_tree.style;
    root_box->viewport_width = viewport_width;
    root_box->viewport_height = viewport_height;
    build_layout_tree(render_tree, *root_box, arena);
    const PreciseBoxContext precision{static_cast<std::int64_t>(std::max(0,viewport_width))*1000,
        static_cast<std::int64_t>(std::max(0,viewport_height))*1000,0,0};
    layout_box(*root_box, 0, 0, viewport_width, viewport_height, 1, &precision);
    const LayoutContentGeometry viewport{0, 0, precision.width, precision.height, true};
    layout_positioned_children(*root_box, PositionedContext{viewport, viewport}, 1);
    return root_box;
}

void LayoutEngine::build_layout_tree(const RenderObject& object, LayoutBox& box, MonotonicArena* arena) const {
    struct PendingObject {
        const RenderObject* object = nullptr;
        LayoutBox* box = nullptr;
    };

    const std::size_t max_layout_boxes = std::max<std::size_t>(1, options_.max_layout_boxes);
    std::size_t layout_box_count = 1;
    bool budget_reported = false;
    std::vector<PendingObject> pending;
    pending.push_back(PendingObject{&object, &box});
    while (!pending.empty()) {
        const PendingObject current = pending.back();
        pending.pop_back();
        const RenderObject& render_object = *current.object;
        LayoutBox& layout_box = *current.box;
        std::vector<PendingObject> child_work;
        child_work.reserve(render_object.children.size());
        for (const auto& child : render_object.children) {
            if (layout_box_count >= max_layout_boxes) {
                if (!budget_reported) {
                    report_diagnostic(options_.diagnostics,
                                      DiagnosticStage::Layout,
                                      DiagnosticSeverity::Warning,
                                      "layout-box-limit",
                                      "Layout box budget was reached; remaining render objects were skipped",
                                      "Increase max_layout_boxes or simplify nested layout.");
                    budget_reported = true;
                }
                break;
            }
            auto child_box = make_layout_box(arena);
            LayoutBox* child_box_raw = child_box.get();
            ++layout_box_count;
            child_box->node = child->node;
            child_box->style = child->style;
            child_box->viewport_width = box.viewport_width;
            child_box->viewport_height = box.viewport_height;
            layout_box.children.push_back(std::move(child_box));
            child_work.push_back(PendingObject{child.get(), child_box_raw});
        }
        for (auto it = child_work.rbegin(); it != child_work.rend(); ++it) {
            pending.push_back(*it);
        }
    }
}

LayoutBoxPtr LayoutEngine::make_layout_box(MonotonicArena* arena) const {
    if (arena == nullptr) {
        return LayoutBoxPtr(new LayoutBox, LayoutBoxDeleter{false});
    }
    return LayoutBoxPtr(&arena->create<LayoutBox>(), LayoutBoxDeleter{true});
}

#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
bool LayoutEngine::layout_precise_flex(LayoutBox& box, std::int64_t width, std::int64_t fixed_height,
    const PreciseBoxContext& parent, std::int64_t x, std::int64_t y, int legacy_height,
    std::size_t depth, std::int64_t& natural_height) const {
    using namespace detail;
    const bool row = box.style.flex_direction == FlexDirection::Row;
    if (box.style.flex_wrap || (!row && fixed_height < 0)) return false;
    const auto main_size = row ? width : fixed_height;
    const auto decoration = [](const Style& s, bool horizontal) {
        return std::max<std::int64_t>(0,horizontal ?
            static_cast<std::int64_t>(s.padding.left)+s.padding.right+s.border_width.left+s.border_width.right :
            static_cast<std::int64_t>(s.padding.top)+s.padding.bottom+s.border_width.top+s.border_width.bottom)*1000;
    };
    const auto margins = [](const Style& s, bool horizontal) {
        return (horizontal ? static_cast<std::int64_t>(s.margin_left_auto?0:s.margin.left)+(s.margin_right_auto?0:s.margin.right) :
            static_cast<std::int64_t>(s.margin.top)+s.margin.bottom)*1000;
    };
    const auto outer = [&](const Style& s, bool horizontal, std::int64_t value) {
        if (value < 0) return value;
        return std::min(kDimensionPrecisionMaximum,std::max(decoration(s,horizontal),
            s.box_sizing_border_box ? value : value+decoration(s,horizontal)));
    };
    auto ordered = ordered_flex_children(box);
    if (ordered.empty()) for (auto& child : box.children)
        if (!is_out_of_flow_positioned(child->style)) ordered.push_back(child.get());
    std::vector<FlexLengthPrecisionItem> items;
    std::vector<std::int64_t> cross;
    items.reserve(ordered.size());cross.reserve(ordered.size());
    // All admission checks precede probing or allocated child layout.
    for (auto* child : ordered) {
        if (is_out_of_flow_positioned(child->style)) continue;
        const auto& s = child->style;
        if (s.display != Display::Block || !child->node || child->node->type == NodeType::Text || is_form_control(*child->node)) return false;
        const auto preferred = computed_size_units(s,row?SizeProperty::Width:SizeProperty::Height,main_size);
        const auto basis = computed_size_units(s,SizeProperty::FlexBasis,main_size);
        const auto minimum = computed_size_units(s,row?SizeProperty::MinWidth:SizeProperty::MinHeight,main_size);
        const auto maximum = computed_size_units(s,row?SizeProperty::MaxWidth:SizeProperty::MaxHeight,main_size);
        if (preferred < -1 || basis < -1 || minimum < -1 || maximum < -1 || (preferred < 0 && basis < 0)) return false;
        const auto alignment = flex_cross_axis_alignment(*child,box.style.align_items);
        const auto cross_basis = row ? fixed_height : width;
        const auto cross_preferred = computed_size_units(s,row?SizeProperty::Height:SizeProperty::Width,cross_basis);
        const auto cross_min = computed_size_units(s,row?SizeProperty::MinHeight:SizeProperty::MinWidth,cross_basis);
        const auto cross_max = computed_size_units(s,row?SizeProperty::MaxHeight:SizeProperty::MaxWidth,cross_basis);
        if (cross_preferred < -1 || cross_min < -1 || cross_max < -1 ||
            (!row && cross_preferred < 0 && alignment != AlignItems::Stretch)) return false;
        FlexLengthPrecisionItem item;
        item.child=child;
        item.base=outer(s,row,basis>=0?basis:preferred);
        item.inner_base=std::max<std::int64_t>(0,item.base-decoration(s,row));
        item.minimum=minimum<0?decoration(s,row):outer(s,row,minimum);
        item.maximum=maximum<0?-1:outer(s,row,maximum);
        if (minimum<0) {
            const auto kind=flex_auto_min_kind(s,row?FlexMainAxis::Horizontal:FlexMainAxis::Vertical);
            if (kind==FlexAutoMinKind::Unresolved) return false;
            if (kind==FlexAutoMinKind::Content) {
                FlexAutoMinInput input;
                input.main_decoration_units=decoration(s,row);
                input.cross_decoration_units=decoration(s,!row);
                input.specified_main_units=outer(s,row,preferred);
                input.max_main_units=item.maximum;
                input.preferred_cross_units=outer(s,!row,cross_preferred);
                input.min_cross_units=outer(s,!row,cross_min);
                input.max_cross_units=outer(s,!row,cross_max);
                if (has_aspect_ratio(s)) {
                    input.ratio_main=row?s.aspect_ratio_width:s.aspect_ratio_height;
                    input.ratio_cross=row?s.aspect_ratio_height:s.aspect_ratio_width;
                    input.ratio_uses_border_box=s.box_sizing_border_box;
                }
                const auto& tag=child->node->tag_name;
                const bool special=tag=="img" || tag=="video" || tag=="canvas" || tag=="object" ||
                    tag=="embed" || tag=="iframe" || tag=="br" || tag=="hr";
                // Only empty ordinary boxes have a source-proven zero content
                // size here. Replaced metadata and general min-content are not
                // inferred from prior rectangles or Grid's width estimate.
                input.content_complete=child->children.empty() && !special;
                if (input.content_complete)input.content_size_units=input.main_decoration_units;
                const auto result=flex_auto_minimum(input);
                if (result.status!=FlexAutoMinStatus::Complete)return false;
                item.minimum=result.minimum_border_box_units;
            }
        }
        item.hypothetical=flex_length_clamp(item.base,item);
        item.target=item.hypothetical;
        item.margins=margins(s,row);
        items.push_back(item);cross.push_back(0);
    }
    const auto gap=static_cast<std::int64_t>(std::max(0,row?box.style.column_gap:box.style.row_gap))*1000;
    if (resolve_flex_lengths_precise(items,main_size,gap,[](std::size_t){}) != FlexLengthPrecisionStatus::Success) return false;
    const auto make_context = [&](const FlexLengthPrecisionItem& item, std::int64_t cx, std::int64_t cy) {
        PreciseBoxContext context{width,fixed_height,cx,cy};
        context.border_origin=true;
        const auto content=std::max<std::int64_t>(0,item.target-decoration(item.child->style,row));
        if(row) context.allocated_width=content; else context.allocated_height=content;
        return context;
    };
    auto cross_size=row?std::int64_t{0}:width;
    for(std::size_t index=0;index<items.size();++index) {
        auto& item=items[index];auto& child=*item.child;
        const auto definite_cross=row?computed_content_axis(child.style,true,fixed_height,-1,decoration(child.style,false)):-1;
        if(row && definite_cross>=0) {
            // A definite cross size cannot depend on descendant layout. Avoid a
            // redundant subtree probe; the final allocated layout still runs.
            cross[index]=std::min(kDimensionPrecisionMaximum,definite_cross+decoration(child.style,false));
            cross_size=std::max(cross_size,cross[index]+margins(child.style,false));
        } else if(row) {
            auto probe=make_context(item,0,0);
            std::int64_t flow=std::numeric_limits<std::int64_t>::min();
            const auto result=layout_box(child,0,0,project_size_edge(width),legacy_height,depth+1,&probe,&flow);
            if(flow==std::numeric_limits<std::int64_t>::min()) flow=static_cast<std::int64_t>(result)*1000;
            cross[index]=std::max<std::int64_t>(0,flow-margins(child.style,false));
            cross_size=std::max(cross_size,cross[index]+margins(child.style,false));
        } else {
            const auto fallback=std::max<std::int64_t>(0,width-margins(child.style,true)-decoration(child.style,true));
            const auto content=computed_content_axis(child.style,false,width,fallback,decoration(child.style,true));
            cross[index]=std::min(kDimensionPrecisionMaximum,content+decoration(child.style,true));
        }
    }
    if(row) {
        if(has_aspect_ratio(box.style)) cross_size=std::max(cross_size,scale_size_units(width,box.style.aspect_ratio_height,box.style.aspect_ratio_width));
        cross_size=parent.allocated_height>=0?parent.allocated_height:computed_content_axis(box.style,true,parent.height,
            std::clamp<std::int64_t>(cross_size,0,kDimensionPrecisionMaximum),decoration(box.style,false));
        if(cross_size<0) return false;
    }
    auto occupied=gap*static_cast<std::int64_t>(items.empty()?0:items.size()-1);
    unsigned auto_edges=0;
    for(const auto& item:items) {
        occupied+=item.target+item.margins;
        if(row) auto_edges+=static_cast<unsigned>(item.child->style.margin_left_auto)+item.child->style.margin_right_auto;
    }
    auto free=std::max<std::int64_t>(0,main_size-occupied);
    auto auto_free=auto_edges?free:0;
    if(auto_edges) free=0;
    std::int64_t cursor=0, extra_gap=0;
    switch(box.style.justify_content) {
    case JustifyContent::End:cursor=free;break;
    case JustifyContent::Center:cursor=free/2;break;
    case JustifyContent::SpaceAround:extra_gap=items.empty()?0:free/static_cast<std::int64_t>(items.size());cursor=extra_gap/2;break;
    case JustifyContent::SpaceBetween:extra_gap=items.size()<2?0:free/static_cast<std::int64_t>(items.size()-1);break;
    case JustifyContent::SpaceEvenly:extra_gap=items.empty()?0:free/static_cast<std::int64_t>(items.size()+1);cursor=extra_gap;break;
    default:break;
    }
    const auto take_margin=[&](bool automatic,int numeric) {
        if(!automatic) return static_cast<std::int64_t>(numeric)*1000;
        const auto share=auto_edges?auto_free/auto_edges:0;
        auto_free-=share;if(auto_edges)--auto_edges;return share;
    };
    for(std::size_t index=0;index<items.size();++index) {
        auto& item=items[index];auto& child=*item.child;const auto& s=child.style;
        const auto alignment=flex_cross_axis_alignment(child,box.style.align_items);
        const auto leading=row?take_margin(s.margin_left_auto,s.margin.left):static_cast<std::int64_t>(s.margin.top)*1000;
        const auto trailing=row?take_margin(s.margin_right_auto,s.margin.right):static_cast<std::int64_t>(s.margin.bottom)*1000;
        auto context=make_context(item,row?x+cursor+leading:x,row?y:y+cursor+leading);
        auto cross_border=cross[index];
        const auto cross_preferred=computed_size_units(s,row?SizeProperty::Height:SizeProperty::Width,row?fixed_height:width);
        const bool stretch=alignment==AlignItems::Stretch && cross_preferred<0;
        if(stretch) {
            const auto fallback=std::max<std::int64_t>(0,cross_size-margins(s,!row)-decoration(s,!row));
            const auto content=computed_content_axis(s,row,row?fixed_height:width,fallback,decoration(s,!row));
            cross_border=std::min(kDimensionPrecisionMaximum,content+decoration(s,!row));
            if(row) context.allocated_height=content;else context.allocated_width=content;
        }
        const auto cross_free=std::max<std::int64_t>(0,cross_size-cross_border-margins(s,!row));
        const auto shift=alignment==AlignItems::Center?cross_free/2:alignment==AlignItems::End?cross_free:0;
        if(row) context.y+=shift+static_cast<std::int64_t>(s.margin.top)*1000;
        else context.x+=shift+static_cast<std::int64_t>(s.margin_left_auto?0:s.margin.left)*1000;
        layout_box(child,project_size_edge(context.x),project_size_edge(context.y),project_size_edge(width),
            legacy_height,depth+1,&context);
        cursor+=leading+item.target+trailing+gap+extra_gap;
    }
    natural_height=row?cross_size:fixed_height;
    return true;
}
#endif

int LayoutEngine::layout_precise_block(LayoutBox& box, int height, std::size_t depth,
    const PreciseBoxContext& context, std::int64_t* flow_units) const {
    using namespace detail;
    const auto& style = box.style;
    const auto horizontal_decoration = std::max<std::int64_t>(0,
        static_cast<std::int64_t>(horizontal_edges(style.padding))+horizontal_edges(style.border_width))*1000;
    const auto vertical_decoration = std::max<std::int64_t>(0,
        static_cast<std::int64_t>(vertical_edges(style.padding))+vertical_edges(style.border_width))*1000;
    const auto left_margin = style.margin_left_auto ? 0 : static_cast<std::int64_t>(style.margin.left)*1000;
    const auto right_margin = style.margin_right_auto ? 0 : static_cast<std::int64_t>(style.margin.right)*1000;
    const auto fallback_width = std::clamp<std::int64_t>(context.width-left_margin-right_margin-horizontal_decoration,
        0,kDimensionPrecisionMaximum);
    const auto content_width_units = context.allocated_width >= 0 ? context.allocated_width :
        computed_content_axis(style,false,context.width,fallback_width,horizontal_decoration);
    const auto fixed_height_units = context.allocated_height >= 0 ? context.allocated_height :
        computed_content_axis(style,true,context.height,-1,vertical_decoration);
    if (content_width_units < 0 || fixed_height_units < -1) {
        const auto result = layout_box(box,project_size_edge(context.x),project_size_edge(context.y),
            project_size_edge(context.width),height,depth);
        if (flow_units) *flow_units = static_cast<std::int64_t>(result)*1000;
        return result;
    }
    const auto border_width_units = std::min(kDimensionPrecisionMaximum,content_width_units+horizontal_decoration);
    const auto auto_space = std::max<std::int64_t>(0,context.width-border_width_units-left_margin-right_margin);
    auto border_x_units = context.x+(context.border_origin?0:left_margin);
    if (!context.border_origin && style.margin_left_auto && style.margin_right_auto) border_x_units += auto_space/2;
    else if (!context.border_origin && style.margin_left_auto) border_x_units += auto_space;
    const auto border_y_units = context.y+(context.border_origin?0:static_cast<std::int64_t>(style.margin.top)*1000);
    const auto content_x_units = border_x_units+
        (static_cast<std::int64_t>(style.border_width.left)+style.padding.left)*1000;
    const auto content_y_units = border_y_units+
        (static_cast<std::int64_t>(style.border_width.top)+style.padding.top)*1000;
    const auto content_width = project_size_extent(content_x_units,content_width_units);
    const bool viewport_root = box.node->tag_name == "document" ||
        (box.node->tag_name == "html" && box.node->parent && box.node->parent->tag_name == "document");
    const auto child_height_basis = fixed_height_units >= 0 ? fixed_height_units :
        viewport_root ? context.height : std::int64_t{-1};
    std::int64_t children_units = 0;
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    if (style.display == Display::Flex) {
        if (!layout_precise_flex(box,content_width_units,fixed_height_units,context,content_x_units,content_y_units,
                                height,depth,children_units)) {
            const auto result = layout_box(box,project_size_edge(context.x),project_size_edge(context.y),
                project_size_edge(context.width),height,depth);
            if (flow_units) *flow_units = static_cast<std::int64_t>(result)*1000;
            return result;
        }
    } else
#endif
    if (has_only_inline_children(box)) {
        children_units = static_cast<std::int64_t>(layout_inline_children(box,project_size_edge(content_x_units),
            project_size_edge(content_y_units),content_width,depth))*1000;
    } else {
        auto cursor_units = content_y_units;
        for (auto& child : box.children) {
            if (is_out_of_flow_positioned(child->style)) {
                child->content_geometry = {content_x_units, cursor_units, -2, -2, false};
                continue;
            }
            const PreciseBoxContext child_context{content_width_units,child_height_basis,content_x_units,cursor_units};
            std::int64_t child_flow_units = std::numeric_limits<std::int64_t>::min();
            const auto child_height = layout_box(*child,project_size_edge(content_x_units),project_size_edge(cursor_units),
                content_width,height,depth+1,&child_context,&child_flow_units);
            if (child_flow_units == std::numeric_limits<std::int64_t>::min())
                child_flow_units = static_cast<std::int64_t>(child_height)*1000;
            cursor_units += child_flow_units;
            children_units += child_flow_units;
        }
    }
    std::int64_t ratio_height_units = 0;
    if (has_aspect_ratio(style) && content_width_units > 0) {
        ratio_height_units = scale_size_units(content_width_units,style.aspect_ratio_height,style.aspect_ratio_width);
        if (ratio_height_units < 0) ratio_height_units = 0;
    }
    const auto auto_height = std::clamp<std::int64_t>(std::max(children_units,ratio_height_units),0,kDimensionPrecisionMaximum);
    const auto content_height_units = context.allocated_height >= 0 ? context.allocated_height :
        computed_content_axis(style,true,context.height,auto_height,vertical_decoration);
    // A malformed retained value was already refused above while computing the
    // fixed height, before descendants were laid out.
    const auto border_height_units = std::min(kDimensionPrecisionMaximum,content_height_units+vertical_decoration);
    box.rect = Rect{project_size_edge(border_x_units),project_size_edge(border_y_units),
        project_size_extent(border_x_units,border_width_units),project_size_extent(border_y_units,border_height_units)};
    box.content_geometry = {content_x_units, content_y_units, content_width_units, content_height_units,
        fixed_height_units >= 0 || viewport_root};
    const auto total_units = static_cast<std::int64_t>(style.margin.top)*1000+border_height_units+
        static_cast<std::int64_t>(style.margin.bottom)*1000;
    if (flow_units) *flow_units = total_units;
    return project_size_edge(total_units);
}

int LayoutEngine::layout_fixed_table(LayoutBox& box, int x, int y, int available_width, int height,
                                    std::size_t depth) const {
    using detail::SizeKind;
    using detail::SizeProperty;
    const auto has_retained = [](const Style& style, SizeProperty property) {
        SizeKind kind = SizeKind::Pixels;
        std::int64_t value = 0;
        return style.computed_sizes.get(property, kind, value);
    };
    const auto integral_pixel_sizing = [](const Style& style) {
        constexpr int Style::* scalars[] = {&Style::width, &Style::height, &Style::min_width,
            &Style::min_height, &Style::max_width, &Style::max_height};
        constexpr int Style::* percentages[] = {&Style::width_percent, &Style::height_percent,
            &Style::min_width_percent, &Style::min_height_percent, &Style::max_width_percent,
            &Style::max_height_percent};
        if ((style.table_properties & kTableUnqualifiedSizingMask) != 0) return false;
        for (unsigned index = 0; index < 6; ++index) {
            SizeKind kind = SizeKind::Pixels;
            std::int64_t value = 0;
            if (style.*percentages[index] >= 0) return false;
            if (style.computed_sizes.get(static_cast<SizeProperty>(index), kind, value) &&
                (kind != SizeKind::Pixels || value < 0 || value % 1000 != 0 ||
                 value != static_cast<std::int64_t>(style.*scalars[index]) * 1000)) return false;
        }
        return true;
    };
    const auto unconstrained = [&](const Style& style) {
        return style.min_width < 0 && style.min_height < 0 && style.max_width < 0 && style.max_height < 0 &&
            !has_retained(style, SizeProperty::MinWidth) && !has_retained(style, SizeProperty::MinHeight) &&
            !has_retained(style, SizeProperty::MaxWidth) && !has_retained(style, SizeProperty::MaxHeight);
    };
    const auto zero_edges = [](const EdgeSizes& edges) {
        return edges.top == 0 && edges.right == 0 && edges.bottom == 0 && edges.left == 0;
    };
    const auto structural_style = [&](const Style& style, bool column) {
        return style.position_type == PositionType::Static && style.height < 0 &&
            !has_retained(style, SizeProperty::Height) && unconstrained(style) &&
            (column || (style.width < 0 && !has_retained(style, SizeProperty::Width))) &&
            zero_edges(style.margin) && !style.margin_left_auto && !style.margin_right_auto &&
            zero_edges(style.padding) && zero_edges(style.border_width);
    };
    if (!table_layout_is_fixed(box.style) || table_border_is_collapsed(box.style) ||
        box.style.width < 0 || box.style.width_percent >= 0 ||
        !integral_pixel_sizing(box.style) || !unconstrained(box.style) ||
        !zero_edges(box.style.padding) || is_out_of_flow_positioned(box.style) ||
        box.style.height >= 0 || has_retained(box.style, SizeProperty::Height) ||
        depth >= options_.max_layout_depth || options_.max_layout_depth - depth < 3) return -1;
    LayoutBox* group = nullptr;
    LayoutBox* columns = nullptr;
    LayoutBox* caption = nullptr;
    for (auto& child : box.children) {
        switch (child->style.display) {
        case Display::TableRowGroup:
            if (group != nullptr || child->node == nullptr || child->node->tag_name != "tbody") return -1;
            group = child.get();
            break;
        case Display::TableColumnGroup:
            if (columns != nullptr) return -1;
            columns = child.get();
            break;
        case Display::TableCaption:
            if (caption != nullptr) return -1;
            // Caption margins need a separate containing-block qualification;
            // ordinary layout remains authoritative for those inputs.
            if (child->style.margin.top != 0 || child->style.margin.right != 0 ||
                child->style.margin.bottom != 0 || child->style.margin.left != 0 ||
                child->style.margin_left_auto || child->style.margin_right_auto ||
                child->style.width >= 0 || child->style.width_percent >= 0 ||
                has_retained(child->style, SizeProperty::Width) || !unconstrained(child->style) ||
                child->style.position_type != PositionType::Static) return -1;
            caption = child.get();
            break;
        default:
            return -1;
        }
    }
    if (group == nullptr || columns == nullptr || columns->children.empty() || group->children.empty()) return -1;
    std::vector<const LayoutBox*> pending_descendants;
    for (const auto& child : box.children) pending_descendants.push_back(child.get());
    while (!pending_descendants.empty()) {
        const LayoutBox* current = pending_descendants.back();
        pending_descendants.pop_back();
        if (current->style.display == Display::Table || !integral_pixel_sizing(current->style)) return -1;
        if ((current->style.display == Display::TableRowGroup || current->style.display == Display::TableRow ||
             current->style.display == Display::TableColumnGroup || current->style.display == Display::TableColumn) &&
            !structural_style(current->style, current->style.display == Display::TableColumn)) return -1;
        if (current->node != nullptr && current->node->type == NodeType::Element) {
            if (current->node != box.node && current->node->tag_name == "table") return -1;
            if (current->node->attributes.find("colspan") != current->node->attributes.end() ||
                current->node->attributes.find("rowspan") != current->node->attributes.end()) return -1;
        }
        for (const auto& child : current->children) pending_descendants.push_back(child.get());
    }
    const std::size_t column_count = columns->children.size();
    std::vector<int> tracks;
    tracks.reserve(column_count);
    std::int64_t tracks_total = 0;
    for (const auto& column : columns->children) {
        if (column->style.display != Display::TableColumn || column->style.width <= 0 ||
            column->style.width_percent >= 0 || !unconstrained(column->style) ||
            !column->children.empty()) return -1;
        tracks.push_back(column->style.width);
        tracks_total += column->style.width;
    }
    for (const auto& row : group->children) {
        if (row->style.display != Display::TableRow || row->children.size() != column_count) return -1;
        for (const auto& cell : row->children) {
            if (cell->style.display != Display::TableCell || cell->style.width_percent >= 0 ||
                cell->style.width >= 0 || has_retained(cell->style, SizeProperty::Width) ||
                !unconstrained(cell->style)) return -1;
        }
    }
    const int spacing_x = table_border_spacing_x(box.style);
    const int spacing_y = table_border_spacing_y(box.style);
    const int border_x = horizontal_edges(box.style.border_width);
    const auto fits = [](std::int64_t value) {
        return value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max();
    };
    const std::int64_t grid_width = tracks_total + static_cast<std::int64_t>(spacing_x) *
        static_cast<std::int64_t>(column_count + 1) + border_x;
    if (!fits(grid_width)) return -1;
    const int table_width = std::max(box.style.width, static_cast<int>(grid_width));
    // Explicit columns grow proportionally. Project cumulative edges to the
    // nearest integer so rounding cannot lose space or separate shared edges.
    const std::int64_t target_tracks_total = tracks_total + table_width - grid_width;
    std::int64_t source_edge = 0;
    std::int64_t assigned_edge = 0;
    for (std::size_t index = 0; index < column_count; ++index) {
        source_edge += tracks[index];
        const auto next_edge = (source_edge * target_tracks_total + tracks_total / 2) / tracks_total;
        tracks[index] = static_cast<int>(next_edge - assigned_edge);
        assigned_edge = next_edge;
    }
    tracks_total = target_tracks_total;
    const int margin_left = box.style.margin_left_auto ? 0 : box.style.margin.left;
    // The caller has already removed fixed horizontal margins from this width.
    const int auto_space = bounded_non_negative_subtract(available_width, table_width);
    int auto_left = 0;
    if (box.style.margin_left_auto && box.style.margin_right_auto) auto_left = auto_space / 2;
    else if (box.style.margin_left_auto) auto_left = auto_space;
    const std::int64_t table_x_value = static_cast<std::int64_t>(x) + margin_left + auto_left;
    if (!fits(table_x_value) || !fits(table_x_value + table_width)) return -1;
    const int table_x = static_cast<int>(table_x_value);
    const std::int64_t first_x_value = table_x_value + box.style.border_width.left + spacing_x;
    if (!fits(first_x_value)) return -1;
    const int first_x = static_cast<int>(first_x_value);
    // All failures after this point restore geometry and prepared-text caches
    // before the caller executes ordinary bounded layout.
    detail::LayoutMutationJournal transaction;
    if (transaction.capture(box, options_.max_layout_boxes) != detail::LayoutMutationJournalStatus::Ready)
        return -1;
    int caption_height = 0;
    if (caption != nullptr) {
        const int measured = layout_box(*caption, table_x, y, table_width, height, depth + 1);
        caption_height = std::max(0, measured);
        caption->rect.x = table_x;
        caption->rect.width = table_width;
    }
    const int caption_before = caption != nullptr && table_caption_side(caption->style) == TableCaptionSide::Top
        ? caption_height : 0;
    std::int64_t cursor_y = static_cast<std::int64_t>(y) + caption_before + box.style.border_width.top + spacing_y;
    if (!fits(cursor_y)) return -1;
    const int grid_start_y = static_cast<int>(cursor_y);
    for (auto& row : group->children) {
        int row_height = 0;
        int cursor_x = first_x;
        for (std::size_t index = 0; index < column_count; ++index) {
            LayoutBox& cell = *row->children[index];
            const Style saved = cell.style;
            // Width is owned by the qualified track for this call. Keep the
            // retained representation consistent, then restore the full Style.
            cell.style.computed_sizes.clear(SizeProperty::Width);
            cell.style.box_sizing_border_box = true;
            cell.style.width = tracks[index];
            cell.style.width_percent = -1;
            cell.style.margin = EdgeSizes{};
            const int cell_height = layout_box(cell, cursor_x, static_cast<int>(cursor_y), tracks[index], height, depth + 3);
            cell.style = saved;
            row_height = std::max(row_height, cell_height);
            const auto next_x = static_cast<std::int64_t>(cursor_x) + tracks[index] + spacing_x;
            if (!fits(next_x)) return -1;
            cursor_x = static_cast<int>(next_x);
        }
        for (auto& cell : row->children) {
            const int spare = std::max(0, row_height - cell->rect.height);
            int offset = 0;
            const TableVerticalAlign align = table_vertical_align(cell->style);
            if (align == TableVerticalAlign::Middle) offset = spare / 2;
            else if (align == TableVerticalAlign::Bottom) offset = spare;
            if (offset != 0) {
                for (auto& child : cell->children) shift_box(*child, 0, offset);
            }
            cell->rect.height = row_height;
        }
        row->rect = Rect{first_x, static_cast<int>(cursor_y), static_cast<int>(tracks_total +
            static_cast<std::int64_t>(column_count - 1) * spacing_x), row_height};
        cursor_y += static_cast<std::int64_t>(row_height) + spacing_y;
        if (!fits(cursor_y)) return -1;
    }
    if (!fits(cursor_y - grid_start_y)) return -1;
    group->rect = Rect{first_x, grid_start_y, table_width - border_x,
                       static_cast<int>(std::max<std::int64_t>(0, cursor_y - grid_start_y))};
    const auto grid_height = cursor_y - (static_cast<std::int64_t>(y) + caption_before);
    const auto table_height_value = caption_height + std::max<std::int64_t>(0, grid_height) + box.style.border_width.bottom;
    const auto flow_height = table_height_value + box.style.margin.top + box.style.margin.bottom;
    if (!fits(table_height_value) || !fits(flow_height) || flow_height < 0 ||
        !fits(static_cast<std::int64_t>(y) + table_height_value)) return -1;
    const int table_height = static_cast<int>(table_height_value);
    box.rect = Rect{table_x, y, table_width, table_height};
    if (caption != nullptr && table_caption_side(caption->style) == TableCaptionSide::Bottom) {
        const auto target_y = static_cast<std::int64_t>(y) + table_height - caption_height;
        if (!fits(target_y - caption->rect.y)) return -1;
        shift_box(*caption, 0, static_cast<int>(target_y - caption->rect.y));
    }
    transaction.commit();
    return static_cast<int>(flow_height);
}

int LayoutEngine::layout_box(LayoutBox& box, int x, int y, int width, int height, std::size_t depth,
                             const PreciseBoxContext* precision, std::int64_t* flow_units) const {
    if (precision && depth <= std::max<std::size_t>(1,options_.max_layout_depth) &&
        (box.style.display == Display::Block
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
         || (box.style.display == Display::Flex && !box.style.flex_wrap)
#endif
        ) &&
        box.node && box.node->type != NodeType::Text && !is_form_control(*box.node)) {
        return layout_precise_block(box,height,depth,*precision,flow_units);
    }
    if (depth > std::max<std::size_t>(1, options_.max_layout_depth)) {
        report_diagnostic(options_.diagnostics,
                          DiagnosticStage::Layout,
                          DiagnosticSeverity::Warning,
                          "layout-depth-limit",
                          "Layout depth budget was reached; remaining nested layout was skipped",
                          "Increase max_layout_depth or simplify deeply nested layout.");
        box.rect = Rect{x, y, 0, 0};
        return 0;
    }

    const int margin_left = box.style.margin_left_auto ? 0 : box.style.margin.left;
    const int margin_right = box.style.margin_right_auto ? 0 : box.style.margin.right;
    const int border_box_y = bounded_add(y, box.style.margin.top);
    const int containing_content_width = bounded_non_negative_subtract(
        bounded_subtract(width, margin_left), margin_right);
    const int available_content_width = resolved_content_width(box.style, containing_content_width);
    int content_width = available_content_width;
    const int max_content_width = resolved_max_content_width(box.style, containing_content_width);
    if (max_content_width >= 0) {
        content_width = std::min(content_width, max_content_width);
    }
    const int measured_border_box_width = bounded_non_negative_add(
        bounded_non_negative_add(content_width, horizontal_edges(box.style.padding)),
        horizontal_edges(box.style.border_width));
    const int min_width = resolved_min_width(box.style, containing_content_width);
    const bool fixed_border_box_width =
        box.style.box_sizing_border_box && (box.style.width >= 0 || box.style.width_percent >= 0);
    const int fixed_border_box_width_px = box.style.width_percent >= 0
        ? resolve_percent(containing_content_width, box.style.width_percent)
        : box.style.width;
    int preferred_border_box_width = fixed_border_box_width ? fixed_border_box_width_px : measured_border_box_width;
    if (max_content_width >= 0) {
        preferred_border_box_width = std::min(preferred_border_box_width, measured_border_box_width);
    }
    int border_box_width = std::max(min_width, preferred_border_box_width);
    const int auto_space = bounded_non_negative_subtract(
        bounded_non_negative_subtract(
            bounded_non_negative_subtract(width, border_box_width), margin_left), margin_right);
    int border_box_x = bounded_add(x, margin_left);
    if (box.style.margin_left_auto && box.style.margin_right_auto) {
        border_box_x = bounded_add(x, auto_space / 2);
    } else if (box.style.margin_left_auto) {
        border_box_x = bounded_add(x, auto_space);
    }
    const int content_x = bounded_add(bounded_add(border_box_x, box.style.border_width.left),
                                      box.style.padding.left);
    const int content_y = bounded_add(bounded_add(border_box_y, box.style.border_width.top),
                                      box.style.padding.top);
    int cursor_y = content_y;

    if (box.node != nullptr && box.node->type == NodeType::Text) {
        return layout_text_box(box, border_box_x, border_box_y, content_width, min_width, height);
    }

    if (box.style.display == Display::Table) {
        const int table_height = layout_fixed_table(box, x, border_box_y, containing_content_width, height, depth);
        if (table_height >= 0) {
            if (flow_units) *flow_units = static_cast<std::int64_t>(table_height) * 1000;
            return table_height;
        }
    }

    int max_child_width = 0;
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    const int children_height = box.style.display == Display::Flex
        ? layout_flex_box(box, content_x, cursor_y, content_width, height, depth)
        : box.style.display == Display::Grid
        ? layout_grid_box(box, content_x, cursor_y, content_width, height, depth)
        : has_only_inline_children(box)
#else
    const int children_height = has_only_inline_children(box)
#endif
        ? layout_inline_children(box, content_x, cursor_y, content_width, depth)
        : [&] {
            int flow_height = 0;
            for (auto& child : box.children) {
                if (is_out_of_flow_positioned(child->style)) {
                    child->content_geometry = {static_cast<std::int64_t>(content_x)*1000,
                        static_cast<std::int64_t>(cursor_y)*1000, -2, -2, false};
                    continue;
                }
                const int child_height = layout_box(*child, content_x, cursor_y, content_width, height, depth + 1);
                cursor_y = bounded_add(cursor_y, child_height);
                flow_height = bounded_add(flow_height, child_height);
                const int child_left = safe_edge(child->rect.x, -child->style.margin.left);
                const int child_right = safe_edge(safe_edge(child->rect.x, child->rect.width), child->style.margin.right);
                max_child_width = std::max(max_child_width, bounded_subtract(child_right, child_left));
            }
            return flow_height;
        }();
    if (box.style.width < 0 && box.style.width_percent < 0 &&
        (box.style.display == Display::Inline || box.style.display == Display::InlineBlock)) {
        if (!box.children.empty()) {
            int min_child_x = safe_edge(box.children.front()->rect.x, -box.children.front()->style.margin.left);
            int max_child_x = safe_edge(safe_edge(box.children.front()->rect.x, box.children.front()->rect.width),
                                        box.children.front()->style.margin.right);
            for (const auto& child : box.children) {
                min_child_x = std::min(min_child_x, safe_edge(child->rect.x, -child->style.margin.left));
                max_child_x = std::max(max_child_x,
                    safe_edge(safe_edge(child->rect.x, child->rect.width), child->style.margin.right));
            }
            max_child_width = std::max(0, bounded_subtract(max_child_x, min_child_x));
        }
        content_width = std::min(available_content_width, max_child_width);
        border_box_width = std::max(min_width,
            bounded_add(bounded_add(content_width, horizontal_edges(box.style.padding)),
                        horizontal_edges(box.style.border_width)));
        if (!box.children.empty()) {
            int min_child_x = bounded_subtract(box.children.front()->rect.x,
                                               box.children.front()->style.margin.left);
            for (const auto& child : box.children) {
                min_child_x = std::min(min_child_x,
                    bounded_subtract(child->rect.x, child->style.margin.left));
            }
            const int dx = bounded_subtract(content_x, min_child_x);
            if (dx != 0) {
                for (auto& child : box.children) {
                    shift_box(*child, dx, 0);
                }
            }
        }
    }

    const int intrinsic_control_height = box.node != nullptr && is_form_control(*box.node)
        ? (box.style.line_height > 0
              ? box.style.line_height
              : fallback_text_metrics({}, box.style.font_size, box.style.font_weight).line_height)
        : 0;
    const int aspect_ratio_height = has_aspect_ratio(box.style) && content_width > 0
        ? std::max(1, static_cast<int>(std::min<std::int64_t>(
              std::numeric_limits<int>::max(),
              (static_cast<std::int64_t>(content_width) * box.style.aspect_ratio_height +
               box.style.aspect_ratio_width / 2) / box.style.aspect_ratio_width)))
        : 0;
    const int fixed_content_height = specified_content_height(box.style, height);
    int content_height = std::max(specified_content_min_height(box.style, height),
        fixed_content_height >= 0
            ? fixed_content_height
            : std::max({children_height, intrinsic_control_height, aspect_ratio_height}));
    const int max_content_height = resolved_max_content_height(box.style, height);
    if (max_content_height >= 0) {
        content_height = std::min(content_height, max_content_height);
    }
    const int border_box_height = bounded_non_negative_add(
        bounded_non_negative_add(vertical_edges(box.style.border_width), vertical_edges(box.style.padding)),
        content_height);
    const int total_height = bounded_add(bounded_add(box.style.margin.top, border_box_height),
                                         box.style.margin.bottom);
    box.rect = Rect{border_box_x, border_box_y, border_box_width, border_box_height};
    box.content_geometry = {static_cast<std::int64_t>(content_x) * 1000,
        static_cast<std::int64_t>(content_y) * 1000, static_cast<std::int64_t>(content_width) * 1000,
        static_cast<std::int64_t>(content_height) * 1000, fixed_content_height >= 0};
    return total_height;
}

int LayoutEngine::layout_text_box(LayoutBox& box,
                                  int border_box_x,
                                  int border_box_y,
                                  int content_width,
                                  int min_width,
                                  int height) const {
    box.text_layout_cache = TextLayoutCache{};
    const std::string text = transformed_render_text(*box.node, box.style.text_transform);
    if (options_.text_system != nullptr) {
        TextLayoutCache& cache = box.text_layout_cache;
        cache.text_system = options_.text_system;
        cache.text_system_generation = options_.text_system->generation();
        cache.source_text = box.node->text;
        cache.rendered_text = text;
        cache.font_size = box.style.font_size;
        cache.font_weight = box.style.font_weight;
        cache.font_family_hash = box.style.font_family_hash;
        cache.text_indent = box.style.text_indent;
        cache.letter_spacing = box.style.letter_spacing;
        cache.text_transform = box.style.text_transform;
        cache.overflow_wrap_anywhere = box.style.overflow_wrap_anywhere;
        cache.white_space_nowrap = box.style.white_space_nowrap;
        cache.text_overflow_ellipsis = box.style.text_overflow_ellipsis;
        cache.text_align = box.style.text_align;
        const auto refuse = [&](const char* detail) {
            cache.valid = true;
            report_diagnostic(options_.diagnostics, DiagnosticStage::Layout,
                              DiagnosticSeverity::Warning, "layout-core-text-refused",
                              "Core TextSystem could not prepare this text layout", detail);
            box.rect = Rect{border_box_x, border_box_y, 0, 0};
            cache.rect_x = box.rect.x;
            cache.rect_y = box.rect.y;
            return 0;
        };
        if (box.style.letter_spacing != 0 || box.style.text_overflow_ellipsis ||
            box.style.font_size < 0 || box.style.font_size > std::numeric_limits<std::int32_t>::max() / 1000) {
            return refuse("letter-spacing, text-overflow ellipsis, or font-size is outside the prepared-text profile");
        }
        std::uint32_t family_id = 0;
        if (!options_.text_system->resolve_family_hash(box.style.font_family_hash, family_id)) {
            return refuse("font-family is not registered in the Core text catalog");
        }
        TextFontRequest request;
        request.families = &family_id;
        request.family_count = 1;
        request.size_q1000 = box.style.font_size * 1000;
        request.weight = static_cast<std::uint16_t>(std::clamp(box.style.font_weight, 1, 1000));
        TextRunMetrics run;
        const auto measured = options_.text_system->measure(text, request, run);
        if (measured != TextSystemStatus::Ok) return refuse("text measurement was refused by Core TextSystem");
        if (run.glyph_count > options_.max_prepared_text_glyphs)
            return refuse("text run exceeds the configured prepared-glyph limit");
        const auto ceil_px = [](std::int32_t value) {
            return value <= 0 ? 0 : static_cast<int>((static_cast<std::int64_t>(value) + 999) / 1000);
        };
        const int raw_text_width = ceil_px(run.advance);
        const int text_indent = std::max(0, std::min(box.style.text_indent, content_width));
        const int usable_text_width = bounded_non_negative_subtract(content_width, text_indent);
        const bool wraps = !box.style.white_space_nowrap &&
            (box.style.overflow_wrap_anywhere || has_text_wrap_opportunity(text));
        const bool needs_wrap = usable_text_width > 0 && raw_text_width > usable_text_width && wraps;
        std::vector<PreparedTextGlyph> glyphs(run.glyph_count);
        TextRunMetrics prepared;
        if (options_.text_system->prepare(text, request, glyphs.data(), glyphs.size(), prepared) != TextSystemStatus::Ok)
            return refuse("prepared glyphs could not be produced");

        struct CoreLineDraft { std::string text; std::int64_t advance = 0; };
        std::vector<CoreLineDraft> line_drafts;
        if (!needs_wrap) {
            line_drafts.push_back({text, run.advance});
        } else {
            struct TokenDraft {
                std::string text;
                std::int64_t advance = 0;
                std::size_t first_glyph = 0;
                std::size_t end_glyph = 0;
            };
            CoreLineDraft line;
            TokenDraft token;
            bool pending_space = false;
            TextRunMetrics space_metrics;
            if (measure_core_text(*options_.text_system, " ", box.style, space_metrics) != TextSystemStatus::Ok)
                return refuse("space advance could not be measured for line breaking");
            const auto emit_line = [&]() {
                if (!line.text.empty() || line_drafts.empty()) line_drafts.push_back(std::move(line));
                line = CoreLineDraft{};
            };
            const auto append_token = [&]() -> bool {
                if (token.text.empty()) return true;
                const std::int64_t separator = line.text.empty() || !pending_space ? 0 : space_metrics.advance;
                const std::int64_t candidate = line.advance + separator + token.advance;
                if (!line.text.empty() && candidate > static_cast<std::int64_t>(usable_text_width) * 1000) emit_line();
                if (box.style.overflow_wrap_anywhere && token.advance > static_cast<std::int64_t>(usable_text_width) * 1000) {
                    for (std::size_t glyph_index = token.first_glyph; glyph_index < token.end_glyph; ++glyph_index) {
                        const auto& glyph = glyphs[glyph_index];
                        if (!line.text.empty() && line.advance + glyph.geometry.advance > static_cast<std::int64_t>(usable_text_width) * 1000)
                            emit_line();
                        line.text.append(text.data() + glyph.source_begin, glyph.source_end - glyph.source_begin);
                        line.advance += glyph.geometry.advance;
                    }
                } else {
                    if (!line.text.empty() && pending_space) {
                        line.text.push_back(' ');
                        line.advance += space_metrics.advance;
                    }
                    line.text += token.text;
                    line.advance += token.advance;
                }
                token = TokenDraft{};
                pending_space = false;
                return true;
            };
            for (std::size_t glyph_index = 0; glyph_index < glyphs.size(); ++glyph_index) {
                const auto& glyph = glyphs[glyph_index];
                const std::uint32_t cp = glyph.source_codepoint;
                if (cp == ' ' || cp == '\t' || cp == '\r') {
                    if (!append_token()) return refuse("line token could not be prepared");
                    pending_space = !line.text.empty();
                    continue;
                }
                const auto byte_count = glyph.source_end - glyph.source_begin;
                if (token.text.empty()) token.first_glyph = glyph_index;
                token.end_glyph = glyph_index + 1;
                token.text.append(text.data() + glyph.source_begin, byte_count);
                token.advance += glyph.geometry.advance;
                if (is_cjk_codepoint(cp) || cp == '-' || cp == '/' || cp == 0x3001U || cp == 0x3002U) {
                    if (!append_token()) return refuse("line token could not be prepared");
                }
            }
            if (!append_token()) return refuse("line token could not be prepared");
            if (!line.text.empty() || line_drafts.empty()) emit_line();
        }
        const int text_width = std::max(min_width, std::min(usable_text_width, raw_text_width));
        const int line_height = box.style.line_height > 0 ? box.style.line_height : ceil_px(run.natural_height);
        const int fixed_text_height = specified_content_height(box.style, height);
        const int line_count = clamp_layout_value(static_cast<std::int64_t>(line_drafts.size()));
        int text_height = std::max(specified_content_min_height(box.style, height),
            fixed_text_height >= 0 ? fixed_text_height : bounded_non_negative_multiply(std::max(0, line_height), line_count));
        const int max_text_height = resolved_max_content_height(box.style, height);
        if (max_text_height >= 0) text_height = std::min(text_height, max_text_height);
        const int text_x = bounded_add(border_box_x, text_indent);
        box.rect = Rect{text_x, border_box_y, text_width, text_height};
        const int vertical_extent = ceil_px(run.ascent) + ceil_px(run.descent);
        const int leading = std::max(0, line_height - vertical_extent);
        cache.prepared_origin_x = text_x;
        cache.prepared_baseline_y = bounded_add(border_box_y, bounded_add(ceil_px(run.ascent), leading / 2));
        cache.available_width = usable_text_width;
        cache.content_width = content_width;
        cache.rect_x = box.rect.x;
        cache.rect_y = box.rect.y;
        cache.rect_width = box.rect.width;
        cache.rect_height = box.rect.height;
        cache.viewport_width = box.viewport_width;
        cache.viewport_height = box.viewport_height;
        cache.line_height = line_height;
        cache.lines.clear();
        cache.prepared_lines.clear();
        for (std::size_t line_index = 0; line_index < line_drafts.size(); ++line_index) {
            auto& line = line_drafts[line_index];
            cache.lines.push_back(line.text);
            std::vector<PreparedTextGlyph> line_glyphs(prepared.glyph_count);
            TextRunMetrics line_metrics;
            if (options_.text_system->prepare(line.text, request, line_glyphs.data(), line_glyphs.size(), line_metrics) != TextSystemStatus::Ok)
                return refuse("a wrapped text line could not be prepared");
            line_glyphs.resize(line_metrics.glyph_count);
            PreparedTextLine prepared_line;
            const int line_width = ceil_px(static_cast<std::int32_t>(std::min<std::int64_t>(line.advance,
                std::numeric_limits<std::int32_t>::max())));
            const int line_offset = box.style.text_align == TextAlign::Center
                ? std::max(0, bounded_subtract(usable_text_width, line_width) / 2)
                : box.style.text_align == TextAlign::End
                    ? std::max(0, bounded_subtract(usable_text_width, line_width)) : 0;
            prepared_line.origin_x = bounded_add(text_x, line_offset);
            prepared_line.baseline_y = bounded_add(border_box_y,
                bounded_add(bounded_non_negative_multiply(std::max(0, line_height), static_cast<int>(line_index)),
                            bounded_add(ceil_px(run.ascent), leading / 2)));
            prepared_line.width = line_width;
            if (!encode_prepared_text(line_glyphs.data(), line_glyphs.size(), request.size_q1000,
                                      static_cast<std::uint32_t>(line.text.size()), prepared_line.packet))
                return refuse("a wrapped text packet could not be encoded");
            cache.prepared_lines.push_back(std::move(prepared_line));
        }
        if (cache.prepared_lines.size() == 1) cache.prepared_packet = cache.prepared_lines.front().packet;
        cache.prepared_text = true;
        cache.valid = true;
        return text_height;
    }
    const TextMetrics metrics = measure_text_with_letter_spacing(text_measure_,
                                                                 text,
                                                                 box.style.font_size,
                                                                 box.style.font_weight,
                                                                 box.style.font_family_hash,
                                                                 box.style.letter_spacing);
    const int raw_text_width = metrics.width;
    const int text_indent = std::max(0, std::min(box.style.text_indent, content_width));
    const int usable_text_width = bounded_non_negative_subtract(content_width, text_indent);
    const int text_width = std::max(min_width, std::min(usable_text_width, raw_text_width));
    const int line_height = box.style.line_height > 0 ? box.style.line_height : metrics.line_height;
    const bool can_wrap = !box.style.white_space_nowrap &&
        (box.style.overflow_wrap_anywhere || has_text_wrap_opportunity(text));
    if (usable_text_width > 0 && raw_text_width > usable_text_width &&
        (box.style.white_space_nowrap || box.style.text_overflow_ellipsis || !can_wrap)) {
        report_diagnostic(options_.diagnostics,
                          DiagnosticStage::Layout,
                          DiagnosticSeverity::Warning,
                          box.style.text_overflow_ellipsis ? "layout-text-overflow-ellipsis" : "layout-text-overflow",
                          box.style.text_overflow_ellipsis
                              ? "Text measured wider than its layout box and will be truncated with an ellipsis"
                              : "Text measured wider than its layout box and will be clipped or visually degraded",
                          text_overflow_detail(box,
                                               text,
                                               raw_text_width,
                                               usable_text_width,
                                               content_width,
                                               text_indent));
    }
    std::vector<std::string> lines;
    int line_count = 1;
    if (can_wrap && usable_text_width > 0) {
        lines = box.style.overflow_wrap_anywhere
            ? wrap_text_anywhere(text_measure_,
                                 text,
                                 box.style.font_size,
                                 box.style.font_weight,
                                 box.style.font_family_hash,
                                 box.style.letter_spacing,
                                 usable_text_width)
            : wrap_text_at_opportunities(text_measure_,
                                         text,
                                         box.style.font_size,
                                         box.style.font_weight,
                                         box.style.font_family_hash,
                                         box.style.letter_spacing,
                                         usable_text_width);
        line_count = clamp_layout_value(static_cast<std::int64_t>(std::min<std::size_t>(
            lines.size(), static_cast<std::size_t>(std::numeric_limits<int>::max()))));
        line_count = std::max(1, line_count);
    }
    if (lines.empty() && !text.empty()) {
        lines.push_back(text);
    }
    const int fixed_text_height = specified_content_height(box.style, height);
    int text_height = std::max(specified_content_min_height(box.style, height),
        fixed_text_height >= 0
            ? fixed_text_height
            : bounded_non_negative_multiply(std::max(0, line_height), line_count));
    const int max_text_height = resolved_max_content_height(box.style, height);
    if (max_text_height >= 0) {
        text_height = std::min(text_height, max_text_height);
    }
    int text_x = bounded_add(border_box_x, text_indent);
    if (box.style.text_align == TextAlign::Center) {
        text_x = bounded_add(text_x,
                             std::max(0, bounded_subtract(usable_text_width, text_width) / 2));
    } else if (box.style.text_align == TextAlign::End) {
        text_x = bounded_add(text_x,
                             std::max(0, bounded_subtract(usable_text_width, text_width)));
    }
    box.rect = Rect{text_x, border_box_y, text_width, text_height};
    TextLayoutCache& cache = box.text_layout_cache;
    cache.valid = true;
    cache.source_text = box.node->text;
    cache.rendered_text = text;
    cache.lines = std::move(lines);
    cache.text_measure = text_measure_;
    cache.available_width = usable_text_width;
    cache.content_width = content_width;
    cache.rect_x = box.rect.x;
    cache.rect_y = box.rect.y;
    cache.rect_width = box.rect.width;
    cache.rect_height = box.rect.height;
    cache.viewport_width = box.viewport_width;
    cache.viewport_height = box.viewport_height;
    cache.font_size = box.style.font_size;
    cache.font_weight = box.style.font_weight;
    cache.font_family_hash = box.style.font_family_hash;
    cache.line_height = line_height;
    cache.text_indent = box.style.text_indent;
    cache.letter_spacing = box.style.letter_spacing;
    cache.text_transform = box.style.text_transform;
    cache.overflow_wrap_anywhere = box.style.overflow_wrap_anywhere;
    cache.white_space_nowrap = box.style.white_space_nowrap;
    cache.text_overflow_ellipsis = box.style.text_overflow_ellipsis;
    cache.text_align = box.style.text_align;
    return text_height;
}

int LayoutEngine::layout_inline_children(LayoutBox& box,
                                         int content_x,
                                         int content_y,
                                         int content_width,
                                         std::size_t depth) const {
    int cursor_x = bounded_add(content_x, std::max(0, std::min(box.style.text_indent, content_width)));
    int line_y = content_y;
    int line_height = 0;
    int line_start_x = cursor_x;
    std::size_t line_start_index = 0;

    const auto finish_line = [&](std::size_t line_end_index, int used_width) {
        if (line_end_index <= line_start_index || used_width <= 0) {
            return;
        }
        int dx = 0;
        const int line_capacity = std::max(0,
            bounded_subtract(bounded_add(content_x, content_width), line_start_x));
        if (box.style.text_align == TextAlign::Center) {
            dx = std::max(0, bounded_subtract(line_capacity, used_width) / 2);
        } else if (box.style.text_align == TextAlign::End) {
            dx = std::max(0, bounded_subtract(line_capacity, used_width));
        }
        if (dx == 0) {
            return;
        }
        for (std::size_t index = line_start_index; index < line_end_index; ++index) {
            shift_box(*box.children[index], dx, 0);
        }
    };

    for (std::size_t index = 0; index < box.children.size(); ++index) {
        auto& child = box.children[index];
        if (is_out_of_flow_positioned(child->style)) {
            child->content_geometry = {static_cast<std::int64_t>(cursor_x)*1000,
                static_cast<std::int64_t>(line_y)*1000, -2, -2, false};
            continue;
        }
        layout_box(*child, 0, 0, content_width, 0, depth + 1);
        const int child_outer_width = bounded_add(
            bounded_add(child->style.margin.left, child->rect.width), child->style.margin.right);
        const int child_outer_height = bounded_add(
            bounded_add(child->style.margin.top, child->rect.height), child->style.margin.bottom);
        const int remaining_width = std::max(0,
            bounded_subtract(bounded_add(content_x, content_width), cursor_x));

        if (cursor_x > line_start_x && child_outer_width > remaining_width) {
            finish_line(index, bounded_subtract(cursor_x, line_start_x));
            line_y = bounded_add(line_y, std::max(1, line_height));
            line_height = 0;
            cursor_x = content_x;
            line_start_x = cursor_x;
            line_start_index = index;
        }

        const int target_x = bounded_add(cursor_x, child->style.margin.left);
        const int target_y = bounded_add(line_y, child->style.margin.top);
        shift_box(*child, bounded_subtract(target_x, child->rect.x),
                  bounded_subtract(target_y, child->rect.y));
        cursor_x = bounded_add(cursor_x, child_outer_width);
        line_height = std::max(line_height, child_outer_height);
    }

    finish_line(box.children.size(), bounded_subtract(cursor_x, line_start_x));
    return bounded_add(bounded_subtract(line_y, content_y), std::max(0, line_height));
}

void LayoutEngine::layout_positioned_children(LayoutBox& box,
                                              const PositionedContext& parent,
                                              std::size_t depth) const {
    using namespace detail;
    if (depth > std::max<std::size_t>(1, options_.max_layout_depth)) return;
    const auto& style = box.style;
    const bool out_of_flow = is_out_of_flow_positioned(style);
    const auto decoration = [&](bool horizontal) {
        return static_cast<std::int64_t>(horizontal
            ? bounded_add(horizontal_edges(style.padding), horizontal_edges(style.border_width))
            : bounded_add(vertical_edges(style.padding), vertical_edges(style.border_width))) * 1000;
    };
    if (out_of_flow) {
        const bool fixed = style.position_type == PositionType::Fixed;
        const LayoutContentGeometry viewport{0, 0,
            static_cast<std::int64_t>(std::max(0, box.viewport_width)) * 1000,
            static_cast<std::int64_t>(std::max(0, box.viewport_height)) * 1000, true};
        const auto& area = fixed ? viewport : parent.containing;
        const auto left = resolve_inset(style, SizeProperty::InsetLeft, area.width);
        const auto right = resolve_inset(style, SizeProperty::InsetRight, area.width);
        const auto top = resolve_inset(style, SizeProperty::InsetTop, area.height());
        const auto bottom = resolve_inset(style, SizeProperty::InsetBottom, area.height());
        const auto horizontal_margins = static_cast<std::int64_t>(style.margin.left) * 1000 +
            static_cast<std::int64_t>(style.margin.right) * 1000;
        const auto vertical_margins = static_cast<std::int64_t>(style.margin.top) * 1000 +
            static_cast<std::int64_t>(style.margin.bottom) * 1000;
        PreciseBoxContext context{area.width, area.height(), 0, 0};
        context.border_origin = true;
        const bool stretch_width = computed_size_units(style, SizeProperty::Width, area.width) == -1 &&
            left.specified && right.specified;
        const bool stretch_height = computed_size_units(style, SizeProperty::Height, area.height()) == -1 &&
            top.specified && bottom.specified;
        if (stretch_width) {
            const auto fallback = std::clamp<std::int64_t>(area.width-left.units-right.units-
                horizontal_margins-decoration(true), 0, kDimensionPrecisionMaximum);
            context.allocated_width = computed_content_axis(style, false, area.width, fallback, decoration(true));
        }
        if (stretch_height) {
            const auto fallback = std::clamp<std::int64_t>(area.height()-top.units-bottom.units-
                vertical_margins-decoration(false), 0, kDimensionPrecisionMaximum);
            context.allocated_height = computed_content_axis(style, true, area.height(), fallback, decoration(false));
        }
        const auto original_sizes = style.computed_sizes;
        const int original_width = style.width, original_width_percent = style.width_percent;
        const int original_height = style.height, original_height_percent = style.height_percent;
        struct RestoreSizes {
            Style& style;
            ComputedSizeStorage sizes;
            int width, width_percent, height, height_percent;
            ~RestoreSizes() noexcept {
                style.width = width; style.width_percent = width_percent;
                style.height = height; style.height_percent = height_percent;
                style.computed_sizes = std::move(sizes);
            }
        } restore{box.style, original_sizes, original_width, original_width_percent,
                  original_height, original_height_percent};
        // Legacy modes receive the same allocation through scalar proxies.
        if (stretch_width) {
            box.style.computed_sizes.clear(SizeProperty::Width);
            box.style.width = project_size_edge(context.allocated_width +
                (style.box_sizing_border_box ? decoration(true) : 0));
            box.style.width_percent = -1;
        }
        if (stretch_height) {
            box.style.computed_sizes.clear(SizeProperty::Height);
            box.style.height = project_size_edge(context.allocated_height +
                (style.box_sizing_border_box ? decoration(false) : 0));
            box.style.height_percent = -1;
        }
        const auto layout_at = [&](std::int64_t x, std::int64_t y) {
            context.x = x; context.y = y;
            layout_box(box, bounded_subtract(project_size_edge(x), style.margin.left),
                bounded_subtract(project_size_edge(y), style.margin.top),
                project_size_edge(area.width), project_size_edge(area.height()), depth, &context);
        };
        const auto static_x = box.content_geometry.width == -2 ? box.content_geometry.x : parent.parent.x;
        const auto static_y = box.content_geometry.width == -2 ? box.content_geometry.y : parent.parent.y;
        const auto initial_x = (left.specified ? area.x+left.units : static_x) +
            static_cast<std::int64_t>(style.margin.left) * 1000;
        const auto initial_y = (top.specified ? area.y+top.units : static_y) +
            static_cast<std::int64_t>(style.margin.top) * 1000;
        layout_at(initial_x, initial_y);
        const auto border_width = box.content_geometry.width >= 0
            ? box.content_geometry.width + decoration(true) : static_cast<std::int64_t>(box.rect.width) * 1000;
        const auto border_height = box.content_geometry.height() >= 0
            ? box.content_geometry.height() + decoration(false) : static_cast<std::int64_t>(box.rect.height) * 1000;
        const auto target_x = !left.specified && right.specified
            ? area.x+area.width-right.units-border_width-static_cast<std::int64_t>(style.margin.right)*1000
            : initial_x;
        const auto target_y = !top.specified && bottom.specified
            ? area.y+area.height()-bottom.units-border_height-static_cast<std::int64_t>(style.margin.bottom)*1000
            : initial_y;
        // Opposite-edge placement needs measured extent before its final origin.
        // Re-layout keeps fractional projected extents and prepared text coherent.
        if (target_x != initial_x || target_y != initial_y) layout_at(target_x, target_y);
    } else if (style.position_type == PositionType::Relative) {
        const auto left = resolve_inset(style, SizeProperty::InsetLeft, parent.parent.width);
        const auto right = resolve_inset(style, SizeProperty::InsetRight, parent.parent.width);
        const auto top = resolve_inset(style, SizeProperty::InsetTop, parent.parent.height(),
            parent.parent.height_definite());
        const auto bottom = resolve_inset(style, SizeProperty::InsetBottom, parent.parent.height(),
            parent.parent.height_definite());
        const auto dx = left.specified ? left.units : right.specified ? -right.units : 0;
        const auto dy = top.specified ? top.units : bottom.specified ? -bottom.units : 0;
        shift_box_precise(box, dx, dy);
    }

    auto content = box.content_geometry;
    if (content.width < 0 || content.height() < 0) {
        // Structural fixed-table boxes have integral owner-measured geometry;
        // their owner does not call layout_box for these grid wrappers.
        content = {static_cast<std::int64_t>(box.rect.x) * 1000 +
                (static_cast<std::int64_t>(style.border_width.left)+style.padding.left)*1000,
            static_cast<std::int64_t>(box.rect.y) * 1000 +
                (static_cast<std::int64_t>(style.border_width.top)+style.padding.top)*1000,
            std::max<std::int64_t>(0, static_cast<std::int64_t>(box.rect.width)*1000-decoration(true)),
            std::max<std::int64_t>(0, static_cast<std::int64_t>(box.rect.height)*1000-decoration(false)), false};
    }
    if (!out_of_flow && !parent.parent.height_definite()) {
        SizeKind kind; std::int64_t units = 0, percentage = 0;
        if (style.height_percent >= 0 ||
            (style.computed_sizes.get(SizeProperty::Height, kind, units, percentage) && kind != SizeKind::Pixels))
            content.set_height_definite(false);
    }
    PositionedContext descendants{parent.containing, content};
    if (style.position_type != PositionType::Static) {
        // Positioned ordinary blocks establish their padding box. Transformed and
        // fragmented inline containing blocks remain outside this layout subset.
        descendants.containing = {content.x-static_cast<std::int64_t>(style.padding.left)*1000,
            content.y-static_cast<std::int64_t>(style.padding.top)*1000,
            std::min(kDimensionPrecisionMaximum, content.width+static_cast<std::int64_t>(horizontal_edges(style.padding))*1000),
            std::min(kDimensionPrecisionMaximum, content.height()+static_cast<std::int64_t>(vertical_edges(style.padding))*1000), true};
    }
    for (auto& child : box.children) layout_positioned_children(*child, descendants, depth+1);
}

#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
int LayoutEngine::layout_flex_box(LayoutBox& box,
                                  int content_x,
                                  int content_y,
                                  int content_width,
                                  int containing_height,
                                  std::size_t depth) const {
    const auto in_flow_count = static_cast<int>(std::count_if(box.children.begin(), box.children.end(),
        [](const LayoutBoxPtr& child) { return !is_out_of_flow_positioned(child->style); }));
    if (in_flow_count == 0) {
        return 0;
    }
    const std::vector<LayoutBox*> ordered_children = ordered_flex_children(box);
    const auto for_each_in_flow_child = [&](const auto& callback) {
        if (ordered_children.empty()) {
            for (const LayoutBoxPtr& child : box.children) {
                if (!is_out_of_flow_positioned(child->style)) {
                    callback(*child);
                }
            }
        } else {
            for (LayoutBox* child : ordered_children) {
                callback(*child);
            }
        }
    };

    if (box.style.flex_direction == FlexDirection::Column) {
        struct ColumnFlexItem {
            LayoutBox* child = nullptr;
            int base_height = 0;
            int target_height = 0;
            bool force_height = false;
            int probe_height = 0;
            bool probe_forced_height = false;
        };

        const int fixed_height = specified_content_height(box.style, containing_height);
        const int minimum_height = std::max(0, specified_content_min_height(box.style, containing_height));
        const int child_containing_height = fixed_height >= 0 ? fixed_height : 0;
        const auto layout_child_for_size = [&](LayoutBox& child, int target_height, bool force_height) {
            const int old_height = child.style.height;
            const int old_height_percent = child.style.height_percent;
            if (force_height) {
                child.style.height = std::max(0, target_height);
                child.style.height_percent = -1;
            } else if (fixed_height<0 && child.style.flex_basis_kind==FlexBasisKind::Percentage) {
                // An unresolved percentage is content, not auto: ignore the
                // preferred main size during every natural-size probe/layout.
                child.style.height = -1;
                child.style.height_percent = -1;
            }
            const int child_height = layout_box(child, 0, 0, content_width, child_containing_height, depth + 1);
            child.style.height = old_height;
            child.style.height_percent = old_height_percent;
            return child_height;
        };

        std::vector<ColumnFlexItem> items;
        items.reserve(box.children.size());
        int total_base_height = 0;
        int total_margin_height = 0;
        int total_grow = 0;
        std::int64_t total_shrink_weight = 0;

        for_each_in_flow_child([&](LayoutBox& child) {
            const int basis=resolved_flex_basis_pixels(child.style,fixed_height);
            const bool content_basis=fixed_height<0 && child.style.flex_basis_kind==FlexBasisKind::Percentage;
            const bool use_basis = basis >= 0;
            const bool flexible_zero_basis = !use_basis && !content_basis && child.style.flex_grow > 0 && child.style.height < 0;
            const int probe_height = use_basis ? basis : 0;
            const bool force_height = use_basis || flexible_zero_basis;
            layout_child_for_size(child, probe_height, force_height);

            int base_height = use_basis ? basis : flexible_zero_basis ? 0 : child.rect.height;
            if (child.style.height >= 0 && !use_basis && !content_basis) {
                base_height = child.style.height;
            }
            base_height = std::max(std::max(0, child.style.min_height), base_height);
            ColumnFlexItem item;
            item.child = &child;
            item.base_height = base_height;
            item.target_height = base_height;
            item.force_height = force_height;
            item.probe_height = probe_height;
            item.probe_forced_height = force_height;
            items.push_back(item);

            total_base_height = bounded_add(total_base_height, base_height);
            total_margin_height = bounded_add(total_margin_height,
                                              bounded_add(child.style.margin.top,
                                                          child.style.margin.bottom));
            total_grow = bounded_add(total_grow, std::max(0, child.style.flex_grow));
            total_shrink_weight += static_cast<std::int64_t>(std::max(0, child.style.flex_shrink)) *
                std::max(1, base_height);
        });

        const int total_gap_height = bounded_non_negative_multiply(
            box.style.row_gap, std::max(0, in_flow_count - 1));
        const int natural_height = bounded_add(
            bounded_add(total_base_height, total_margin_height), total_gap_height);
        const int container_height = fixed_height >= 0 ? fixed_height : std::max(minimum_height, natural_height);
        const int available_item_height = bounded_non_negative_subtract(
            bounded_non_negative_subtract(container_height, total_margin_height), total_gap_height);
        const int free_space = bounded_subtract(available_item_height, total_base_height);

        if (free_space > 0 && total_grow > 0) {
            int remaining_space = free_space;
            int remaining_grow = total_grow;
            for (ColumnFlexItem& item : items) {
                const int grow = std::max(0, item.child->style.flex_grow);
                if (grow <= 0) {
                    continue;
                }
                const int share = remaining_grow == grow
                    ? remaining_space
                    : static_cast<int>((static_cast<std::int64_t>(remaining_space) * grow) / remaining_grow);
                remaining_space -= share;
                remaining_grow -= grow;
                item.target_height = std::max(0, bounded_add(item.base_height, share));
                item.force_height = true;
            }
        } else if (free_space < 0 && total_shrink_weight > 0) {
            int remaining_deficit = bounded_subtract(0, free_space);
            std::int64_t remaining_weight = total_shrink_weight;
            for (ColumnFlexItem& item : items) {
                const std::int64_t weight = static_cast<std::int64_t>(std::max(0, item.child->style.flex_shrink)) *
                    std::max(1, item.base_height);
                if (weight <= 0) {
                    continue;
                }
                const int share = remaining_weight == weight
                    ? remaining_deficit
                    : static_cast<int>((static_cast<std::int64_t>(remaining_deficit) * weight) / remaining_weight);
                remaining_deficit -= share;
                remaining_weight -= weight;
                item.target_height = std::max(std::max(0, item.child->style.min_height),
                                              bounded_subtract(item.base_height, share));
                item.force_height = true;
            }
        }

        int total_child_height = bounded_add(total_margin_height, total_gap_height);
        for (ColumnFlexItem& item : items) {
            if (item.force_height != item.probe_forced_height ||
                (item.force_height && item.target_height != item.probe_height)) {
                layout_child_for_size(*item.child, item.target_height, item.force_height);
            }
            total_child_height = bounded_add(total_child_height, item.child->rect.height);
        }

        const FlexJustifyPlacement placement = flex_justify_placement(box.style.justify_content,
                                                                      content_y,
                                                                      container_height,
                                                                      total_child_height,
                                                                      in_flow_count);
        int cursor_y = placement.cursor;
        for (ColumnFlexItem& item : items) {
            LayoutBox* child = item.child;
            const AlignItems alignment = flex_cross_axis_alignment(*child, box.style.align_items);
            if (alignment == AlignItems::Stretch && flex_cross_size_is_auto(child->style, false)) {
                const int old_width = child->style.width;
                const int old_width_percent = child->style.width_percent;
                set_flex_stretch_cross_size(child->style, content_width, false);
                layout_child_for_size(*child, item.target_height, item.force_height);
                child->style.width = old_width;
                child->style.width_percent = old_width_percent;
            }
            const int target_x = flex_aligned_x(alignment,
                                                content_x,
                                                content_width,
                                                flex_outer_cross_size(*child, false));
            const int dx = bounded_subtract(bounded_add(target_x, child->style.margin.left),
                                            child->rect.x);
            const int dy = bounded_subtract(bounded_add(cursor_y, child->style.margin.top),
                                            child->rect.y);
            shift_box(*child, dx, dy);
            cursor_y = bounded_add(
                cursor_y,
                bounded_add(
                    bounded_add(
                        bounded_add(child->rect.height, child->style.margin.top),
                        child->style.margin.bottom),
                    bounded_add(placement.extra_gap, box.style.row_gap)));
        }
        return container_height;
    }

    const auto layout_child_for_width = [&](LayoutBox& child, int target_width, bool force_width) {
        const int old_width = child.style.width;
        const int old_width_percent = child.style.width_percent;
        if (force_width) {
            child.style.width = std::max(0, target_width);
            child.style.width_percent = -1;
        }
        const int height = layout_box(child, 0, 0, std::max(0, target_width), 0, depth + 1);
        child.style.width = old_width;
        child.style.width_percent = old_width_percent;
        return height;
    };

    if (box.style.flex_wrap) {
        return layout_wrapped_flex_children(box, content_x, content_y, content_width,
                                            layout_child_for_width, ordered_children);
    }

    std::vector<FlexLayoutItem> items;
    items.reserve(box.children.size());
    int total_base_width = 0;
    int total_margin_width = 0;
    int max_child_height = 0;
    int total_grow = 0;
    std::int64_t total_shrink_weight = 0;
    detail::IntrinsicSourceQueryLimits intrinsic_limits;
    detail::IntrinsicSourceQueryUsage intrinsic_usage;
    bool intrinsic_queries_complete = true;

    for_each_in_flow_child([&](LayoutBox& child) {
        const int basis=resolved_flex_basis_pixels(child.style,content_width);
        const bool use_basis = basis >= 0;
        const bool flexible_zero_basis = !use_basis && child.style.flex_grow > 0 && child.style.width < 0;
        const int probe_width = use_basis ? basis : flexible_zero_basis ? 0 : content_width;
        const bool force_width = use_basis || flexible_zero_basis;
        layout_child_for_width(child, probe_width, force_width);

        int base_width = use_basis ? basis : flexible_zero_basis ? 0 : child.rect.width;
        if (child.style.width >= 0 && !use_basis) {
            base_width = child.style.width;
        }
        base_width = std::max(std::max(0, child.style.min_width), base_width);
        FlexLayoutItem item;
        item.child = &child;
        item.base_width = base_width;
        item.target_width = base_width;
        const auto declared_minimum = detail::computed_size_units(child.style,
            detail::SizeProperty::MinWidth, static_cast<std::int64_t>(content_width) * 1000);
        detail::SizeKind minimum_kind = detail::SizeKind::Pixels;
        std::int64_t minimum_value = 0;
        const bool retained_minimum = child.style.computed_sizes.get(
            detail::SizeProperty::MinWidth, minimum_kind, minimum_value);
        const bool unsupported_minimum = declared_minimum < -1 ||
            (retained_minimum && minimum_kind == detail::SizeKind::LengthPercentage);
        const auto has_mixed_constraint = [&](detail::SizeProperty property) {
            detail::SizeKind kind = detail::SizeKind::Pixels;
            std::int64_t value = 0;
            return child.style.computed_sizes.get(property, kind, value) &&
                kind == detail::SizeKind::LengthPercentage;
        };
        const bool unsupported_intrinsic_constraints = unsupported_minimum ||
            has_mixed_constraint(detail::SizeProperty::Width) ||
            has_mixed_constraint(detail::SizeProperty::MaxWidth) ||
            has_mixed_constraint(detail::SizeProperty::Height) ||
            has_mixed_constraint(detail::SizeProperty::MinHeight) ||
            has_mixed_constraint(detail::SizeProperty::MaxHeight);
        const std::int64_t main_edges = (static_cast<std::int64_t>(child.style.padding.left) +
            child.style.padding.right + child.style.border_width.left +
            child.style.border_width.right) * 1000;
        const auto to_border_box = [&](std::int64_t value) {
            if (value < 0) return value;
            return child.style.box_sizing_border_box ? std::max(main_edges, value) : value + main_edges;
        };
        item.minimum_width = !unsupported_minimum && declared_minimum >= 0
            ? static_cast<int>(std::clamp<std::int64_t>((to_border_box(declared_minimum) + 999) / 1000, 0,
                std::numeric_limits<int>::max()))
            : std::max(0, child.style.min_width);
        const auto auto_min_kind = detail::flex_auto_min_kind(child.style,
            detail::FlexMainAxis::Horizontal);
        if (intrinsic_queries_complete && !unsupported_intrinsic_constraints && declared_minimum < 0 &&
            options_.text_system != nullptr &&
            auto_min_kind == detail::FlexAutoMinKind::Content && !has_aspect_ratio(child.style)) {
            auto remaining = intrinsic_limits;
            remaining.max_boxes -= intrinsic_usage.boxes;
            remaining.max_source_bytes -= intrinsic_usage.source_bytes;
            remaining.max_measurements -= intrinsic_usage.measurements;
            remaining.max_work -= intrinsic_usage.work;
            const auto measured = detail::query_intrinsic_source_width(child, *options_.text_system,
                {detail::IntrinsicPercentageBasisKind::CyclicSameAxis, -1}, remaining);
            const bool accounted = intrinsic_usage.consume(measured, intrinsic_limits);
            if (!accounted || measured.status != detail::IntrinsicSourceStatus::Complete) {
                intrinsic_queries_complete = false;
            } else {
            if (measured.status == detail::IntrinsicSourceStatus::Complete) {
                const auto preferred = detail::computed_size_units(child.style,
                    detail::SizeProperty::Width, static_cast<std::int64_t>(content_width) * 1000);
                const auto maximum = detail::computed_size_units(child.style,
                    detail::SizeProperty::MaxWidth, static_cast<std::int64_t>(content_width) * 1000);
                const auto min_height = detail::computed_size_units(child.style,
                    detail::SizeProperty::MinHeight, -1);
                const auto max_height = detail::computed_size_units(child.style,
                    detail::SizeProperty::MaxHeight, -1);
                const auto preferred_height = detail::computed_size_units(child.style,
                    detail::SizeProperty::Height, -1);
                if (preferred >= -1 && maximum >= -1 && min_height >= -1 &&
                    max_height >= -1 && preferred_height >= -1) {
                    detail::FlexAutoMinInput input;
                    input.content_size_units = measured.min_content_border_box_units;
                    input.content_complete = true;
                    input.specified_main_units = to_border_box(preferred);
                    input.max_main_units = to_border_box(maximum);
                    input.main_decoration_units = main_edges;
                    input.preferred_cross_units = preferred_height;
                    input.min_cross_units = min_height;
                    input.max_cross_units = max_height;
                    const auto automatic = detail::flex_auto_minimum(input);
                    if (automatic.status == detail::FlexAutoMinStatus::Complete) {
                        item.intrinsic_minimum_width = static_cast<int>(
                            std::clamp<std::int64_t>((automatic.minimum_border_box_units + 999) / 1000,
                                0, std::numeric_limits<int>::max()));
                    }
                }
            }
            }
        }
        item.force_width = force_width;
        item.probe_width = probe_width;
        item.probe_forced_width = force_width;
        items.push_back(item);

        total_base_width = bounded_add(total_base_width, base_width);
        total_margin_width = bounded_add(total_margin_width,
                                         bounded_add(child.style.margin.left,
                                                     child.style.margin.right));
        total_grow = bounded_add(total_grow, flex_grow_factor(item));
        total_shrink_weight += flex_shrink_weight(item);
    });

    for (auto& item : items) {
        if (intrinsic_queries_complete)
            item.minimum_width = std::max(item.minimum_width, item.intrinsic_minimum_width);
        item.base_width = std::max(item.base_width, item.minimum_width);
        item.target_width = item.base_width;
    }
    total_base_width = 0;
    total_grow = 0;
    total_shrink_weight = 0;
    for (const auto& item : items) {
        total_base_width = bounded_add(total_base_width, item.base_width);
        total_grow = bounded_add(total_grow, flex_grow_factor(item));
        total_shrink_weight += flex_shrink_weight(item);
    }

    const int total_gap_width = bounded_non_negative_multiply(
        box.style.column_gap, std::max(0, in_flow_count - 1));
    const int available_item_width = bounded_non_negative_subtract(
        bounded_non_negative_subtract(content_width, total_margin_width), total_gap_width);
    const int free_space = bounded_subtract(available_item_width, total_base_width);
    distribute_flex_row_widths(items, free_space, total_grow, total_shrink_weight);

    int total_child_width = bounded_add(total_margin_width, total_gap_width);
    for (FlexLayoutItem& item : items) {
        if (item.force_width != item.probe_forced_width ||
            (item.force_width && item.target_width != item.probe_width)) {
            layout_child_for_width(*item.child, item.target_width, item.force_width);
        }
        total_child_width = bounded_add(total_child_width, item.child->rect.width);
        max_child_height = std::max(max_child_height,
            bounded_add(
                bounded_add(item.child->rect.height, item.child->style.margin.top),
                item.child->style.margin.bottom));
    }

    const FlexJustifyPlacement placement = flex_justify_placement(box.style.justify_content,
                                                                  content_x,
                                                                  content_width,
                                                                  total_child_width,
                                                                  in_flow_count);
    int cursor_x = placement.cursor;

    const int container_height = std::max(box.style.min_height, box.style.height >= 0 ? box.style.height : max_child_height);
    for (FlexLayoutItem& item : items) {
        LayoutBox* child = item.child;
        const AlignItems alignment = flex_cross_axis_alignment(*child, box.style.align_items);
        if (alignment == AlignItems::Stretch && flex_cross_size_is_auto(child->style, true)) {
            const int old_height = child->style.height;
            const int old_height_percent = child->style.height_percent;
            set_flex_stretch_cross_size(child->style, container_height, true);
            layout_child_for_width(*child, item.target_width, item.force_width);
            child->style.height = old_height;
            child->style.height_percent = old_height_percent;
        }
        const int child_outer_height = flex_outer_cross_size(*child, true);
        const int target_y = bounded_add(
            flex_aligned_y(alignment, content_y, container_height, child_outer_height),
            child->style.margin.top);

        const int dx = bounded_subtract(bounded_add(cursor_x, child->style.margin.left),
                                        child->rect.x);
        const int dy = bounded_subtract(target_y, child->rect.y);
        shift_box(*child, dx, dy);
        cursor_x = bounded_add(
            cursor_x,
            bounded_add(
                bounded_add(
                    bounded_add(child->rect.width, child->style.margin.left),
                    child->style.margin.right),
                bounded_add(placement.extra_gap, box.style.column_gap)));
    }

    return container_height;
}

int LayoutEngine::layout_grid_box(LayoutBox& box,
                                  int content_x,
                                  int content_y,
                                  int content_width,
                                  int containing_height,
                                  std::size_t depth) const {
    const auto in_flow_count = static_cast<int>(std::count_if(box.children.begin(), box.children.end(),
        [](const LayoutBoxPtr& child) { return !is_out_of_flow_positioned(child->style); }));
    if (in_flow_count == 0) {
        return 0;
    }

    const int row_gap = std::max(0, box.style.row_gap);
    GridColumns columns = resolve_grid_columns(box.style, content_width, in_flow_count);

    GridPlacementState placement_state;
    if (box.style.grid_template_row_count > 0) {
        const int template_rows = std::min({static_cast<int>(box.style.grid_template_row_count),
                                            static_cast<int>(box.style.grid_template_row_heights.size()),
                                            kMaxGridRows});
        placement_state.occupied.assign(static_cast<std::size_t>(template_rows), 0);
        placement_state.row_heights.reserve(static_cast<std::size_t>(template_rows));
        placement_state.row_fixed.reserve(static_cast<std::size_t>(template_rows));
        for (int row = 0; row < template_rows; ++row) {
            const int track = box.style.grid_template_row_heights[static_cast<std::size_t>(row)];
            placement_state.row_heights.push_back(std::max(0, track));
            placement_state.row_fixed.push_back(track > 0);
        }
        const int fixed_content_height = specified_content_height(box.style, containing_height);
        if (fixed_content_height >= 0) {
            int fixed_height = bounded_non_negative_multiply(row_gap, std::max(0, template_rows - 1));
            int flexible_count = 0;
            for (int row = 0; row < template_rows; ++row) {
                if (placement_state.row_fixed[static_cast<std::size_t>(row)]) {
                    fixed_height = bounded_add(
                        fixed_height, placement_state.row_heights[static_cast<std::size_t>(row)]);
                } else {
                    ++flexible_count;
                }
            }
            if (flexible_count > 0) {
                const int flexible_height = std::max(
                    1, bounded_subtract(fixed_content_height, fixed_height) / flexible_count);
                for (int row = 0; row < template_rows; ++row) {
                    if (!placement_state.row_fixed[static_cast<std::size_t>(row)]) {
                        placement_state.row_heights[static_cast<std::size_t>(row)] = flexible_height;
                    }
                }
            }
        }
    }
    std::vector<GridPlacement> placements;
    placements.reserve(static_cast<std::size_t>(in_flow_count));
    bool grid_placement_budget_reported = false;

    for (auto& child : box.children) {
        if (is_out_of_flow_positioned(child->style)) {
            continue;
        }
        const int column_span = std::max(1, std::min(static_cast<int>(child->style.grid_column_span), columns.count));
        const int row_span = std::max(1, static_cast<int>(child->style.grid_row_span));
        GridPlacement placement = place_grid_item(placement_state,
                                                  columns.count,
                                                  column_span,
                                                  row_span,
                                                  box.style.grid_auto_row_min,
                                                  child->style.grid_column_start,
                                                  child->style.grid_row_start);
        placement.child = child.get();

        if (placement.fallback_block) {
            if (!grid_placement_budget_reported) {
                report_diagnostic(options_.diagnostics,
                                  DiagnosticStage::Layout,
                                  DiagnosticSeverity::Warning,
                                  "grid-placement-budget",
                                  "Grid placement exceeded the bounded row budget; later items use block-flow fallback",
                                  "maximum tracked rows=" + std::to_string(kMaxGridRows));
                grid_placement_budget_reported = true;
            }
        }

        placements.push_back(placement);
    }

    finalize_grid_columns(columns, placements, text_measure_, options_.text_system, content_width);

    for (const GridPlacement& placement : placements) {
        LayoutBox& child = *placement.child;
        if (placement.fallback_block) {
            layout_box(child, 0, 0, content_width, 0, depth + 1);
            continue;
        }
        const int column_span = placement.column_span;
        const int row_span = placement.row_span;
        const int item_width = grid_item_width(columns, placement.column, column_span);
        const int original_width = child.style.width;
        const int original_width_percent = child.style.width_percent;
        const bool original_box_sizing = child.style.box_sizing_border_box;
        if (child.style.width < 0 && child.style.width_percent < 0) {
            child.style.width = item_width;
            child.style.width_percent = -1;
            child.style.box_sizing_border_box = true;
        }
        const int child_height = layout_box(child, 0, 0, item_width, 0, depth + 1);
        child.style.width = original_width;
        child.style.width_percent = original_width_percent;
        child.style.box_sizing_border_box = original_box_sizing;
        const int min_allocated_height = bounded_add(
            bounded_non_negative_multiply(box.style.grid_auto_row_min, row_span),
            bounded_non_negative_multiply(row_gap, row_span - 1));
        const int allocated_height = std::max(child_height, min_allocated_height);
        const int row_gaps = bounded_non_negative_multiply(row_gap, row_span - 1);
        const int per_row_height = std::max(1,
            bounded_add(bounded_subtract(allocated_height, row_gaps), row_span - 1) / row_span);
        for (int r = placement.row; r < placement.row + row_span; ++r) {
            if (!placement_state.row_fixed[static_cast<std::size_t>(r)]) {
                placement_state.row_heights[static_cast<std::size_t>(r)] =
                    std::max(placement_state.row_heights[static_cast<std::size_t>(r)], per_row_height);
            }
        }
    }

    const int fixed_content_height = specified_content_height(box.style, containing_height);
    if (fixed_content_height >= 0 && !placement_state.row_heights.empty()) {
        int occupied_height = bounded_non_negative_multiply(
            row_gap, std::max(0, static_cast<int>(placement_state.row_heights.size()) - 1));
        int flexible_count = 0;
        for (std::size_t row = 0; row < placement_state.row_heights.size(); ++row) {
            occupied_height = bounded_add(occupied_height, placement_state.row_heights[row]);
            if (!placement_state.row_fixed[row]) {
                ++flexible_count;
            }
        }
        int remaining_height = bounded_non_negative_subtract(fixed_content_height, occupied_height);
        int remaining_flexible = flexible_count;
        for (std::size_t row = 0;
             row < placement_state.row_heights.size() && remaining_height > 0 && remaining_flexible > 0;
             ++row) {
            if (placement_state.row_fixed[row]) {
                continue;
            }
            const int share = remaining_height / remaining_flexible;
            placement_state.row_heights[row] = bounded_add(placement_state.row_heights[row], share);
            remaining_height -= share;
            --remaining_flexible;
        }
    }

    std::vector<int> row_offsets(placement_state.row_heights.size(), 0);
    int total_height = 0;
    for (std::size_t row = 0; row < placement_state.row_heights.size(); ++row) {
        row_offsets[row] = total_height;
        total_height = bounded_add(total_height, placement_state.row_heights[row]);
        if (row + 1 < placement_state.row_heights.size()) {
            total_height = bounded_add(total_height, row_gap);
        }
    }

    int fallback_y = total_height;
    const bool has_grid_rows = !placement_state.row_heights.empty();
    const bool has_fallback = std::any_of(placements.begin(), placements.end(),
                                          [](const GridPlacement& placement) {
                                              return placement.fallback_block;
                                          });
    if (has_grid_rows && has_fallback) {
        fallback_y = bounded_add(fallback_y, row_gap);
    }

    for (const GridPlacement& placement : placements) {
        if (placement.fallback_block) {
            const int target_x = bounded_add(content_x, placement.child->style.margin.left);
            const int target_y = bounded_add(
                bounded_add(content_y, fallback_y), placement.child->style.margin.top);
            shift_box(*placement.child,
                      bounded_subtract(target_x, placement.child->rect.x),
                      bounded_subtract(target_y, placement.child->rect.y));
            const int fallback_height = bounded_add(
                bounded_add(placement.child->rect.height, placement.child->style.margin.top),
                placement.child->style.margin.bottom);
            fallback_y = bounded_add(fallback_y, bounded_add(std::max(1, fallback_height), row_gap));
            continue;
        }
        int allocated_height = 0;
        for (int r = 0; r < placement.row_span; ++r) {
            allocated_height = bounded_add(
                allocated_height,
                placement_state.row_heights[static_cast<std::size_t>(placement.row + r)]);
        }
        allocated_height = bounded_add(
            allocated_height, bounded_non_negative_multiply(row_gap, placement.row_span - 1));
        const AlignItems alignment = flex_cross_axis_alignment(*placement.child, box.style.align_items);
        const int row_y = bounded_add(content_y, row_offsets[static_cast<std::size_t>(placement.row)]);
        const int target_x = bounded_add(
            grid_column_x(columns, content_x, placement.column), placement.child->style.margin.left);
        if (alignment == AlignItems::Stretch && flex_cross_size_is_auto(placement.child->style, true)) {
            const int stretched_height = bounded_non_negative_subtract(
                bounded_non_negative_subtract(allocated_height, placement.child->style.margin.top),
                placement.child->style.margin.bottom);
            placement.child->rect.height = std::max(placement.child->rect.height, stretched_height);
        }
        const int child_outer_height = flex_outer_cross_size(*placement.child, true);
        const int target_y = bounded_add(
            flex_aligned_y(alignment, row_y, allocated_height, child_outer_height),
            placement.child->style.margin.top);
        shift_box(*placement.child,
                  bounded_subtract(target_x, placement.child->rect.x),
                  bounded_subtract(target_y, placement.child->rect.y));
        placement.child->rect.width = grid_item_width(columns, placement.column, placement.column_span);
    }

    return has_fallback ? std::max(total_height, fallback_y - row_gap) : total_height;
}
#endif

std::size_t count_layout_boxes(const LayoutBox& root) {
    std::size_t count = 0;
    std::vector<const LayoutBox*> pending;
    pending.push_back(&root);
    while (!pending.empty()) {
        const LayoutBox* current = pending.back();
        pending.pop_back();
        ++count;
        for (const auto& child : current->children) {
            pending.push_back(child.get());
        }
    }
    return count;
}

} // namespace jellyframe
