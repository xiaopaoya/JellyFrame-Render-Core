#include "intrinsic_source_query_height.h"

#include "core_text_measure.h"
#include "css_dimension_precision.h"
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

constexpr char kOpportunity = '\x1e';
// Unlike an automatic word-break opportunity, an explicit wbr participates
// in line-box generation even when it is the only inline content.
constexpr char kSoftBreak = '\x1c';
constexpr char kBlockBreak = '\x1d';
constexpr char kForcedBreak = '\x1f';
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

bool has_authored_block_size(const Style& style) {
    if (style.height >= 0 || style.min_height >= 0 || style.max_height >= 0 ||
        style.height_percent >= 0 || style.min_height_percent >= 0 ||
        style.max_height_percent >= 0) return true;
    for (SizeProperty property : {SizeProperty::Height, SizeProperty::MinHeight,
                                  SizeProperty::MaxHeight}) {
        SizeKind kind = SizeKind::Pixels;
        std::int64_t value = 0;
        if (style.computed_sizes.get(property, kind, value)) return true;
    }
    return false;
}

bool supported_transform(TextTransform transform) {
    return transform == TextTransform::None || transform == TextTransform::Uppercase ||
        transform == TextTransform::Lowercase;
}

bool ascii_alphanumeric(unsigned char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9');
}

unsigned char transform_ascii(unsigned char ch, TextTransform transform) {
    if (transform == TextTransform::Uppercase && ch >= 'a' && ch <= 'z')
        return static_cast<unsigned char>(ch - 'a' + 'A');
    if (transform == TextTransform::Lowercase && ch >= 'A' && ch <= 'Z')
        return static_cast<unsigned char>(ch - 'A' + 'a');
    return ch;
}

bool same_text_metrics(const Style& left, const Style& right) {
    return !right.text_decoration_underline && !right.text_decoration_line_through &&
        left.font_size == right.font_size && left.font_weight == right.font_weight &&
        left.font_family_hash == right.font_family_hash && left.letter_spacing == right.letter_spacing &&
        left.line_height == right.line_height && left.text_transform == right.text_transform &&
        left.white_space_nowrap == right.white_space_nowrap &&
        left.overflow_wrap_anywhere == right.overflow_wrap_anywhere;
}

bool replaced_or_native(const Node& node) {
    const auto& tag = node.tag_name;
    return tag == "input" || tag == "button" || tag == "select" || tag == "textarea" ||
        tag == "img" || tag == "picture" || tag == "video" || tag == "audio" ||
        tag == "canvas" || tag == "object" || tag == "embed" || tag == "iframe" ||
        tag == "hr";
}

class SourceHeightQuery {
public:
    SourceHeightQuery(const LayoutBox& root, const TextSystem& text_system,
                      std::int64_t inline_size, IntrinsicSourceQueryLimits limits)
        : root_(root), text_system_(text_system), inline_size_(inline_size), limits_(limits) {
        limits_.max_boxes = std::min(limits_.max_boxes, kHardMaxBoxes);
        limits_.max_depth = std::min(limits_.max_depth, kHardMaxDepth);
        limits_.max_source_bytes = std::min(limits_.max_source_bytes, kHardMaxSourceBytes);
        limits_.max_measurements = std::min(limits_.max_measurements, kHardMaxMeasurements);
        limits_.max_work = std::min(limits_.max_work, kHardMaxWork);
        limits_.max_scratch_bytes = std::min(limits_.max_scratch_bytes, kHardMaxScratchBytes);
    }

    IntrinsicSourceHeightResult run() {
        if (!consume_work(0)) return result(status_);
        if (inline_size_ < 0 || inline_size_ > kDimensionPrecisionMaximum)
            return result(IntrinsicSourceStatus::InvalidInput);
        if (limits_.max_boxes == 0 || limits_.max_depth == 0 || limits_.max_source_bytes == 0 ||
            limits_.max_measurements == 0 || limits_.max_work == 0 || limits_.max_scratch_bytes == 0)
            return result(IntrinsicSourceStatus::InvalidInput);
        if (!root_.node || root_.node->type != NodeType::Element ||
            root_.style.display != Display::Block || replaced_or_native(*root_.node) ||
            root_.style.position_type != PositionType::Static ||
            root_.style.text_decoration_underline || root_.style.text_decoration_line_through ||
            root_.style.white_space_nowrap || root_.style.letter_spacing != 0 ||
            !has_valid_size_sentinels(root_.style) || !supported_transform(root_.style.text_transform) ||
            root_.style.line_height < -1 ||
            !root_.style.aspect_ratio_auto || root_.style.aspect_ratio_width != 0 ||
            root_.style.aspect_ratio_height != 0 ||
            root_.style.text_indent != 0 || root_.style.list_style_type != ListStyleType::None ||
            root_.node->tag_name == "li" || root_.style.before_content_kind != GeneratedContentKind::None ||
            root_.style.after_content_kind != GeneratedContentKind::None ||
            has_authored_block_size(root_.style))
            return result(IntrinsicSourceStatus::Unsupported);
        if (root_.style.padding.top < 0 || root_.style.padding.bottom < 0 ||
            root_.style.border_width.top < 0 || root_.style.border_width.bottom < 0)
            return result(IntrinsicSourceStatus::InvalidInput);
        try {
            if (!collect_source()) return result(status_);
            if (!text_system_.ready()) return result(IntrinsicSourceStatus::MeasurementFailure);
            std::int64_t line_height = root_.style.line_height;
            if (line_height < 0) {
                bool needs_line_height = false;
                for (std::size_t i = 0; i < stream_size_; ++i) {
                    if (!consume_work(1)) return result(status_);
                    const char ch = stream_[i];
                    if (ch == kForcedBreak || ch == kSoftBreak ||
                        (ch != ' ' && ch != kOpportunity && ch != kBlockBreak)) {
                        needs_line_height = true;
                        break;
                    }
                }
                if (needs_line_height) {
                    TextRunMetrics metrics;
                    const auto measurement = measure("", metrics);
                    if (measurement != IntrinsicSourceStatus::Complete)
                        return result(measurement);
                    line_height = metrics.natural_height;
                } else {
                    line_height = 0;
                }
            } else {
                line_height *= kDimensionPrecisionScale;
            }
            if (line_height < 0) return result(IntrinsicSourceStatus::Unsupported);
            std::int64_t content_height = 0;
            if (!measure_wrapped_lines(line_height, content_height)) return result(status_);
            const std::int64_t decoration =
                (static_cast<std::int64_t>(root_.style.padding.top) + root_.style.padding.bottom +
                 root_.style.border_width.top + root_.style.border_width.bottom) * kDimensionPrecisionScale;
            if (decoration < 0 || decoration > kDimensionPrecisionMaximum ||
                content_height > kDimensionPrecisionMaximum - decoration)
                return result(IntrinsicSourceStatus::ResourceLimit);
            return {IntrinsicSourceStatus::Complete, content_height + decoration,
                boxes_, source_bytes_, measurements_, work_, peak_scratch_};
        } catch (const std::bad_alloc&) {
            return result(IntrinsicSourceStatus::ResourceLimit);
        }
    }

private:
    struct Frame {
        const LayoutBox* box = nullptr;
        std::size_t next_child = 0;
        std::size_t depth = 0;
        bool root = false;
        bool entered = false;
    };

    IntrinsicSourceHeightResult result(IntrinsicSourceStatus status) const {
        return {status, -1, boxes_, source_bytes_, measurements_, work_, peak_scratch_};
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

    bool append(char value) {
        if (!consume_work(1)) return false;
        if (stream_size_ >= limits_.max_source_bytes) {
            status_ = IntrinsicSourceStatus::ResourceLimit;
            return false;
        }
        const std::size_t required = stream_size_ + 1;
        if (required > stream_capacity_) {
            const std::size_t remaining = limits_.max_scratch_bytes - stream_capacity_;
            const std::size_t doubled = stream_capacity_ == 0 ? 64 :
                stream_capacity_ > std::numeric_limits<std::size_t>::max() / 2
                    ? remaining : stream_capacity_ * 2;
            const std::size_t capacity = std::min(doubled, remaining);
            if (!consume_work(stream_size_)) return false;
            if (capacity < required) {
                status_ = IntrinsicSourceStatus::ResourceLimit;
                return false;
            }
            peak_scratch_ = std::max(peak_scratch_, stream_capacity_ + capacity);
            std::unique_ptr<char[]> next(new char[capacity]);
            if (stream_size_) std::memcpy(next.get(), stream_.get(), stream_size_);
            stream_ = std::move(next);
            stream_capacity_ = capacity;
        }
        stream_[stream_size_++] = value;
        return true;
    }

    bool append_text(std::string_view text) {
        if (text.size() > limits_.max_source_bytes - std::min(source_bytes_, limits_.max_source_bytes)) {
            status_ = IntrinsicSourceStatus::ResourceLimit;
            return false;
        }
        source_bytes_ += text.size();
        if (!consume_work(text.size())) return false;
        for (std::size_t pos = 0; pos < text.size();) {
            std::uint32_t cp = 0;
            if (!read_utf8_scalar(text, pos, cp)) {
                status_ = IntrinsicSourceStatus::InvalidInput;
                return false;
            }
            if (cp > 0x7fU) {
                status_ = IntrinsicSourceStatus::Unsupported;
                return false;
            }
            const auto ch = static_cast<unsigned char>(cp);
            if (ch < 0x20U && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\f') {
                status_ = IntrinsicSourceStatus::Unsupported;
                return false;
            }
            const bool whitespace = ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
                ch == '\f';
            if (whitespace) {
                std::size_t prior = stream_size_;
                while (prior > 0 && (stream_[prior - 1] == kOpportunity ||
                       stream_[prior - 1] == kSoftBreak)) {
                    if (!consume_work(1)) return false;
                    --prior;
                }
                const bool line_has_content = prior > 0 && stream_[prior - 1] != ' ' &&
                    stream_[prior - 1] != kForcedBreak && stream_[prior - 1] != kBlockBreak;
                if (pending_hyphen_) {
                    if (!append(kOpportunity)) return false;
                    pending_hyphen_ = false;
                }
                pending_question_ = false;
                pending_space_ = line_has_content;
                continue;
            }
            if (pending_hyphen_) {
                if (ch != '/') {
                    if (!append(kOpportunity)) return false;
                }
                pending_hyphen_ = false;
            }
            if (pending_question_) {
                if (ascii_alphanumeric(ch) && !append(kOpportunity)) return false;
                pending_question_ = false;
            }
            if (!emit_space()) return false;
            const auto transformed = transform_ascii(ch, root_.style.text_transform);
            if (!append(static_cast<char>(transformed))) return false;
            pending_hyphen_ = transformed == '-';
            pending_question_ = transformed == '?';
        }
        return true;
    }

    bool emit_space() {
        if (!pending_space_) return true;
        pending_space_ = false;
        return append(' ');
    }

    bool append_forced_break(bool coalesce = true) {
        pending_space_ = false;
        pending_question_ = false;
        if (pending_hyphen_) {
            if (!append(kOpportunity)) return false;
            pending_hyphen_ = false;
        }
        if (coalesce && (stream_size_ == 0 || stream_[stream_size_ - 1] == kBlockBreak)) return true;
        return append(coalesce ? kBlockBreak : kForcedBreak);
    }

    bool collect_source() {
        std::array<Frame, kHardMaxDepth + 1> stack{};
        std::size_t size = 1;
        stack[0] = Frame{&root_, 0, 0, true, false};
        while (size) {
            Frame& frame = stack[size - 1];
            if (!frame.box) { status_ = IntrinsicSourceStatus::InvalidInput; return false; }
            const LayoutBox& box = *frame.box;
            if (!frame.entered) {
                if (frame.depth > limits_.max_depth || boxes_ >= limits_.max_boxes) {
                    status_ = IntrinsicSourceStatus::ResourceLimit;
                    return false;
                }
                ++boxes_;
                if (!consume_work(1)) return false;
                if (!box.node) { status_ = IntrinsicSourceStatus::Unsupported; return false; }
                if (!frame.root && box.style.position_type != PositionType::Static) {
                    status_ = IntrinsicSourceStatus::Unsupported;
                    return false;
                }
                if (!frame.root && box.style.display == Display::None) { --size; continue; }
                if (!admit_context(*box.node)) return false;
                if (box.node->type == NodeType::Text) {
                    if (!same_text_metrics(root_.style, box.style) || box.style.white_space_nowrap ||
                        box.style.letter_spacing != 0) {
                        status_ = IntrinsicSourceStatus::Unsupported;
                        return false;
                    }
                    if (!append_text(box.node->text)) return false;
                    --size;
                    continue;
                }
                if (replaced_or_native(*box.node)) { status_ = IntrinsicSourceStatus::Unsupported; return false; }
                // Break elements have special formatting semantics even when
                // display:block is authored. Generic block boundaries would
                // double-count the preceding line. Keep that profile outside
                // this bounded query until its special semantics are modeled.
                if ((box.node->tag_name == "br" || box.node->tag_name == "wbr") &&
                    box.style.display != Display::Inline) {
                    status_ = IntrinsicSourceStatus::Unsupported;
                    return false;
                }
                if (!frame.root) {
                    if (box.style.position_type != PositionType::Static || has_authored_size(box.style) ||
                        !has_valid_size_sentinels(box.style) || box.style.line_height < -1 ||
                        !box.style.aspect_ratio_auto || box.style.aspect_ratio_width != 0 ||
                        box.style.aspect_ratio_height != 0 || box.style.text_indent != 0 ||
                        box.style.list_style_type != ListStyleType::None || box.node->tag_name == "li" ||
                        !same_text_metrics(root_.style, box.style) ||
                        !zero_edges(box.style.margin) || !zero_edges(box.style.padding) ||
                        !zero_edges(box.style.border_width) ||
                        box.style.before_content_kind != GeneratedContentKind::None ||
                        box.style.after_content_kind != GeneratedContentKind::None ||
                        (box.style.display != Display::Inline && box.style.display != Display::Block)) {
                        status_ = IntrinsicSourceStatus::Unsupported;
                        return false;
                    }
                    if (box.style.display == Display::Block && stream_size_ &&
                        !append_forced_break()) return false;
                }
                if (box.node->tag_name == "br" || box.node->tag_name == "wbr") {
                    if (!box.children.empty()) { status_ = IntrinsicSourceStatus::Unsupported; return false; }
                    if (box.node->tag_name == "br") {
                        if (!append_forced_break(false)) return false;
                    } else {
                        if (pending_hyphen_) {
                            if (!append(kOpportunity)) return false;
                            pending_hyphen_ = false;
                        }
                        pending_question_ = false;
                        if (!append(kSoftBreak)) return false;
                    }
                    --size;
                    continue;
                }
                frame.entered = true;
            }
            if (frame.next_child < box.children.size()) {
                if (size > limits_.max_depth || size > kHardMaxDepth) {
                    status_ = IntrinsicSourceStatus::ResourceLimit;
                    return false;
                }
                const LayoutBox* child = box.children[frame.next_child++].get();
                stack[size++] = Frame{child, 0, frame.depth + 1, false, false};
            } else {
                if (!frame.root && box.style.display == Display::Block && stream_size_ &&
                    !append_forced_break()) return false;
                --size;
            }
        }
        while (stream_size_ && (stream_[stream_size_ - 1] == kBlockBreak ||
               stream_[stream_size_ - 1] == kOpportunity || stream_[stream_size_ - 1] == ' ')) {
            if (!consume_work(1)) return false;
            --stream_size_;
        }
        return true;
    }

    IntrinsicSourceStatus measure(std::string_view text, TextRunMetrics& metrics) {
        if (!consume_work(text.size())) return status_;
        if (measurements_ >= limits_.max_measurements)
            return status_ = IntrinsicSourceStatus::ResourceLimit;
        ++measurements_;
        std::size_t measured_work = 0;
        const auto result = measure_core_text(text_system_, text, root_.style,
            metrics, limits_.max_work - work_, measured_work);
        if (!consume_work(measured_work)) return status_;
        if (result == TextSystemStatus::ResourceLimit) return status_ = IntrinsicSourceStatus::ResourceLimit;
        if (result != TextSystemStatus::Ok) return status_ = IntrinsicSourceStatus::MeasurementFailure;
        return IntrinsicSourceStatus::Complete;
    }

    bool measure_wrapped_lines(std::int64_t line_height, std::int64_t& content_height) {
        if (!consume_work(stream_size_)) return false;
        std::size_t line_count = 0;
        bool line_generated = false;
        std::size_t begin = 0;
        std::int64_t line_width = 0;
        std::int64_t space_width = 0;
        if (stream_size_ && std::find(stream_.get(), stream_.get() + stream_size_, ' ') != stream_.get() + stream_size_) {
            TextRunMetrics space_metrics;
            const auto space_status = measure(" ", space_metrics);
            if (space_status != IntrinsicSourceStatus::Complete) return false;
            space_width = space_metrics.advance;
        }
        bool pending_separator = false;
        const auto append_word = [&](std::size_t end, std::size_t next,
                                     bool has_space_before, std::int64_t& width) -> bool {
            if (end == begin) { begin = next; return true; }
            line_generated = true;
            TextRunMetrics metrics;
            const auto measured = measure(std::string_view(stream_.get() + begin, end - begin), metrics);
            if (measured != IntrinsicSourceStatus::Complete) return false;
            const std::int64_t word_width = metrics.advance;
            if (word_width < 0 || word_width > kDimensionPrecisionMaximum) {
                status_ = IntrinsicSourceStatus::ResourceLimit;
                return false;
            }
            const std::int64_t separator = has_space_before && width > 0 ? space_width : 0;
            if (separator > kDimensionPrecisionMaximum - width ||
                word_width > kDimensionPrecisionMaximum - width - separator) {
                status_ = IntrinsicSourceStatus::ResourceLimit;
                return false;
            }
            if (width > 0 && width + separator + word_width > inline_size_) {
                ++line_count;
                width = 0;
            }
            if (root_.style.overflow_wrap_anywhere && word_width > inline_size_) {
                for (std::size_t j = begin; j < end; ++j) {
                    TextRunMetrics scalar;
                    const auto measured_scalar = measure(std::string_view(stream_.get() + j, 1), scalar);
                    if (measured_scalar != IntrinsicSourceStatus::Complete) return false;
                    const std::int64_t scalar_width = scalar.advance;
                    if (scalar_width < 0 || scalar_width > kDimensionPrecisionMaximum) {
                        status_ = IntrinsicSourceStatus::ResourceLimit;
                        return false;
                    }
                    if (width > 0 && scalar_width > inline_size_ - std::min(width, inline_size_)) {
                        ++line_count;
                        width = 0;
                    }
                    if (scalar_width > kDimensionPrecisionMaximum - width) {
                        status_ = IntrinsicSourceStatus::ResourceLimit;
                        return false;
                    }
                    width += scalar_width;
                }
                begin = next;
                return true;
            }
            width += (has_space_before && width > 0 ? space_width : 0) + word_width;
            begin = next;
            return true;
        };
        for (std::size_t i = 0; i <= stream_size_; ++i) {
            if (!consume_work(1)) return false;
            if (i != stream_size_ && stream_[i] != ' ' && stream_[i] != kOpportunity &&
                stream_[i] != kSoftBreak &&
                stream_[i] != kForcedBreak && stream_[i] != kBlockBreak) continue;
            const char delimiter = i == stream_size_ ? '\0' : stream_[i];
            if (!append_word(i, i + 1, pending_separator, line_width)) return false;
            pending_separator = delimiter == ' ';
            if (delimiter == kSoftBreak) line_generated = true;
            if (delimiter == kForcedBreak || delimiter == kBlockBreak) {
                // br completes a line even when empty. A block boundary only
                // completes existing inline content and must not duplicate br.
                if (delimiter == kForcedBreak || line_generated) ++line_count;
                line_generated = false;
                line_width = 0;
            }
        }
        if (line_generated) ++line_count;
        if (line_height != 0 && line_count > static_cast<std::size_t>(kDimensionPrecisionMaximum / line_height)) {
            status_ = IntrinsicSourceStatus::ResourceLimit;
            return false;
        }
        content_height = static_cast<std::int64_t>(line_count) * line_height;
        return true;
    }

    const LayoutBox& root_;
    const TextSystem& text_system_;
    std::int64_t inline_size_;
    IntrinsicSourceQueryLimits limits_;
    std::unique_ptr<char[]> stream_;
    std::size_t stream_size_ = 0, stream_capacity_ = 0;
    std::size_t peak_scratch_ = 0;
    std::size_t boxes_ = 0, source_bytes_ = 0, measurements_ = 0, work_ = 0;
    bool pending_space_ = false;
    bool pending_hyphen_ = false;
    bool pending_question_ = false;
    IntrinsicSourceStatus status_ = IntrinsicSourceStatus::Unsupported;
};

} // namespace

IntrinsicSourceHeightResult query_intrinsic_source_height(
    const LayoutBox& box, const TextSystem& text_system,
    std::int64_t available_inline_size_units,
    IntrinsicSourceQueryLimits limits) noexcept {
    SourceHeightQuery query(box, text_system, available_inline_size_units, limits);
    return query.run();
}

IntrinsicSourceHeightResult IntrinsicSourceQueryLedger::query_height(
    const LayoutBox& box, const TextSystem& text_system, std::int64_t inline_size) noexcept {
    const auto remaining = remaining_limits();
    IntrinsicSourceHeightResult result;
    if (cancelled()) result.status = IntrinsicSourceStatus::Cancelled;
    else if (!can_query(remaining)) result.status = IntrinsicSourceStatus::ResourceLimit;
    else result = query_intrinsic_source_height(box, text_system, inline_size, remaining);
    if (!consume(result)) return {IntrinsicSourceStatus::ResourceLimit};
    return result;
}

} // namespace jellyframe::detail
