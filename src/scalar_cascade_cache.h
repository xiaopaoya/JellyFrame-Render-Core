#pragma once
#include "render_core/style.h"
#include <algorithm>
#include <array>

namespace jellyframe::detail {
// No DOM addresses are retained. A destroyed node/reused address cannot hit
// without reproducing every cascade input. Styles are saved before tree
// inheritance, overrides, blockification, layout and counter materialization.
struct ScalarCascadeCache {
    struct Entry {
        std::string key;
        Style style;
        std::unique_ptr<Entry> next;
    };
    std::unique_ptr<Entry> first;
    std::size_t entries = 0;
    std::size_t payload_bytes = sizeof(ScalarCascadeCache);

    const Style *find(const std::string &key) const {
        for (auto *entry = first.get(); entry; entry = entry->next.get())
            if (entry->key == key)
                return &entry->style;
        return nullptr;
    }
    static bool scalar(const Style &style) {
        if (style.transitions.capacity() || style.animations.capacity())
            return false;
        for (unsigned index = 0; index < 11; ++index) {
            SizeKind kind{};
            std::int64_t units = 0, percentage = 0;
            if (style.computed_sizes.read_typed(static_cast<SizeProperty>(index), kind, units, percentage) &&
                (index >= 6 || kind == SizeKind::LengthPercentage))
                return false;
        }
        return true;
    }
    static std::size_t string_payload(const Style &s) {
        // Count capacity+terminator even for inline strings (conservative).
        return s.overflow.capacity() + s.transform.capacity() + s.position.capacity() +
               s.before_content_text.capacity() + s.before_counter_name.capacity() +
               s.before_counter_suffix.capacity() + s.after_content_text.capacity() +
               s.after_counter_name.capacity() + s.after_counter_suffix.capacity() + 9;
    }
    bool insert(std::string key, const Style &style, std::size_t entry_limit, std::size_t byte_limit) {
        if (entries >= std::min<std::size_t>(entry_limit, 16) || !scalar(style))
            return false;
        // Copy before admission so implementation-specific string capacity is
        // accounted, and publish only after every potentially throwing action.
        auto entry = std::make_unique<Entry>();
        entry->key = std::move(key);
        entry->style = style;
        // Only fixed slots are admitted: no nested banks need accounting.
        // Charge a whole owner per entry even if another Style shares it.
        const auto bytes = sizeof(Entry) + entry->key.capacity() + 1 + string_payload(entry->style) +
                           entry->style.computed_sizes.retained_payload_bytes();
        const auto limit = std::min<std::size_t>(byte_limit, 32 * 1024);
        if (payload_bytes > limit || bytes > limit - payload_bytes)
            return false;
        entry->next = std::move(first);
        first = std::move(entry);
        payload_bytes += bytes;
        ++entries;
        return true;
    }
};

// Length-prefix every variable component, including embedded NULs. No hash
// collisions, string-view lifetimes or padding bytes participate in equality.
struct ScalarCascadeKey {
    std::string bytes;
    bool valid = true;
    bool append(const char *source, std::size_t size) {
        if (!valid || size > 2048 - bytes.size()) {
            valid = false;
            return false;
        }
        bytes.append(source, size);
        return true;
    }
    template <class T> void number(T value) {
        append(reinterpret_cast<const char *>(&value), sizeof(value));
    }
    void text(std::string_view value) {
        number(value.size());
        append(value.data(), value.size());
    }
    void inputs(const Node &node, bool root, const StyleLengthResolutionContext &context,
                const CustomPropertyMap &variables) {
        if (node.attributes.size() > 64 || variables.size() > 64) {
            valid = false;
            return;
        }
        number(root);
        number(context.viewport_width);
        number(context.viewport_height);
        text(node.tag_name);
        number(node.attributes.size());
        for (const auto &attribute : node.attributes) {
            text(attribute.first);
            text(attribute.second);
        }
        // Unordered map insertion/bucket order is not a semantic dependency.
        std::array<const CustomPropertyMap::value_type *, 64> ordered{};
        std::size_t count = 0;
        for (const auto &variable : variables)
            ordered[count++] = &variable;
        std::sort(ordered.begin(), ordered.begin() + count,
                  [](auto *a, auto *b) { return a->first < b->first; });
        number(count);
        for (std::size_t i = 0; i < count; ++i) {
            text(ordered[i]->first);
            text(ordered[i]->second);
        }
    }
};
} // namespace jellyframe::detail
