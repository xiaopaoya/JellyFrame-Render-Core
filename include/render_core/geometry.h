#pragma once

// Version of the installed value-safe display-command payload contract.
#define JELLYFRAME_RENDER_CORE_CANONICAL_VALUE_API_VERSION 1

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <limits>
#include <string>
#include <vector>

namespace jellyframe {

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

inline int clamp_int64_to_int(std::int64_t value) {
    return static_cast<int>(std::clamp(value,
                                       static_cast<std::int64_t>(std::numeric_limits<int>::min()),
                                       static_cast<std::int64_t>(std::numeric_limits<int>::max())));
}

inline int safe_edge(int origin, int extent) {
    const std::int64_t edge = static_cast<std::int64_t>(origin) + static_cast<std::int64_t>(extent);
    return clamp_int64_to_int(edge);
}

inline int safe_add(int left, int right) {
    return safe_edge(left, right);
}

inline int safe_negate(int value) {
    const std::int64_t negated = -static_cast<std::int64_t>(value);
    return clamp_int64_to_int(negated);
}

inline int safe_span(int start, int end) {
    if (end <= start) {
        return 0;
    }
    return clamp_int64_to_int(std::min<std::int64_t>(
        static_cast<std::int64_t>(std::numeric_limits<int>::max()),
        static_cast<std::int64_t>(end) - start));
}

// CSS values and public LayerNode inputs reach several integer-coordinate
// paths. Keep their conversion defined even for malformed or extreme values.
inline int clamp_float_to_int(float value) {
    if (std::isnan(value)) {
        return 0;
    }
    if (value >= static_cast<float>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    if (value <= static_cast<float>(std::numeric_limits<int>::min())) {
        return std::numeric_limits<int>::min();
    }
    return static_cast<int>(value);
}

inline int floor_float_to_int(float value) {
    return clamp_float_to_int(std::floor(value));
}

inline int ceil_float_to_int(float value) {
    return clamp_float_to_int(std::ceil(value));
}

inline int round_float_to_int(float value) {
    if (std::isnan(value)) {
        return 0;
    }
    if (value >= static_cast<float>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    if (value <= static_cast<float>(std::numeric_limits<int>::min())) {
        return std::numeric_limits<int>::min();
    }
    return static_cast<int>(value >= 0.0F ? value + 0.5F : value - 0.5F);
}

inline bool checked_multiply(std::size_t left, std::size_t right, std::size_t& result) {
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

inline bool checked_add(std::size_t left, std::size_t right, std::size_t& result) {
    if (std::numeric_limits<std::size_t>::max() - left < right) {
        return false;
    }
    result = left + right;
    return true;
}

struct EdgeSizes {
    int top = 0;
    int right = 0;
    int bottom = 0;
    int left = 0;
};

constexpr std::uint32_t kCornerRadiusMarker = 0x80000000U;

struct CornerRadii {
    int top_left = 0;
    int top_right = 0;
    int bottom_right = 0;
    int bottom_left = 0;
};

// Horizontal and vertical corner radii are encoded independently so legacy
// circular commands retain their compact, byte-for-byte value representation.
struct EllipticalCornerRadii {
    CornerRadii horizontal{};
    CornerRadii vertical{};
};

inline int encode_corner_radii(CornerRadii radii) {
    if (radii.top_left == radii.top_right && radii.top_left == radii.bottom_right &&
        radii.top_left == radii.bottom_left && radii.top_left >= 0) {
        return radii.top_left;
    }
    radii.top_left = std::max(0, std::min(127, radii.top_left));
    radii.top_right = std::max(0, std::min(127, radii.top_right));
    radii.bottom_right = std::max(0, std::min(127, radii.bottom_right));
    radii.bottom_left = std::max(0, std::min(127, radii.bottom_left));
    return static_cast<int>(kCornerRadiusMarker |
                            static_cast<std::uint32_t>(radii.top_left) |
                            (static_cast<std::uint32_t>(radii.top_right) << 7U) |
                            (static_cast<std::uint32_t>(radii.bottom_right) << 14U) |
                            (static_cast<std::uint32_t>(radii.bottom_left) << 21U));
}

inline CornerRadii decode_corner_radii(int encoded) {
    const std::uint32_t packed = static_cast<std::uint32_t>(encoded);
    if ((packed & kCornerRadiusMarker) == 0U) {
        const int radius = std::max(0, encoded);
        return CornerRadii{radius, radius, radius, radius};
    }
    return CornerRadii{static_cast<int>(packed & 0x7FU), static_cast<int>((packed >> 7U) & 0x7FU),
                       static_cast<int>((packed >> 14U) & 0x7FU), static_cast<int>((packed >> 21U) & 0x7FU)};
}

inline bool has_corner_radius(int encoded) {
    const CornerRadii radii = decode_corner_radii(encoded);
    return radii.top_left > 0 || radii.top_right > 0 || radii.bottom_right > 0 || radii.bottom_left > 0;
}

inline bool is_valid_corner_radius_encoding(int encoded) {
    return encoded >= 0 || (static_cast<std::uint32_t>(encoded) & 0xf0000000U) == kCornerRadiusMarker;
}

inline int effective_vertical_radius(int horizontal, int vertical) {
    // Nonuniform corner values carry the high-bit marker and are negative
    // as signed ints. Only -1 denotes an omitted legacy vertical axis.
    return vertical == -1 ? horizontal : vertical;
}

inline bool has_corner_radius(const EllipticalCornerRadii& radii) {
    return has_corner_radius(encode_corner_radii(radii.horizontal)) ||
        has_corner_radius(encode_corner_radii(radii.vertical));
}

inline bool has_elliptical_difference(const EllipticalCornerRadii& radii) {
    return std::tie(radii.horizontal.top_left, radii.horizontal.top_right,
                    radii.horizontal.bottom_right, radii.horizontal.bottom_left) !=
        std::tie(radii.vertical.top_left, radii.vertical.top_right,
                 radii.vertical.bottom_right, radii.vertical.bottom_left);
}

inline int expand_corner_radii(int encoded, int amount) {
    CornerRadii radii = decode_corner_radii(encoded);
    const bool uniform = radii.top_left == radii.top_right && radii.top_left == radii.bottom_right &&
        radii.top_left == radii.bottom_left;
    const auto expand = [amount, uniform](int radius) {
        const std::int64_t value = static_cast<std::int64_t>(radius) + amount;
        return static_cast<int>(std::clamp(value, static_cast<std::int64_t>(0),
            static_cast<std::int64_t>(uniform ? std::numeric_limits<int>::max() : 127)));
    };
    radii.top_left = expand(radii.top_left);
    radii.top_right = expand(radii.top_right);
    radii.bottom_right = expand(radii.bottom_right);
    radii.bottom_left = expand(radii.bottom_left);
    return encode_corner_radii(radii);
}

enum class DisplayCommandType : std::uint8_t {
    FillRect,
    StrokeRect,
    LinearGradient,
    ConicGradient,
    RadialGradient,
    BoxShadow,
    Text,
    PreparedText,
    Image,
};

constexpr std::size_t kDisplayCommandTypeCount =
    static_cast<std::size_t>(DisplayCommandType::Image) + 1U;

enum class GradientAxis : std::uint8_t {
    Vertical,
    Horizontal,
    DiagonalDownRight,
    DiagonalDownLeft,
    RadialPosition,
};

enum class TextCommandAlign : std::uint8_t {
    Start,
    Center,
    End,
};

enum class TextTransform : std::uint8_t {
    None,
    Uppercase,
    Lowercase,
    Capitalize,
};

enum class ObjectFit : std::uint8_t {
    Fill,
    Contain,
    Cover,
    None,
    ScaleDown,
};

enum class ImageRendering : std::uint8_t {
    Auto,
    Pixelated,
    CrispEdges,
};

// Background natural dimensions are bounded to 1..8191 per axis when Known.
// Unknown permits legacy handle-only resolution; Invalid omits background paint.
enum class ImageIntrinsicSizeState : std::uint8_t { Unknown, Known, Invalid };

struct ImageIntrinsicSize {
    ImageIntrinsicSizeState state = ImageIntrinsicSizeState::Unknown;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
};

// Relative painting bounds preserve the full image/gradient sampling tile.
struct BackgroundPaintClip {
    std::int16_t dx = 0;
    std::int16_t dy = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
};
static_assert(sizeof(BackgroundPaintClip) == 8, "Background paint clip must remain compact");

struct ObjectPosition {
    int x_percent = 50;
    int y_percent = 50;
};

// Value-frame consumers cannot retain LayerNode objects. Keep the final
// device-space affine matrix alongside a flattened command instead. Values
// use 1/1024 fixed point so the frame codec stays deterministic across tasks.
struct DisplayCommandTransform {
    bool enabled = false;
    std::int32_t xx_1024 = 1024;
    std::int32_t xy_1024 = 0;
    std::int32_t yx_1024 = 0;
    std::int32_t yy_1024 = 1024;
    std::int32_t tx_1024 = 0;
    std::int32_t ty_1024 = 0;
    // A transformed layer may clip its own source surface before affine
    // compositing. This is separate from destination-space frame clips. The
    // uint16_t wire representation is intentional: value-frame producers must
    // reject a frame whose clip table cannot be represented before publishing.
    std::uint16_t source_clip_index = 0xffffU;
};

// Each command owns exactly one active trivial paint payload. The string and
// transform remain common values, so copying never depends on a Style owner.
struct BasicPaint {
    int border_radius = 0;
    int border_radius_y = -1;
    int stroke_width = 1;
    GradientAxis gradient_axis = GradientAxis::Vertical;
    int gradient_stop_percent = 100;
    // Gradient-only: all zero means absent; otherwise both extents are positive.
    BackgroundPaintClip background_paint_clip;
};

struct TextPaint {
    int border_radius = 0;
    int border_radius_y = -1;
    int stroke_width = 1;
    int font_size = 14;
    int font_weight = 400;
    std::uint32_t font_family_hash = 0;
    TextCommandAlign text_align = TextCommandAlign::Start;
    bool text_single_line = true;
    int origin_x = 0;
    int baseline_y = 0;
};

struct ImagePaint {
    std::uint32_t image_handle = 0;
    // All zero means absent; otherwise both extents must be positive.
    BackgroundPaintClip background_paint_clip;
    ObjectFit object_fit = ObjectFit::Fill;
    ObjectPosition object_position;
    ImageRendering image_rendering = ImageRendering::Auto;
    int border_radius = 0;
    int border_radius_y = -1;
};

class DisplayCommand {
    DisplayCommandType type_ = DisplayCommandType::FillRect;

public:
    Rect rect;
    Color color;
    Color color2;
    // Frame-local attribution; codecs intentionally do not serialize it.
    std::uint32_t trace_owner_token = 0;
    std::string text;
    DisplayCommandTransform transform;

private:
    union Paint {
        BasicPaint basic;
        TextPaint text;
        ImagePaint image;
        Paint() {
            std::memset(static_cast<void*>(this), 0, sizeof(*this));
            new (&basic) BasicPaint{};
        }
    } paint_;

    static bool basic_type(DisplayCommandType type) noexcept {
        return type >= DisplayCommandType::FillRect && type <= DisplayCommandType::BoxShadow;
    }
    void require(bool valid) const {
        if (!valid) throw std::logic_error("DisplayCommand paint payload does not match its type");
    }

public:
    DisplayCommand() = default;
    explicit DisplayCommand(DisplayCommandType type) { set_type(type); }
    DisplayCommand(const DisplayCommand&) = default;
    DisplayCommand(DisplayCommand&&) noexcept = default;
    DisplayCommand& operator=(DisplayCommand&&) noexcept = default;
    DisplayCommand& operator=(const DisplayCommand& other) {
        if (this != &other) {
            DisplayCommand copy(other);
            *this = std::move(copy);
        }
        return *this;
    }
    DisplayCommandType type() const noexcept { return type_; }
    // Re-selecting the same type preserves its payload. Changing types resets
    // the newly active value to its documented defaults, including reserved bits.
    void set_type(DisplayCommandType type) {
        if (type == type_) return;
        if (!basic_type(type) && type != DisplayCommandType::Text && type != DisplayCommandType::PreparedText &&
            type != DisplayCommandType::Image)
            throw std::invalid_argument("Unknown DisplayCommand type");
        // Canonicalize unused storage before activating a trivial alternative.
        // Equality and transport still must inspect semantic fields only.
        std::memset(static_cast<void*>(&paint_), 0, sizeof(paint_));
        if (basic_type(type)) new (&paint_.basic) BasicPaint{};
        else if (type == DisplayCommandType::Text || type == DisplayCommandType::PreparedText)
            new (&paint_.text) TextPaint{};
        else if (type == DisplayCommandType::Image) new (&paint_.image) ImagePaint{};
        type_ = type;
    }
    BasicPaint& basic_paint() { require(basic_type(type_)); return paint_.basic; }
    const BasicPaint& basic_paint() const { require(basic_type(type_)); return paint_.basic; }
    TextPaint& text_paint() {
        require(type_ == DisplayCommandType::Text || type_ == DisplayCommandType::PreparedText);
        return paint_.text;
    }
    const TextPaint& text_paint() const {
        require(type_ == DisplayCommandType::Text || type_ == DisplayCommandType::PreparedText);
        return paint_.text;
    }
    ImagePaint& image_paint() { require(type_ == DisplayCommandType::Image); return paint_.image; }
    const ImagePaint& image_paint() const { require(type_ == DisplayCommandType::Image); return paint_.image; }

};

inline void set_prepared_text_origin(DisplayCommand& command, int x, int baseline_y) {
    if (command.type() != DisplayCommandType::PreparedText)
        throw std::logic_error("Prepared text origin requires a PreparedText command");
    command.text_paint().origin_x = x;
    command.text_paint().baseline_y = baseline_y;
}
inline int prepared_text_origin_x(const DisplayCommand& command) {
    if (command.type() != DisplayCommandType::PreparedText)
        throw std::logic_error("Prepared text origin requires a PreparedText command");
    return command.text_paint().origin_x;
}
inline int prepared_text_baseline_y(const DisplayCommand& command) {
    if (command.type() != DisplayCommandType::PreparedText)
        throw std::logic_error("Prepared text origin requires a PreparedText command");
    return command.text_paint().baseline_y;
}

// Translate bounds together with the explicit prepared-text origin.
inline bool translate_display_command(DisplayCommand& command, int dx, int dy) {
    command.rect.x = safe_add(command.rect.x, dx);
    command.rect.y = safe_add(command.rect.y, dy);
    if (command.type() == DisplayCommandType::PreparedText) {
        auto& paint = command.text_paint();
        paint.origin_x = safe_add(paint.origin_x, dx);
        paint.baseline_y = safe_add(paint.baseline_y, dy);
    }
    return true;
}

inline bool display_commands_equal(const DisplayCommand& a, const DisplayCommand& b) {
    const auto rect = [](const Rect& r) { return std::tie(r.x, r.y, r.width, r.height); };
    const auto color = [](const Color& c) { return std::tie(c.r, c.g, c.b, c.a); };
    const auto transform = [](const DisplayCommandTransform& t) {
        return std::tie(t.enabled, t.xx_1024, t.xy_1024, t.yx_1024, t.yy_1024,
                        t.tx_1024, t.ty_1024, t.source_clip_index);
    };
    if (a.type() != b.type() || rect(a.rect) != rect(b.rect) || color(a.color) != color(b.color) ||
        color(a.color2) != color(b.color2) || a.text != b.text ||
        a.trace_owner_token != b.trace_owner_token || transform(a.transform) != transform(b.transform)) return false;
    switch (a.type()) {
    case DisplayCommandType::Text:
    case DisplayCommandType::PreparedText: {
        const auto fields = [](const TextPaint& p) {
            return std::tie(p.border_radius, p.stroke_width, p.font_size, p.font_weight, p.font_family_hash,
                            p.text_align, p.text_single_line, p.origin_x, p.baseline_y);
        };
        return fields(a.text_paint()) == fields(b.text_paint()) &&
            effective_vertical_radius(a.text_paint().border_radius, a.text_paint().border_radius_y) ==
            effective_vertical_radius(b.text_paint().border_radius, b.text_paint().border_radius_y);
    }
    case DisplayCommandType::Image: {
        const auto fields = [](const ImagePaint& p) {
            return std::tie(p.image_handle, p.background_paint_clip.dx, p.background_paint_clip.dy,
                            p.background_paint_clip.width, p.background_paint_clip.height, p.object_fit,
                            p.object_position.x_percent, p.object_position.y_percent, p.image_rendering,
                            p.border_radius);
        };
        return fields(a.image_paint()) == fields(b.image_paint()) &&
            effective_vertical_radius(a.image_paint().border_radius, a.image_paint().border_radius_y) ==
            effective_vertical_radius(b.image_paint().border_radius, b.image_paint().border_radius_y);
    }
    default: {
        const auto fields = [](const BasicPaint& p) {
            return std::tie(p.border_radius, p.stroke_width, p.gradient_axis, p.gradient_stop_percent,
                            p.background_paint_clip.dx, p.background_paint_clip.dy,
                            p.background_paint_clip.width, p.background_paint_clip.height);
        };
        return fields(a.basic_paint()) == fields(b.basic_paint()) &&
            effective_vertical_radius(a.basic_paint().border_radius, a.basic_paint().border_radius_y) ==
            effective_vertical_radius(b.basic_paint().border_radius, b.basic_paint().border_radius_y);
    }
    }
}

using DisplayList = std::vector<DisplayCommand>;

} // namespace jellyframe
