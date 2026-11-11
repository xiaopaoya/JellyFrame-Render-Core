#pragma once

#include "render_core/layout.h"

#include <cstddef>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace jellyframe::detail {

enum class LayoutMutationJournalStatus { Ready, ResourceLimit, AlreadyActive };

class LayoutMutationJournal {
public:
    LayoutMutationJournal() = default;
    LayoutMutationJournal(const LayoutMutationJournal&) = delete;
    LayoutMutationJournal& operator=(const LayoutMutationJournal&) = delete;
    ~LayoutMutationJournal() noexcept { rollback(); }

    LayoutMutationJournalStatus capture(LayoutBox& root, std::size_t max_boxes,
        std::size_t max_scratch_bytes = std::numeric_limits<std::size_t>::max()) {
        if (active_) return LayoutMutationJournalStatus::AlreadyActive;
        if (max_boxes == 0) return LayoutMutationJournalStatus::ResourceLimit;
        if (max_boxes > max_scratch_bytes / sizeof(const LayoutBox*))
            return LayoutMutationJournalStatus::ResourceLimit;
        std::unique_ptr<const LayoutBox*[]> pending;
        std::size_t pending_size = 0;
        try {
            pending.reset(new const LayoutBox*[max_boxes]);
            pending[pending_size++] = &root;
        } catch (const std::bad_alloc&) {
            return LayoutMutationJournalStatus::ResourceLimit;
        } catch (const std::length_error&) {
            return LayoutMutationJournalStatus::ResourceLimit;
        }
        std::size_t count = 0;
        while (pending_size != 0) {
            const LayoutBox* box = pending[--pending_size];
            ++count;
            if (count > max_boxes || box->children.size() > max_boxes - count ||
                pending_size > max_boxes - count - box->children.size())
                return LayoutMutationJournalStatus::ResourceLimit;
            for (const auto& child : box->children) pending[pending_size++] = child.get();
        }
        static_assert(std::is_nothrow_swappable<TextLayoutCache>::value,
                      "layout mutation rollback requires noexcept text-cache swap");
        const auto pending_bytes = max_boxes * sizeof(const LayoutBox*);
        if (pending_bytes > max_scratch_bytes || count > (max_scratch_bytes - pending_bytes) / sizeof(Slot))
            return LayoutMutationJournalStatus::ResourceLimit;
        try {
            // Raw allocation plus counted construction avoids new[]'s hidden
            // destructor cookie, keeping the charged snapshot payload exact.
            std::unique_ptr<Slot[], SlotDeleter> next(
                static_cast<Slot*>(::operator new(count * sizeof(Slot))), SlotDeleter{});
            for (std::size_t index = 0; index < count; ++index) {
                new (next.get() + index) Slot;
                ++next.get_deleter().count;
            }
            slots_ = std::move(next);
            slot_count_ = count;
        } catch (const std::bad_alloc&) {
            return LayoutMutationJournalStatus::ResourceLimit;
        } catch (const std::length_error&) {
            return LayoutMutationJournalStatus::ResourceLimit;
        }
        scratch_peak_bytes_ = pending_bytes + count * sizeof(Slot);
        slots_[0].box = &root;
        std::size_t write = 1;
        for (std::size_t read = 0; read < write; ++read) {
            for (const auto& child : slots_[read].box->children) {
                if (write >= slot_count_) {
                    slots_.reset(); slot_count_ = 0;
                    return LayoutMutationJournalStatus::ResourceLimit;
                }
                slots_[write++].box = child.get();
            }
        }
        if (write != slot_count_) {
            slots_.reset(); slot_count_ = 0;
            return LayoutMutationJournalStatus::ResourceLimit;
        }
        for (std::size_t index = 0; index < slot_count_; ++index) {
            Slot& slot = slots_[index];
            slot.rect = slot.box->rect;
            slot.content_geometry = slot.box->content_geometry;
        }
        for (std::size_t index = 0; index < slot_count_; ++index) {
            Slot& slot = slots_[index];
            using std::swap;
            swap(slot.cache, slot.box->text_layout_cache);
        }
        active_ = true;
        return LayoutMutationJournalStatus::Ready;
    }

    void rollback() noexcept {
        if (!active_) return;
        for (std::size_t index = 0; index < slot_count_; ++index) {
            Slot& slot = slots_[index];
            slot.box->rect = slot.rect;
            slot.box->content_geometry = slot.content_geometry;
            using std::swap;
            swap(slot.cache, slot.box->text_layout_cache);
        }
        slots_.reset(); slot_count_ = 0;
        active_ = false;
    }

    void commit() noexcept {
        if (!active_) return;
        slots_.reset(); slot_count_ = 0;
        active_ = false;
    }

    bool active() const noexcept { return active_; }
    std::size_t size() const noexcept { return active_ ? slot_count_ : 0; }
    static std::size_t capture_scratch_bytes(std::size_t boxes) noexcept {
        constexpr auto per_box = sizeof(Slot) + sizeof(const LayoutBox*);
        return boxes > std::numeric_limits<std::size_t>::max() / per_box ?
            std::numeric_limits<std::size_t>::max() : boxes * per_box;
    }
    // Includes capture's simultaneously live traversal and snapshot payload.
    std::size_t scratch_peak_bytes() const noexcept { return scratch_peak_bytes_; }

private:
    struct Slot {
        LayoutBox* box = nullptr;
        Rect rect{};
        LayoutContentGeometry content_geometry{};
        TextLayoutCache cache{};
    };
    struct SlotDeleter {
        std::size_t count = 0;
        void operator()(Slot* slots) const noexcept {
            for (std::size_t index = 0; index < count; ++index) slots[index].~Slot();
            ::operator delete(slots);
        }
    };
    std::unique_ptr<Slot[], SlotDeleter> slots_{nullptr, SlotDeleter{}};
    std::size_t slot_count_ = 0;
    bool active_ = false;
    std::size_t scratch_peak_bytes_ = 0;
};

} // namespace jellyframe::detail
