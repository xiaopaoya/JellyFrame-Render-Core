#pragma once

#include "render_core/arena.h"
#include "render_core/animation_timeline.h"
#include "render_core/dom.h"
#include "render_core/style.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace jellyframe {

struct RenderObject;

struct RenderObjectDeleter {
    bool arena_owned = false;
    void operator()(RenderObject* object) const;
};

using RenderObjectPtr = std::unique_ptr<RenderObject, RenderObjectDeleter>;

enum class RenderObjectType {
    View,
    Block,
    Inline,
    Text,
};

enum class ContentsSourceKind { Wrapper, OmittedWhitespace };

struct ContentsSourceStyle {
    std::uint32_t font_family_hash = 0;
    int font_size = 14;
    int font_weight = 400;
    int line_height = -1;
    std::int16_t letter_spacing = 0;
    TextTransform text_transform = TextTransform::None;
    bool white_space_nowrap = false;
    bool overflow_wrap_anywhere = false;
    bool visibility_hidden = false;
    Color color{0, 0, 0, 255};
    Color background_color{0, 0, 0, 0};
    TextShadowStyle text_shadow;
    GeneratedContentKind before_content_kind = GeneratedContentKind::None;
    std::string before_content_text;
    std::string before_counter_name;
    std::string before_counter_suffix;
    Color before_color{0, 0, 0, 255};
    bool before_color_specified = false;
    std::int16_t word_spacing = 0;
    int before_font_weight = 400;
    bool before_font_weight_specified = false;
    bool before_left_specified = false;
    GeneratedContentKind after_content_kind = GeneratedContentKind::None;
    std::string after_content_text;
    std::string after_counter_name;
    std::string after_counter_suffix;
    Color after_color{0, 0, 0, 255};
    bool after_color_specified = false;
    int after_font_weight = 400;
    bool after_font_weight_specified = false;
    bool after_left_specified = false;
    bool text_decoration_underline = false;
    bool text_decoration_line_through = false;
};

struct ContentsSourceRecord {
    ContentsSourceKind kind = ContentsSourceKind::Wrapper;
    const Node* owner = nullptr;
    const Node* node = nullptr;
    const Node* dom_parent = nullptr;
    std::size_t dom_index = 0;
    ContentsSourceStyle style;
    std::string text;
};

struct ContentsSourceTopology {
    bool complete = true;
    std::size_t source_bytes = 0;
    std::vector<ContentsSourceRecord> records;
};

struct RenderObject {
    RenderObjectType type = RenderObjectType::Block;
    const Node* node = nullptr;
    Style style;
    std::vector<RenderObjectPtr> children;
    std::shared_ptr<const ContentsSourceTopology> contents_sources;
};

struct RenderTreeOptions {
    std::size_t max_render_objects = 4096;
    DiagnosticSink* diagnostics = nullptr;
    const std::vector<StyleOverride>* style_overrides = nullptr;
    int viewport_width = 360;
    int viewport_height = 240;
    std::size_t max_contents_source_records = 512;
    std::size_t max_contents_source_bytes = 8192;
};

class RenderTreeBuilder {
public:
    explicit RenderTreeBuilder(const StyleResolver& style_resolver, RenderTreeOptions options = {});

    RenderObjectPtr build(const Node& document) const;
    RenderObjectPtr build(const Node& document, MonotonicArena& arena) const;

private:
    const StyleResolver& style_resolver_;
    RenderTreeOptions options_;

    RenderObjectPtr build_with_arena(const Node& document, MonotonicArena* arena) const;
    RenderObjectPtr make_render_object(MonotonicArena* arena) const;
    RenderObjectType render_type_for(const Node& node, const Style& style) const;
};

std::size_t count_render_objects(const RenderObject& root);

} // namespace jellyframe
