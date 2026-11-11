#include "intrinsic_source_query.h"

#include "core_text_measure.h"
#include "css_dimension_precision.h"
#include "render_core/text_scan.h"
#include "utf8_scalar.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string_view>

namespace jellyframe::detail {
namespace {

constexpr char kSoftBreak = '\x1e';
constexpr char kHardBreak = '\x1f';
constexpr std::size_t kHardMaxBoxes = 4096;
constexpr std::size_t kHardMaxDepth = 64;
constexpr std::size_t kHardMaxSourceBytes = 65536;
constexpr std::size_t kHardMaxMeasurements = 4096;
constexpr std::size_t kHardMaxWork = 1024U * 1024U;
constexpr std::size_t kHardMaxScratchBytes = 65536;

bool zero_edges(const EdgeSizes& edges) {
    return edges.top == 0 && edges.right == 0 && edges.bottom == 0 && edges.left == 0;
}

bool has_authored_size(const Style& style) {
    if (style.width >= 0 || style.height >= 0 || style.min_width >= 0 ||
        style.min_height >= 0 || style.max_width >= 0 || style.max_height >= 0 ||
        style.width_percent >= 0 || style.height_percent >= 0 ||
        style.min_width_percent >= 0 || style.min_height_percent >= 0 ||
        style.max_width_percent >= 0 || style.max_height_percent >= 0) return true;
    for (SizeProperty property : {SizeProperty::Width, SizeProperty::Height,
             SizeProperty::MinWidth, SizeProperty::MinHeight,
             SizeProperty::MaxWidth, SizeProperty::MaxHeight}) {
        SizeKind kind = SizeKind::Pixels;
        std::int64_t value = 0;
        if (style.computed_sizes.get(property, kind, value)) return true;
    }
    return false;
}

bool has_valid_size_sentinels(const Style& style) {
    for (int value : {style.width, style.height, style.min_width, style.min_height,
                      style.max_width, style.max_height, style.width_percent,
                      style.height_percent, style.min_width_percent,
                      style.min_height_percent, style.max_width_percent,
                      style.max_height_percent}) {
        if (value < -1) return false;
    }
    return true;
}

bool has_only_cyclic_width_sizes(const Style& style) {
    constexpr SizeProperty horizontal[] = {
        SizeProperty::Width, SizeProperty::MinWidth, SizeProperty::MaxWidth
    };
    constexpr int Style::* horizontal_percent[] = {
        &Style::width_percent, &Style::min_width_percent, &Style::max_width_percent
    };
    constexpr int Style::* horizontal_scalar[] = {
        &Style::width, &Style::min_width, &Style::max_width
    };
    for (std::size_t i = 0; i < 3; ++i) {
        if (style.*horizontal_percent[i] < -1) return false;
        SizeKind kind = SizeKind::Pixels;
        std::int64_t value = 0;
        const bool has_computed = style.computed_sizes.get(horizontal[i], kind, value);
        if (has_computed) {
            // StyleResolver publishes retained precision with a rounded legacy proxy.
            const auto rounded_percent =
                (value + kDimensionPrecisionScale / 2) / kDimensionPrecisionScale;
            if ((kind != SizeKind::Percent) ||
                style.*horizontal_scalar[i] != -1 ||
                style.*horizontal_percent[i] != rounded_percent) return false;
        } else if (style.*horizontal_scalar[i] != -1) {
            return false;
        }
    }
    if (style.height != -1 || style.min_height != -1 || style.max_height != -1 ||
        style.height_percent != -1 || style.min_height_percent != -1 ||
        style.max_height_percent != -1) return false;
    for (SizeProperty property : {SizeProperty::Height, SizeProperty::MinHeight,
                                  SizeProperty::MaxHeight}) {
        SizeKind kind = SizeKind::Pixels;
        std::int64_t value = 0;
        if (style.computed_sizes.get(property, kind, value)) return false;
    }
    return true;
}

bool same_text_metrics(const Style& a, const Style& b) {
    return !b.text_decoration_underline && !b.text_decoration_line_through &&
        a.font_size == b.font_size && a.font_weight == b.font_weight &&
        a.font_family_hash == b.font_family_hash && a.letter_spacing == b.letter_spacing &&
        a.text_transform == b.text_transform && a.white_space_nowrap == b.white_space_nowrap &&
        a.overflow_wrap_anywhere == b.overflow_wrap_anywhere;
}

bool supported_text_transform(TextTransform transform) {
    return transform == TextTransform::None || transform == TextTransform::Uppercase ||
        transform == TextTransform::Lowercase;
}

unsigned char transform_ascii(unsigned char ch, TextTransform transform) {
    if (transform == TextTransform::Uppercase && ch >= 'a' && ch <= 'z')
        return static_cast<unsigned char>(ch - 'a' + 'A');
    if (transform == TextTransform::Lowercase && ch >= 'A' && ch <= 'Z')
        return static_cast<unsigned char>(ch - 'A' + 'a');
    return ch;
}

bool neutral_inline_style(const Style& style, bool allow_cyclic_width_percent = false) {
    return has_valid_size_sentinels(style) && zero_edges(style.margin) &&
        zero_edges(style.padding) &&
        zero_edges(style.border_width) &&
        (!has_authored_size(style) ||
            (allow_cyclic_width_percent && has_only_cyclic_width_sizes(style))) &&
        style.aspect_ratio_auto && style.aspect_ratio_width == 0 &&
        style.aspect_ratio_height == 0 && style.before_content_kind == GeneratedContentKind::None &&
        style.after_content_kind == GeneratedContentKind::None &&
        style.position_type == PositionType::Static &&
        style.text_indent == 0 && style.list_style_type == ListStyleType::None &&
        !style.white_space_nowrap && style.letter_spacing == 0;
}

bool replaced_or_native(const Node& node) {
    const auto& tag = node.tag_name;
    return tag == "input" || tag == "button" || tag == "select" || tag == "textarea" ||
        tag == "img" || tag == "picture" || tag == "video" || tag == "audio" ||
        tag == "canvas" || tag == "object" || tag == "embed" || tag == "iframe" ||
        tag == "hr";
}

class SourceWidthQuery {
public:
    SourceWidthQuery(const LayoutBox& root, const TextSystem& text_system,
                     IntrinsicPercentageBasis percentage_basis,
                     IntrinsicSourceQueryLimits limits)
        : root_(root), text_system_(text_system), percentage_basis_(percentage_basis),
          limits_(limits) {
        limits_.max_boxes = std::min(limits_.max_boxes, kHardMaxBoxes);
        limits_.max_depth = std::min(limits_.max_depth, kHardMaxDepth);
        limits_.max_source_bytes = std::min(limits_.max_source_bytes, kHardMaxSourceBytes);
        limits_.max_measurements = std::min(limits_.max_measurements, kHardMaxMeasurements);
        limits_.max_work = std::min(limits_.max_work, kHardMaxWork);
        limits_.max_scratch_bytes = std::min(limits_.max_scratch_bytes, kHardMaxScratchBytes);
    }

    IntrinsicSourceWidthResult run() {
        if (!consume_work(0)) return result(status_);
        switch (percentage_basis_.kind) {
        case IntrinsicPercentageBasisKind::CyclicSameAxis:
            if (percentage_basis_.basis_units != -1)
                return result(IntrinsicSourceStatus::InvalidInput);
            break;
        case IntrinsicPercentageBasisKind::DefiniteIndependent:
            if (percentage_basis_.basis_units < 0 ||
                percentage_basis_.basis_units > kDimensionPrecisionMaximum)
                return result(IntrinsicSourceStatus::InvalidInput);
            return result(IntrinsicSourceStatus::Unsupported);
        case IntrinsicPercentageBasisKind::IndefiniteOrUnknown:
            if (percentage_basis_.basis_units != -1)
                return result(IntrinsicSourceStatus::InvalidInput);
            return result(IntrinsicSourceStatus::Unsupported);
        default:
            return result(IntrinsicSourceStatus::InvalidInput);
        }
        if (limits_.max_boxes == 0 || limits_.max_depth == 0 ||
            limits_.max_source_bytes == 0 || limits_.max_measurements == 0 ||
            limits_.max_work == 0 || limits_.max_scratch_bytes == 0)
            return result(IntrinsicSourceStatus::InvalidInput);
        if (root_.node == nullptr || root_.node->type != NodeType::Element ||
            root_.style.display != Display::Block || replaced_or_native(*root_.node) ||
            root_.style.text_decoration_underline || root_.style.text_decoration_line_through ||
            root_.style.white_space_nowrap ||
            !has_valid_size_sentinels(root_.style) ||
            root_.style.letter_spacing != 0 || !supported_text_transform(root_.style.text_transform) ||
            root_.style.position_type != PositionType::Static ||
            root_.style.text_indent != 0 ||
            root_.style.list_style_type != ListStyleType::None || root_.node->tag_name == "li")
            return result(IntrinsicSourceStatus::Unsupported);
        if (root_.style.before_content_kind != GeneratedContentKind::None ||
            root_.style.after_content_kind != GeneratedContentKind::None)
            return result(IntrinsicSourceStatus::Unsupported);
        if (root_.style.padding.left < 0 || root_.style.padding.right < 0 ||
            root_.style.border_width.left < 0 || root_.style.border_width.right < 0)
            return result(IntrinsicSourceStatus::InvalidInput);
        try {
            if (!collect_source()) return result(status_);
            if (needs_measurement_ && !text_system_.ready())
                return result(IntrinsicSourceStatus::MeasurementFailure);
            std::int64_t min_content = 0;
            std::int64_t max_content = 0;
            if (!measure_lines(min_content, max_content)) return result(status_);
            const auto decoration = (static_cast<std::int64_t>(root_.style.padding.left) +
                root_.style.padding.right + root_.style.border_width.left +
                root_.style.border_width.right) * 1000;
            if (decoration < 0 || decoration > kDimensionPrecisionMaximum ||
                min_content > kDimensionPrecisionMaximum - decoration ||
                max_content > kDimensionPrecisionMaximum - decoration)
                return result(IntrinsicSourceStatus::ResourceLimit);
            return {IntrinsicSourceStatus::Complete,
                    min_content + decoration, max_content + decoration,
                    boxes_, source_bytes_, measurements_, work_, peak_scratch_};
        } catch (const std::bad_alloc&) {
            return result(IntrinsicSourceStatus::ResourceLimit);
        }
    }

private:
    enum class SourceMode : std::uint8_t { None, Ascii, Han };

    struct Frame {
        const LayoutBox* box = nullptr;
        std::size_t next_child = 0;
        std::size_t depth = 0;
        bool root = false;
        bool block_flow = false;
        bool entered = false;
    };

    IntrinsicSourceWidthResult result(IntrinsicSourceStatus status) const {
        return {status, -1, -1, boxes_, source_bytes_, measurements_, work_, peak_scratch_};
    }

    bool admit_context(const Node& node) {
        std::size_t depth = 0;
        for (const Node* current = &node; current; current = current->parent, ++depth) {
            if (depth > limits_.max_depth) {
                status_ = IntrinsicSourceStatus::ResourceLimit;
                return false;
            }
            if (!consume_work(1)) return false;
            const auto& tag = current->tag_name;
            if (tag == "pre" || tag == "script" || tag == "style" || tag == "textarea" || tag == "title") {
                status_ = IntrinsicSourceStatus::Unsupported;
                return false;
            }
            for (const auto& attribute : current->attributes) {
                if (!consume_work(1)) return false;
                if (attribute.first == "dir" && !attribute.second.empty() && attribute.second != "ltr") {
                    status_ = IntrinsicSourceStatus::Unsupported;
                    return false;
                }
            }
        }
        return true;
    }

    bool consume_work(std::size_t amount) {
        if (limits_.cancellation && limits_.cancellation->load(std::memory_order_relaxed)) {
            status_ = IntrinsicSourceStatus::Cancelled;
            return false;
        }
        if (work_ > limits_.max_work || amount > limits_.max_work - work_) {
            status_ = IntrinsicSourceStatus::ResourceLimit;
            return false;
        }
        work_ += amount;
        return true;
    }

    bool append_byte(char value) {
        if (!consume_work(1)) return false;
        if (stream_size_ >= limits_.max_source_bytes) {
            status_ = IntrinsicSourceStatus::ResourceLimit;
            return false;
        }
        const std::size_t required = stream_size_ + 1;
        if (required > stream_capacity_) {
            const auto capacity_ceiling = limits_.max_scratch_bytes - stream_capacity_;
            const auto doubled = stream_capacity_ == 0 ? std::size_t{64} :
                stream_capacity_ > std::numeric_limits<std::size_t>::max() / 2
                    ? capacity_ceiling : stream_capacity_ * 2;
            const auto capacity = std::min(doubled, capacity_ceiling);
            if (capacity < required) {
                status_ = IntrinsicSourceStatus::ResourceLimit;
                return false;
            }
            if (!consume_work(stream_size_)) return false;
            peak_scratch_ = std::max(peak_scratch_, stream_capacity_ + capacity);
            std::unique_ptr<char[]> next(new char[capacity]);
            if (stream_size_ != 0) std::memcpy(next.get(), stream_.get(), stream_size_);
            stream_ = std::move(next);
            stream_capacity_ = capacity;
        }
        stream_[stream_size_++] = value;
        return true;
    }

    bool append_text(std::string_view text) {
        if (text.size() > limits_.max_source_bytes -
                std::min(source_bytes_, limits_.max_source_bytes)) {
            status_ = IntrinsicSourceStatus::ResourceLimit;
            return false;
        }
        source_bytes_ += text.size();
        if (!consume_work(text.size())) return false;
        for (std::size_t position = 0; position < text.size();) {
            const std::size_t begin = position;
            std::uint32_t codepoint = 0;
            if (!read_utf8_scalar(text, position, codepoint)) {
                status_ = IntrinsicSourceStatus::InvalidInput;
                return false;
            }
            if (codepoint > 0x7fU) {
                if (!is_cjk_codepoint(codepoint) || source_mode_ == SourceMode::Ascii ||
                    saw_segment_break_ ||
                    root_.style.text_transform != TextTransform::None) {
                    status_ = IntrinsicSourceStatus::Unsupported;
                    return false;
                }
                source_mode_ = SourceMode::Han;
                if (han_line_has_content_ && !append_byte(kSoftBreak)) return false;
                if (!emit_pending_space()) return false;
                needs_measurement_ = true;
                for (std::size_t i = begin; i < position; ++i) {
                    if (!append_byte(text[i])) return false;
                }
                line_has_content_ = true;
                han_line_has_content_ = true;
                continue;
            }
            unsigned char ch = static_cast<unsigned char>(codepoint);
            if (ch < 0x20U && ch != '\t' && ch != '\n' &&
                ch != '\r' && ch != '\f') {
                status_ = IntrinsicSourceStatus::Unsupported;
                return false;
            }
            ch = transform_ascii(ch, root_.style.text_transform);
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f') {
                if (ch == '\n' || ch == '\r' || ch == '\f') {
                    saw_segment_break_ = true;
                    if (source_mode_ == SourceMode::Han) {
                        status_ = IntrinsicSourceStatus::Unsupported;
                        return false;
                    }
                }
                if (!flush_pending_question_break(false)) return false;
                if (!flush_pending_hyphen_break(true)) return false;
                pending_space_ = line_has_content_;
                continue;
            }
            if (source_mode_ == SourceMode::Han) {
                status_ = IntrinsicSourceStatus::Unsupported;
                return false;
            }
            source_mode_ = SourceMode::Ascii;
            if (root_.style.overflow_wrap_anywhere) {
                if (!flush_pending_question_break(false)) return false;
                pending_hyphen_break_ = false;
                if (!emit_pending_space()) return false;
                if (line_has_content_ && !append_byte(kSoftBreak)) return false;
                needs_measurement_ = true;
                if (!append_byte(static_cast<char>(ch))) return false;
                line_has_content_ = true;
                continue;
            }
            const bool ascii_alphanumeric =
                (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                (ch >= '0' && ch <= '9');
            if (!flush_pending_question_break(ascii_alphanumeric)) return false;
            if (!flush_pending_hyphen_break(ch != '/')) return false;
            needs_measurement_ = true;
            if (!emit_pending_space() || !append_byte(static_cast<char>(ch))) return false;
            line_has_content_ = true;
            if (ch == '?') pending_question_break_ = true;
            if (ch == '-') pending_hyphen_break_ = true;
        }
        return true;
    }

    bool emit_pending_space() {
        if (!pending_space_) return true;
        pending_space_ = false;
        if (!line_has_content_) return true;
        return append_byte(' ');
    }

    bool flush_pending_hyphen_break(bool allow_break) {
        if (!pending_hyphen_break_) return true;
        pending_hyphen_break_ = false;
        return !allow_break || append_byte(kSoftBreak);
    }

    bool flush_pending_question_break(bool allow_break) {
        if (!pending_question_break_) return true;
        pending_question_break_ = false;
        return !allow_break || append_byte(kSoftBreak);
    }

    bool collect_source() {
        std::array<Frame, kHardMaxDepth + 1> stack{};
        std::size_t stack_size = 1;
        stack[0] = Frame{&root_, 0, 0, true, true, false};
        while (stack_size != 0) {
            Frame& frame = stack[stack_size - 1];
            if (frame.box == nullptr) {
                status_ = IntrinsicSourceStatus::InvalidInput;
                return false;
            }
            const LayoutBox& box = *frame.box;
            if (!frame.entered) {
                if (frame.depth > limits_.max_depth || boxes_ >= limits_.max_boxes) {
                    status_ = IntrinsicSourceStatus::ResourceLimit;
                    return false;
                }
                ++boxes_;
                if (!consume_work(1)) return false;
                if (box.node == nullptr) {
                    status_ = IntrinsicSourceStatus::Unsupported;
                    return false;
                }
                if (!frame.root && (box.style.display == Display::None ||
                    box.style.position_type == PositionType::Absolute ||
                    box.style.position_type == PositionType::Fixed)) {
                    --stack_size;
                    continue;
                }
                if (!admit_context(*box.node)) return false;
                if (box.node->type == NodeType::Text) {
                    if (!same_text_metrics(root_.style, box.style) ||
                        box.style.white_space_nowrap ||
                        box.style.letter_spacing != 0) {
                        status_ = IntrinsicSourceStatus::Unsupported;
                        return false;
                    }
                    if (!append_text(box.node->text)) return false;
                    --stack_size;
                    continue;
                }
                const auto& tag = box.node->tag_name;
                if (replaced_or_native(*box.node)) {
                    status_ = IntrinsicSourceStatus::Unsupported;
                    return false;
                }
                if (!frame.root) {
                    const bool ordinary_inline = box.style.display == Display::Inline &&
                        same_text_metrics(root_.style, box.style) && neutral_inline_style(box.style);
                    const bool ordinary_block = box.style.display == Display::Block &&
                        same_text_metrics(root_.style, box.style) &&
                        neutral_inline_style(box.style, true);
                    if (!ordinary_inline && !ordinary_block) {
                        status_ = IntrinsicSourceStatus::Unsupported;
                        return false;
                    }
                    if (frame.block_flow) {
                        if (!flush_pending_question_break(false)) return false;
                        if (!flush_pending_hyphen_break(true)) return false;
                        pending_space_ = false;
                        line_has_content_ = false;
                        han_line_has_content_ = false;
                        if (!append_byte(kHardBreak)) return false;
                    }
                }
                if (tag == "br" || tag == "wbr") {
                    if (!box.children.empty()) {
                        status_ = IntrinsicSourceStatus::Unsupported;
                        return false;
                    }
                    if (tag == "br") {
                        if (!flush_pending_question_break(false)) return false;
                        pending_hyphen_break_ = false;
                        pending_space_ = false;
                        line_has_content_ = false;
                        han_line_has_content_ = false;
                        if (!append_byte(kHardBreak)) return false;
                    } else {
                        if (!flush_pending_question_break(false)) return false;
                        if (!flush_pending_hyphen_break(true) || !emit_pending_space() ||
                            !append_byte(kSoftBreak)) return false;
                    }
                    --stack_size;
                    continue;
                }
                frame.entered = true;
            }
            if (frame.next_child < box.children.size()) {
                if (stack_size > limits_.max_depth || stack_size > kHardMaxDepth) {
                    status_ = IntrinsicSourceStatus::ResourceLimit;
                    return false;
                }
                const LayoutBox* child = box.children[frame.next_child++].get();
                const bool block_flow = child != nullptr && child->style.display == Display::Block;
                stack[stack_size++] = Frame{child, 0, frame.depth + 1, false, block_flow, false};
            } else {
                if (frame.block_flow && !frame.root) {
                    if (!flush_pending_question_break(false)) return false;
                    if (!flush_pending_hyphen_break(true)) return false;
                    pending_space_ = false;
                    line_has_content_ = false;
                    han_line_has_content_ = false;
                    if (!append_byte(kHardBreak)) return false;
                }
                --stack_size;
            }
        }
        return flush_pending_hyphen_break(true);
    }

    bool measure_range(std::size_t begin, std::size_t end, std::int64_t& advance) {
        if (begin > end || end > stream_size_) {
            status_ = IntrinsicSourceStatus::InvalidInput;
            return false;
        }
        const auto length = end - begin;
        if (length == 0) {
            advance = 0;
            return true;
        }
        if (!consume_work(length)) return false;
        if (measurements_ >= limits_.max_measurements) {
            status_ = IntrinsicSourceStatus::ResourceLimit;
            return false;
        }
        ++measurements_;
        TextRunMetrics metrics;
        std::size_t measured_work = 0;
        const std::string_view text(stream_.get() + begin, length);
        const auto remaining_work = limits_.max_work - work_;
        const auto measurement_status = measure_core_text(
            text_system_, text, root_.style, metrics, remaining_work, measured_work);
        if (!consume_work(measured_work)) return false;
        if (measurement_status == TextSystemStatus::ResourceLimit) {
            status_ = IntrinsicSourceStatus::ResourceLimit;
            return false;
        }
        if (measurement_status != TextSystemStatus::Ok || metrics.advance < 0) {
            status_ = IntrinsicSourceStatus::MeasurementFailure;
            return false;
        }
        advance = metrics.advance;
        return true;
    }

    bool measure_lines(std::int64_t& min_content, std::int64_t& max_content) {
        if (!consume_work(stream_size_)) return false;
        std::size_t token_begin = 0;
        std::int64_t line_advance = 0;
        std::int64_t pending_space = 0;
        const auto finish_token = [&](std::size_t end) -> bool {
            std::int64_t width = 0;
            if (!measure_range(token_begin, end, width)) return false;
            min_content = std::max(min_content, width);
            if (width > 0) {
                if (line_advance > kDimensionPrecisionMaximum - pending_space ||
                    line_advance + pending_space > kDimensionPrecisionMaximum - width) {
                    status_ = IntrinsicSourceStatus::ResourceLimit;
                    return false;
                }
                line_advance += pending_space + width;
                pending_space = 0;
            }
            return true;
        };

        for (std::size_t i = 0; i < stream_size_; ++i) {
            const char ch = stream_[i];
            if (ch == kSoftBreak) {
                if (!finish_token(i)) return false;
                token_begin = i + 1;
            } else if (ch == kHardBreak) {
                if (!finish_token(i)) return false;
                max_content = std::max(max_content, line_advance);
                line_advance = 0;
                pending_space = 0;
                token_begin = i + 1;
            } else if (ch == ' ') {
                if (!finish_token(i)) return false;
                if (!measure_range(i, i + 1, pending_space)) return false;
                token_begin = i + 1;
            }
        }
        if (!finish_token(stream_size_)) return false;
        max_content = std::max(max_content, line_advance);
        return true;
    }

    const LayoutBox& root_;
    const TextSystem& text_system_;
    IntrinsicPercentageBasis percentage_basis_;
    IntrinsicSourceQueryLimits limits_;
    std::unique_ptr<char[]> stream_;
    std::size_t stream_size_ = 0;
    std::size_t stream_capacity_ = 0;
    std::size_t peak_scratch_ = 0;
    std::size_t boxes_ = 0;
    std::size_t source_bytes_ = 0;
    std::size_t measurements_ = 0;
    std::size_t work_ = 0;
    bool pending_space_ = false;
    bool line_has_content_ = false;
    bool pending_hyphen_break_ = false;
    bool pending_question_break_ = false;
    bool needs_measurement_ = false;
    bool han_line_has_content_ = false;
    bool saw_segment_break_ = false;
    SourceMode source_mode_ = SourceMode::None;
    IntrinsicSourceStatus status_ = IntrinsicSourceStatus::Unsupported;
};

} // namespace

IntrinsicSourceWidthResult query_intrinsic_source_width(
    const LayoutBox& box, const TextSystem& text_system,
    IntrinsicPercentageBasis percentage_basis,
    IntrinsicSourceQueryLimits limits) noexcept {
    SourceWidthQuery query(box, text_system, percentage_basis, limits);
    return query.run();
}

IntrinsicSourceWidthResult IntrinsicSourceQueryLedger::query_width(
    const LayoutBox& box, const TextSystem& text_system, IntrinsicPercentageBasis basis) noexcept {
    const auto remaining = remaining_limits();
    IntrinsicSourceWidthResult result;
    if (cancelled()) result.status = IntrinsicSourceStatus::Cancelled;
    else if (!can_query(remaining)) result.status = IntrinsicSourceStatus::ResourceLimit;
    else result = query_intrinsic_source_width(box, text_system, basis, remaining);
    if (!consume(result)) return {IntrinsicSourceStatus::ResourceLimit};
    return result;
}

} // namespace jellyframe::detail
