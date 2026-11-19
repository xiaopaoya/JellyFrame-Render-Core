#include "render_core/render_tree.h"

#include "render_core/text_normalization.h"
#include "css_aspect_ratio.h"
#include "background_style_access.h"
#include "scalar_cascade_cache.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <limits>
#include <utility>

namespace jellyframe {
namespace {

void inherit_text_style(Style& style, const Style& parent_style) {
    if (!style.visibility_specified) {
        style.visibility_hidden = parent_style.visibility_hidden;
    }
    if (!style.color_specified) {
        style.color = parent_style.color;
    }
    if (!style.font_size_specified) {
        style.font_size = parent_style.font_size;
    }
    if (!style.font_weight_specified) {
        style.font_weight = parent_style.font_weight;
    }
    if (!style.font_family_specified) {
        style.font_family_hash = parent_style.font_family_hash;
    }
    if (!style.text_align_specified) {
        style.text_align = parent_style.text_align;
    }
    if (!style.line_height_specified) {
        style.line_height = parent_style.line_height;
    }
    if (!style.text_indent_specified) {
        style.text_indent = parent_style.text_indent;
    }
    if (!style.letter_spacing_specified) {
        style.letter_spacing = parent_style.letter_spacing;
    }
    if (!style.word_spacing_specified) {
        style.word_spacing = parent_style.word_spacing;
    }
    if (!style.text_transform_specified) {
        style.text_transform = parent_style.text_transform;
    }
    if (!style.text_decoration_specified) {
        style.text_decoration_underline = parent_style.text_decoration_underline;
        style.text_decoration_line_through = parent_style.text_decoration_line_through;
    }
    if (!style.text_shadow_specified) {
        style.text_shadow = parent_style.text_shadow;
    }
    if (!style.white_space_specified) {
        style.white_space_nowrap = parent_style.white_space_nowrap;
    }
    // text-overflow is not generally inherited. The render tree represents a
    // direct text run as its own node, so carry the owning inline container's
    // ellipsis intent only to that anonymous-equivalent run.
    if (!style.text_overflow_specified) {
        style.text_overflow_ellipsis = parent_style.text_overflow_ellipsis;
    }
    if (!style.overflow_wrap_specified) {
        style.overflow_wrap_anywhere = parent_style.overflow_wrap_anywhere;
    }
    if (!style.list_style_type_specified) {
        style.list_style_type = parent_style.list_style_type;
    }
}

void inherit_element_style(Style& style, const Style& parent_style) {
    inherit_text_style(style, parent_style);
    if ((style.table_properties & kTableCaptionSideSpecified) == 0)
        style.table_properties = (style.table_properties & ~4U) | (parent_style.table_properties & 4U);
    if ((style.table_properties & kTableBorderCollapseSpecified) == 0)
        style.table_properties = (style.table_properties & ~2U) | (parent_style.table_properties & 2U);
    if ((style.table_properties & kTableBorderSpacingSpecified) == 0) {
        constexpr std::uint32_t spacing_mask = (0xFFU << 12U) | (0xFFU << 20U);
        style.table_properties = (style.table_properties & ~spacing_mask) |
            (parent_style.table_properties & spacing_mask);
    }
    detail::finalize_inherited_aspect_ratio(style, &parent_style);
    if (!style.text_overflow_specified) {
        style.text_overflow_ellipsis = false;
    }
}

bool creates_render_object(const Node& node, const Style& style) {
    if (style.display == Display::None) {
        return false;
    }
    if (node.type == NodeType::Text) {
        return !node.text.empty() && !is_collapsible_whitespace_text(node);
    }
    return true;
}

bool is_document_root_element(const Node& node) {
    return node.type == NodeType::Element && node.tag_name == "html" && node.parent != nullptr &&
        node.parent->tag_name == "document";
}

bool is_replaced_control(const Node& node) {
    if (node.type != NodeType::Element) {
        return false;
    }
    return node.tag_name == "input" || node.tag_name == "select" || node.tag_name == "textarea" ||
        node.tag_name == "img" || node.tag_name == "picture" || node.tag_name == "video" ||
        node.tag_name == "audio" || node.tag_name == "iframe";
}

bool display_contents_computes_to_none(const Node& node) {
    if (node.type != NodeType::Element) {
        return false;
    }
    return node.tag_name == "input" || node.tag_name == "select" || node.tag_name == "textarea" ||
        node.tag_name == "img" || node.tag_name == "video" || node.tag_name == "audio" ||
        node.tag_name == "iframe" || node.tag_name == "canvas" || node.tag_name == "object" ||
        node.tag_name == "embed";
}

ContentsSourceStyle contents_source_style(const Style& style) {
    ContentsSourceStyle result;
    result.font_family_hash = style.font_family_hash;
    result.font_size = style.font_size;
    result.font_weight = style.font_weight;
    result.line_height = style.line_height;
    result.letter_spacing = style.letter_spacing;
    result.word_spacing = style.word_spacing;
    result.text_transform = style.text_transform;
    result.white_space_nowrap = style.white_space_nowrap;
    result.overflow_wrap_anywhere = style.overflow_wrap_anywhere;
    result.visibility_hidden = style.visibility_hidden;
    result.color = style.color;
    result.background_color = style.background_color;
    result.text_shadow = style.text_shadow;
    result.before_content_kind = style.before_content_kind;
    result.before_content_text = style.before_content_text;
    result.before_counter_name = style.before_counter_name;
    result.before_counter_suffix = style.before_counter_suffix;
    result.before_color = style.before_color;
    result.before_color_specified = style.before_color_specified;
    result.before_font_weight = style.before_font_weight;
    result.before_font_weight_specified = style.before_font_weight_specified;
    result.before_left_specified = style.before_left_specified;
    result.after_content_kind = style.after_content_kind;
    result.after_content_text = style.after_content_text;
    result.after_counter_name = style.after_counter_name;
    result.after_counter_suffix = style.after_counter_suffix;
    result.after_color = style.after_color;
    result.after_color_specified = style.after_color_specified;
    result.after_font_weight = style.after_font_weight;
    result.after_font_weight_specified = style.after_font_weight_specified;
    result.after_left_specified = style.after_left_specified;
    result.text_decoration_underline = style.text_decoration_underline;
    result.text_decoration_line_through = style.text_decoration_line_through;
    return result;
}

bool is_first_summary_child(const Node& parent, const Node& child) {
    for (const auto& sibling : parent.children) {
        if (sibling->type == NodeType::Element && sibling->tag_name == "summary") {
            return sibling.get() == &child;
        }
    }
    return false;
}

bool should_descend_into_child(const Node& parent, const Node& child) {
    if (parent.type == NodeType::Element && parent.tag_name == "details" &&
        parent.attributes.find("open") == parent.attributes.end()) {
        return child.type == NodeType::Element && child.tag_name == "summary" &&
            is_first_summary_child(parent, child);
    }
    return true;
}

void apply_style_override(Style& style, const Node& node, const std::vector<StyleOverride>* overrides) {
    if (overrides == nullptr || overrides->empty()) {
        return;
    }
    for (const StyleOverride& override : *overrides) {
        if (override.node != &node) {
            continue;
        }
        if (override.has_opacity) {
            style.opacity = override.opacity;
        }
        if (override.has_color) {
            style.color = override.color;
            style.color_specified = true;
        }
        if (override.has_background_color) {
            style.background_color = override.background_color;
        }
        if (override.has_transform) {
            style.transform = override.transform;
        }
        return;
    }
}

} // namespace

void RenderObjectDeleter::operator()(RenderObject* object) const {
    if (!arena_owned) {
        delete object;
    }
}

struct RenderTreeStyleReuse::State {
    static constexpr std::size_t max_nodes = 128;
    static constexpr std::size_t max_bytes = 16 * 1024;
    static constexpr std::size_t no_parent = std::numeric_limits<std::size_t>::max();
    struct Entry {
        const Node* node = nullptr;
        std::size_t parent = no_parent;
        std::size_t children = 0;
        std::shared_ptr<const std::string> inputs;
        const RenderObject* object = nullptr;
        std::uint64_t local_generation = 0;
        bool affected = true;
    };
    std::vector<Entry> entries;
    const Node* document = nullptr;
    const RenderObject* pending_tree = nullptr;
    const Node* root_element = nullptr;
    const StyleResolver* resolver = nullptr;
    std::uint64_t context_generation = 0;
    std::uint64_t dom_generation = 0;
    std::uint64_t structure_generation = 0;
    int width = 0;
    int height = 0;
    std::size_t object_limit = 0;
    std::size_t contents_record_limit = 0;
    std::size_t contents_byte_limit = 0;
    std::size_t bytes = 0;
    bool complete = false;
    bool tracked = false;
    mutable std::size_t hits = 0;

    static bool safe_node(const Node& node) {
        const auto& tag = node.tag_name;
        return node.attributes.size() <= 64 && node.attribute("style").empty() &&
            node.attribute("dir").empty() && tag != "bdi" && tag != "bdo" &&
            tag != "ol" && tag != "ul" && tag != "li" && tag != "menu" &&
            tag != "input" && tag != "select" && tag != "textarea" &&
            tag != "option" && tag != "optgroup" && tag != "progress" && tag != "meter";
    }

    static bool same_inputs(const std::string& saved, const Node& node) {
        std::size_t position = 0;
        const auto number = [&](std::size_t value) {
            if (saved.size() - position < sizeof(value) ||
                std::memcmp(saved.data() + position, &value, sizeof(value)) != 0) return false;
            position += sizeof(value);
            return true;
        };
        const auto text = [&](std::string_view value) {
            if (!number(value.size()) || saved.size() - position < value.size() ||
                saved.compare(position, value.size(), value.data(), value.size()) != 0) return false;
            position += value.size();
            return true;
        };
        if (!number(static_cast<std::size_t>(node.type)) || !text(node.tag_name) ||
            !number(node.attributes.size())) return false;
        for (const auto& attribute : node.attributes)
            if (!text(attribute.first) || !text(attribute.second)) return false;
        return position == saved.size();
    }

    static std::shared_ptr<const std::string> capture_inputs(const Node& node) {
        auto inputs = std::make_shared<std::string>();
        const auto append_number = [&](std::size_t value) {
            if (inputs->size() > max_bytes - sizeof(value))
                throw std::length_error("style reuse input limit");
            inputs->append(reinterpret_cast<const char*>(&value), sizeof(value));
        };
        const auto append_text = [&](std::string_view value) {
            if (value.size() > max_bytes ||
                inputs->size() > max_bytes - sizeof(std::size_t) ||
                value.size() > max_bytes - inputs->size() - sizeof(std::size_t))
                throw std::length_error("style reuse input limit");
            append_number(value.size());
            inputs->append(value.data(), value.size());
        };
        append_number(static_cast<std::size_t>(node.type));
        append_text(node.tag_name);
        append_number(node.attributes.size());
        for (const auto& attribute : node.attributes) {
            append_text(attribute.first);
            append_text(attribute.second);
        }
        return inputs;
    }

    void invalidate_root_dependents() {
        for (const auto& entry : entries) {
            if (entry.node == root_element && entry.affected) {
                for (auto& dependent : entries) {
                    dependent.affected = true;
                    dependent.object = nullptr;
                }
                return;
            }
        }
    }

    bool matches_document(const Node& current_document) const {
        struct Pending { const Node* node; std::size_t parent; };
        std::vector<Pending> pending{{&current_document, no_parent}};
        std::size_t ordinal = 0;
        while (!pending.empty()) {
            const Pending current = pending.back();
            pending.pop_back();
            if (ordinal >= entries.size()) return false;
            const Entry& entry = entries[ordinal];
            if (entry.node != current.node || entry.parent != current.parent ||
                entry.children != current.node->children.size() || !entry.inputs ||
                !same_inputs(*entry.inputs, *current.node) ||
                (current.parent != no_parent && current.node->parent != entries[current.parent].node))
                return false;
            ++ordinal;
            for (auto it = current.node->children.rbegin(); it != current.node->children.rend(); ++it)
                pending.push_back({it->get(), ordinal - 1});
        }
        return ordinal == entries.size();
    }
};

RenderTreeStyleReuse::RenderTreeStyleReuse() = default;
RenderTreeStyleReuse::~RenderTreeStyleReuse() = default;
RenderTreeStyleReuse::RenderTreeStyleReuse(RenderTreeStyleReuse&&) noexcept = default;
RenderTreeStyleReuse& RenderTreeStyleReuse::operator=(RenderTreeStyleReuse&&) noexcept = default;
void RenderTreeStyleReuse::reset() { state_.reset(); }
std::size_t RenderTreeStyleReuse::reused_styles() const { return state_ ? state_->hits : 0; }
std::size_t RenderTreeStyleReuse::payload_bytes() const { return state_ ? state_->bytes : 0; }

bool RenderTreeStyleReuse::prepare(const Node& document, const StyleResolver& resolver,
                                   const RenderTreeOptions& options, const RenderTreeStyleReuse* previous) {
    return prepare_impl(document, resolver, options, previous, false);
}

bool RenderTreeStyleReuse::prepare_tracked(const Node& document, const StyleResolver& resolver,
                                           const RenderTreeOptions& options, const RenderTreeStyleReuse* previous) {
    return prepare_impl(document, resolver, options, previous, true);
}

bool RenderTreeStyleReuse::prepare_impl(const Node& document, const StyleResolver& resolver,
                                        const RenderTreeOptions& options, const RenderTreeStyleReuse* previous,
                                        bool tracked) {
    if (previous == this) {
        reset();
        return false;
    }
    reset();
    if (document.parent || options.diagnostics ||
        (options.style_overrides && !options.style_overrides->empty()) ||
        !resolver.permits_scoped_style_reuse()) return false;
    try {
        const State* old = previous ? previous->state_.get() : nullptr;
        const Node* root_element = &document;
        if (document.tag_name == "document") {
            root_element = nullptr;
            for (const auto& child : document.children) {
                if (child->type == NodeType::Element) {
                    root_element = child.get();
                    break;
                }
            }
        }
        const auto context_generation = resolver.scoped_style_context_generation();
        std::vector<const Node*> current_nodes;
        bool tracked_structure_matches = false;
        if (tracked && old && old->tracked && old->complete && old->document == &document &&
            old->resolver == &resolver && old->root_element == root_element &&
            old->context_generation == context_generation &&
            old->structure_generation == document.structural_mutation_generation() &&
            old->width == options.viewport_width && old->height == options.viewport_height &&
            old->object_limit == options.max_render_objects &&
            old->contents_record_limit == options.max_contents_source_records &&
            old->contents_byte_limit == options.max_contents_source_bytes) {
            struct Pending { const Node* node; std::size_t parent; };
            std::vector<Pending> pending{{&document, State::no_parent}};
            current_nodes.reserve(old->entries.size());
            tracked_structure_matches = true;
            while (!pending.empty()) {
                const Pending current = pending.back();
                pending.pop_back();
                const std::size_t ordinal = current_nodes.size();
                if (ordinal >= old->entries.size() ||
                    (current.parent != State::no_parent &&
                     current.node->parent != current_nodes[current.parent]) ||
                    current.node->children.size() != old->entries[ordinal].children ||
                    current.node != old->entries[ordinal].node ||
                    old->entries[ordinal].parent != current.parent) {
                    tracked_structure_matches = false;
                    break;
                }
                current_nodes.push_back(current.node);
                for (auto it = current.node->children.rbegin(); it != current.node->children.rend(); ++it)
                    pending.push_back({it->get(), ordinal});
            }
            tracked_structure_matches = tracked_structure_matches &&
                current_nodes.size() == old->entries.size();
        }
        if (tracked_structure_matches) {
            auto next = std::make_unique<State>(*old);
            next->complete = false;
            next->hits = 0;
            next->dom_generation = document.mutation_generation;
            for (std::size_t i = 0; i < next->entries.size(); ++i) {
                auto& entry = next->entries[i];
                const Node& node = *current_nodes[i];
                const auto generation = node.local_style_mutation_generation();
                const bool changed = generation != entry.local_generation || !entry.inputs ||
                    !State::same_inputs(*entry.inputs, node);
                if (changed && !State::safe_node(node)) return false;
                entry.local_generation = generation;
                if (changed) entry.inputs = State::capture_inputs(node);
                entry.affected = changed ||
                    (entry.parent != State::no_parent && next->entries[entry.parent].affected);
                if (entry.affected) entry.object = nullptr;
            }
            next->invalidate_root_dependents();
            next->bytes = sizeof(State) + next->entries.capacity() * sizeof(State::Entry);
            for (const auto& entry : next->entries) {
                if (entry.inputs) next->bytes += sizeof(std::string) + entry.inputs->capacity() + 1;
            }
            if (next->bytes > State::max_bytes) return false;
            state_ = std::move(next);
            return true;
        }

        auto next = std::make_unique<State>();
        next->document = &document;
        next->resolver = &resolver;
        next->root_element = root_element;
        next->context_generation = context_generation;
        next->dom_generation = document.mutation_generation;
        next->structure_generation = document.structural_mutation_generation();
        next->width = options.viewport_width;
        next->height = options.viewport_height;
        next->object_limit = options.max_render_objects;
        next->contents_record_limit = options.max_contents_source_records;
        next->contents_byte_limit = options.max_contents_source_bytes;
        next->tracked = tracked;

        struct Pending { const Node* node; std::size_t parent; };
        std::vector<Pending> pending{{&document, State::no_parent}};
        while (!pending.empty()) {
            const Pending current = pending.back();
            pending.pop_back();
            const Node& node = *current.node;
            const std::size_t ordinal = next->entries.size();
            if (current.parent != State::no_parent &&
                node.parent != next->entries[current.parent].node) return false;
            if (ordinal >= State::max_nodes || node.children.size() > State::max_nodes - ordinal - 1 ||
                !State::safe_node(node)) return false;

            State::Entry entry;
            entry.node = &node;
            entry.parent = current.parent;
            entry.children = node.children.size();
            entry.local_generation = node.local_style_mutation_generation();
            entry.inputs = State::capture_inputs(node);
            next->entries.push_back(std::move(entry));
            for (auto it = node.children.rbegin(); it != node.children.rend(); ++it)
                pending.push_back({it->get(), ordinal});
        }
        next->bytes = sizeof(State) + next->entries.capacity() * sizeof(State::Entry);
        for (const auto& entry : next->entries) {
            if (entry.inputs) next->bytes += sizeof(std::string) + entry.inputs->capacity() + 1;
        }
        if (next->bytes > State::max_bytes) return false;

        const bool compatible = !tracked && old && !old->tracked && old->complete &&
            old->document == &document && old->resolver == &resolver && old->root_element == root_element &&
            old->context_generation == context_generation && old->width == options.viewport_width &&
            old->height == options.viewport_height && old->object_limit == options.max_render_objects &&
            old->contents_record_limit == options.max_contents_source_records &&
            old->contents_byte_limit == options.max_contents_source_bytes &&
            old->entries.size() == next->entries.size();
        if (compatible) {
            for (std::size_t i = 0; i < next->entries.size(); ++i) {
                const auto& before = old->entries[i];
                const auto& after = next->entries[i];
                if (before.node != after.node || before.parent != after.parent ||
                    before.children != after.children) {
                    return false;
                }
            }
            for (std::size_t i = 0; i < next->entries.size(); ++i) {
                auto& entry = next->entries[i];
                const auto& before = old->entries[i];
                entry.affected = !before.inputs || !entry.inputs ||
                    before.inputs->size() != entry.inputs->size() || *before.inputs != *entry.inputs ||
                    (entry.parent != State::no_parent && next->entries[entry.parent].affected);
                if (!entry.affected) entry.object = before.object;
            }
            next->invalidate_root_dependents();
        }
        state_ = std::move(next);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
}

const Style* RenderTreeStyleReuse::find(const Node& node) const {
    if (!state_ || state_->complete) return nullptr;
    for (const auto& entry : state_->entries) {
        if (entry.node == &node) {
            if (!entry.object || entry.affected) return nullptr;
            ++state_->hits;
            return &entry.object->style;
        }
    }
    return nullptr;
}

bool RenderTreeStyleReuse::complete(const RenderObject& tree) {
    if (!state_) return false;
    State& state = *state_;
    if (state.pending_tree != &tree || state.document != tree.node ||
        state.document->mutation_generation != state.dom_generation ||
        state.resolver->scoped_style_context_generation() != state.context_generation) {
        reset();
        return false;
    }
    try {
        if (!state.matches_document(*state.document)) {
            reset();
            return false;
        }
        for (auto& entry : state.entries) entry.object = nullptr;
        std::vector<const RenderObject*> pending{&tree};
        std::size_t visited = 0;
        while (!pending.empty()) {
            const RenderObject* object = pending.back();
            pending.pop_back();
            const Style& style = object->style;
            if (++visited > State::max_nodes || !object->node ||
                !detail::ScalarCascadeCache::scalar(style) ||
                (style.display != Display::Block && style.display != Display::Inline &&
                 style.display != Display::InlineBlock && style.display != Display::None) ||
                style.before_content_kind != GeneratedContentKind::None ||
                style.after_content_kind != GeneratedContentKind::None) {
                reset();
                return false;
            }
            for (auto& entry : state.entries) {
                if (entry.node == object->node) {
                    if (entry.object) {
                        reset();
                        return false;
                    }
                    entry.object = object;
                    break;
                }
            }
            for (const auto& child : object->children) pending.push_back(child.get());
        }
        state.complete = true;
        state.pending_tree = nullptr;
        state.hits = 0;
        return true;
    } catch (const std::bad_alloc&) {
        reset();
        return false;
    }
}

RenderTreeBuilder::RenderTreeBuilder(const StyleResolver& style_resolver, RenderTreeOptions options)
    : style_resolver_(style_resolver),
      options_(options) {}

RenderObjectPtr RenderTreeBuilder::build(const Node& document) const {
    return build_with_arena(document, nullptr);
}

RenderObjectPtr RenderTreeBuilder::build(const Node& document, MonotonicArena& arena) const {
    return build_with_arena(document, &arena);
}

RenderObjectPtr RenderTreeBuilder::build_with_arena(const Node& document, MonotonicArena* arena) const {
    const RenderTreeStyleReuse* reuse = options_.style_reuse;
    if (reuse != nullptr) {
        const auto* state = reuse->state_.get();
        if (state == nullptr || state->complete || state->document != &document ||
            state->resolver != &style_resolver_ || state->dom_generation != document.mutation_generation ||
            state->context_generation != style_resolver_.scoped_style_context_generation() ||
            state->width != options_.viewport_width || state->height != options_.viewport_height ||
            state->object_limit != options_.max_render_objects ||
            state->contents_record_limit != options_.max_contents_source_records ||
            state->contents_byte_limit != options_.max_contents_source_bytes ||
            options_.diagnostics != nullptr ||
            (options_.style_overrides != nullptr && !options_.style_overrides->empty())) reuse = nullptr;
        else {
            try {
                if (!state->matches_document(document)) reuse = nullptr;
            } catch (const std::bad_alloc&) {
                reuse = nullptr;
            } catch (const std::length_error&) {
                reuse = nullptr;
            }
        }
    }
    struct PendingNode {
        const Node* node = nullptr;
        RenderObject* parent = nullptr;
        const Style* parent_style = nullptr;
        bool under_contents = false;
        std::size_t dom_index = 0;
    };

    StyleResolveContext style_context;
    style_context.viewport_width = options_.viewport_width;
    style_context.viewport_height = options_.viewport_height;
    auto view = make_render_object(arena);
    RenderObject* view_raw = view.get();
    view->type = RenderObjectType::View;
    view->node = &document;
    const Style* reusable_view_style = reuse != nullptr ? reuse->find(document) : nullptr;
    if (reusable_view_style != nullptr) {
        view->style = *reusable_view_style;
    } else {
        view->style = style_resolver_.resolve(document, style_context);
        detail::finalize_inherited_aspect_ratio(view->style, nullptr);
        apply_style_override(view->style, document, options_.style_overrides);
    }

    std::size_t render_object_count = 1;
    bool budget_reported = false;
    const std::size_t max_render_objects = std::max<std::size_t>(1, options_.max_render_objects);

    std::vector<PendingNode> pending;
    pending.reserve(document.children.size());
    for (std::size_t i = document.children.size(); i > 0; --i) {
        pending.push_back(PendingNode{document.children[i - 1].get(), view_raw,
            &view_raw->style, false, i - 1});
    }
    std::deque<Style> contents_styles;
    std::shared_ptr<ContentsSourceTopology> contents_sources;
    bool has_contents = false;
    const auto record_source = [&](ContentsSourceRecord record) {
        if (!contents_sources) contents_sources = std::make_shared<ContentsSourceTopology>();
        const auto max_records = std::max<std::size_t>(1, options_.max_contents_source_records);
        const auto record_bytes = record.text.size() + record.style.before_content_text.size() +
            record.style.after_content_text.size() + record.style.before_counter_name.size() +
            record.style.before_counter_suffix.size() + record.style.after_counter_name.size() +
            record.style.after_counter_suffix.size();
        if (!contents_sources->complete) return;
        if (contents_sources->records.size() >= max_records ||
            record_bytes > options_.max_contents_source_bytes -
                std::min(options_.max_contents_source_bytes, contents_sources->source_bytes)) {
            contents_sources->complete = false;
            return;
        }
        contents_sources->source_bytes += record_bytes;
        contents_sources->records.push_back(std::move(record));
    };
    while (!pending.empty()) {
        const PendingNode pending_node = pending.back();
        pending.pop_back();
        const Node& node = *pending_node.node;
        RenderObject& parent = *pending_node.parent;

        if (render_object_count >= max_render_objects) {
            if (!budget_reported) {
                report_diagnostic(options_.diagnostics,
                                  DiagnosticStage::RenderTree,
                                  DiagnosticSeverity::Warning,
                                  "render-object-limit",
                                  "Render object budget was reached; remaining visible nodes were skipped",
                                  "Increase max_render_objects or simplify the view hierarchy.");
                budget_reported = true;
            }
            continue;
        }

        // These sources are discarded independently of their computed style.
        // Keep overrides on the established path and preserve budget diagnostics.
        if (node.type == NodeType::Text &&
            (options_.style_overrides == nullptr || options_.style_overrides->empty()) &&
            (node.text.empty() || (!pending_node.under_contents && is_collapsible_whitespace_text(node)))) {
            continue;
        }

        const Style* reusable_style = reuse != nullptr ? reuse->find(node) : nullptr;
        Style style = reusable_style != nullptr ? *reusable_style : style_resolver_.resolve(node, style_context);
        if (reusable_style == nullptr) {
            if (node.type == NodeType::Text) {
                inherit_text_style(style, *pending_node.parent_style);
            } else {
                inherit_element_style(style, *pending_node.parent_style);
            }
            apply_style_override(style, node, options_.style_overrides);
        }

        // The document element retains its root box; replaced elements remain
        // leaves even when their authored display value is contents.
        if (style.display == Display::Contents && is_document_root_element(node)) {
            style.display = Display::Block;
        } else if (style.display == Display::Contents && display_contents_computes_to_none(node)) {
            style.display = Display::None;
        }

        if (style.display == Display::Contents) {
            has_contents = true;
            record_source(ContentsSourceRecord{ContentsSourceKind::Wrapper, parent.node, &node,
                node.parent, pending_node.dom_index, contents_source_style(style), {}});
            contents_styles.push_back(style);
            const Style* inherited_style = &contents_styles.back();
            for (std::size_t i = node.children.size(); i > 0; --i) {
                if (should_descend_into_child(node, *node.children[i - 1])) {
                    pending.push_back(PendingNode{node.children[i - 1].get(), &parent,
                        inherited_style, true, i - 1});
                }
            }
            continue;
        }

        if (pending_node.under_contents && node.type == NodeType::Text &&
            !node.text.empty() && is_collapsible_whitespace_text(node)) {
            record_source(ContentsSourceRecord{ContentsSourceKind::OmittedWhitespace, parent.node, &node,
                node.parent, pending_node.dom_index, contents_source_style(style), node.text});
            continue;
        }

        if (!creates_render_object(node, style)) {
            continue;
        }

        auto object = make_render_object(arena);
        RenderObject* object_raw = object.get();
        ++render_object_count;
        object->type = render_type_for(node, style);
        object->node = &node;
        object->style = style;
        parent.children.push_back(std::move(object));

        if (!is_replaced_control(node)) {
            for (std::size_t i = node.children.size(); i > 0; --i) {
                if (should_descend_into_child(node, *node.children[i - 1])) {
                    pending.push_back(PendingNode{node.children[i - 1].get(), object_raw,
                        &object_raw->style, false, i - 1});
                }
            }
        }
    }

    if (budget_reported) {
        if (!contents_sources) contents_sources = std::make_shared<ContentsSourceTopology>();
        contents_sources->complete = false;
    }
    if (has_contents || budget_reported) view->contents_sources = std::move(contents_sources);

    if (reuse != nullptr) reuse->state_->pending_tree = view_raw;

    return view;
}

RenderObjectPtr RenderTreeBuilder::make_render_object(MonotonicArena* arena) const {
    if (arena == nullptr) {
        return RenderObjectPtr(new RenderObject, RenderObjectDeleter{false});
    }
    return RenderObjectPtr(&arena->create<RenderObject>(), RenderObjectDeleter{true});
}

RenderObjectType RenderTreeBuilder::render_type_for(const Node& node, const Style& style) const {
    if (node.type == NodeType::Text) {
        return RenderObjectType::Text;
    }
    if (style.display == Display::Inline || style.display == Display::InlineBlock) {
        return RenderObjectType::Inline;
    }
    return RenderObjectType::Block;
}

std::size_t count_render_objects(const RenderObject& root) {
    std::size_t count = 0;
    std::vector<const RenderObject*> pending;
    pending.push_back(&root);
    while (!pending.empty()) {
        const RenderObject* current = pending.back();
        pending.pop_back();
        ++count;
        for (const auto& child : current->children) {
            pending.push_back(child.get());
        }
    }
    return count;
}

} // namespace jellyframe
