#pragma once

#include "render_core/layout.h"

#include <cstddef>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace jellyframe::detail {

enum class LayoutMutationJournalStatus { Ready, ResourceLimit, AlreadyActive };

class LayoutMutationJournal {
public:
    LayoutMutationJournal() = default;
    LayoutMutationJournal(const LayoutMutationJournal&) = delete;
    LayoutMutationJournal& operator=(const LayoutMutationJournal&) = delete;
    ~LayoutMutationJournal() noexcept { rollback(); }

    LayoutMutationJournalStatus capture(LayoutBox& root, std::size_t max_boxes) {
        if (active_) return LayoutMutationJournalStatus::AlreadyActive;
        if (max_boxes == 0) return LayoutMutationJournalStatus::ResourceLimit;
        std::vector<const LayoutBox*> pending;
        try {
            pending.reserve(max_boxes);
            pending.push_back(&root);
        } catch (const std::bad_alloc&) {
            return LayoutMutationJournalStatus::ResourceLimit;
        } catch (const std::length_error&) {
            return LayoutMutationJournalStatus::ResourceLimit;
        }
        std::size_t count = 0;
        while (!pending.empty()) {
            const LayoutBox* box = pending.back();
            pending.pop_back();
            ++count;
            if (count > max_boxes || box->children.size() > max_boxes - count ||
                pending.size() > max_boxes - count - box->children.size())
                return LayoutMutationJournalStatus::ResourceLimit;
            for (const auto& child : box->children) pending.push_back(child.get());
        }
        static_assert(std::is_nothrow_swappable<TextLayoutCache>::value,
                      "layout mutation rollback requires noexcept text-cache swap");
        try {
            slots_.resize(count);
        } catch (const std::bad_alloc&) {
            return LayoutMutationJournalStatus::ResourceLimit;
        } catch (const std::length_error&) {
            return LayoutMutationJournalStatus::ResourceLimit;
        }
        slots_[0].box = &root;
        std::size_t write = 1;
        for (std::size_t read = 0; read < write; ++read) {
            for (const auto& child : slots_[read].box->children) {
                if (write >= slots_.size()) {
                    slots_.clear();
                    return LayoutMutationJournalStatus::ResourceLimit;
                }
                slots_[write++].box = child.get();
            }
        }
        if (write != slots_.size()) {
            slots_.clear();
            return LayoutMutationJournalStatus::ResourceLimit;
        }
        for (Slot& slot : slots_) slot.rect = slot.box->rect;
        for (Slot& slot : slots_) {
            using std::swap;
            swap(slot.cache, slot.box->text_layout_cache);
        }
        active_ = true;
        return LayoutMutationJournalStatus::Ready;
    }

    void rollback() noexcept {
        if (!active_) return;
        for (Slot& slot : slots_) {
            slot.box->rect = slot.rect;
            using std::swap;
            swap(slot.cache, slot.box->text_layout_cache);
        }
        slots_.clear();
        active_ = false;
    }

    void commit() noexcept {
        if (!active_) return;
        slots_.clear();
        active_ = false;
    }

private:
    struct Slot {
        LayoutBox* box = nullptr;
        Rect rect{};
        TextLayoutCache cache{};
    };
    std::vector<Slot> slots_;
    bool active_ = false;
};

} // namespace jellyframe::detail
