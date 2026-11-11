#include "../../src/layout_mutation_journal.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>

static int allocations_until_failure = -1;
static std::size_t largest_allocation_bytes = 0;
void* operator new(std::size_t bytes) {
    if (allocations_until_failure == 0) throw std::bad_alloc();
    if (allocations_until_failure > 0) --allocations_until_failure;
    if (auto* result = std::malloc(bytes ? bytes : 1)) {
        if (bytes > largest_allocation_bytes) largest_allocation_bytes = bytes;
        return result;
    }
    throw std::bad_alloc();
}
void operator delete(void* data) noexcept { std::free(data); }
void operator delete(void* data, std::size_t) noexcept { std::free(data); }
// Route array allocations through the same deterministic hook, including ASan.
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete[](void* data) noexcept { ::operator delete(data); }
void operator delete[](void* data, std::size_t) noexcept { ::operator delete(data); }

using namespace jellyframe;
using namespace jellyframe::detail;

namespace {

LayoutBoxPtr make_child() {
    return LayoutBoxPtr(new LayoutBox, LayoutBoxDeleter{false});
}

void fill_cache(LayoutBox& box, const TextSystem* text_system, const std::string& prefix) {
    auto& cache = box.text_layout_cache;
    cache.valid = true;
    cache.source_text = prefix + " source text";
    cache.rendered_text = prefix + " rendered text";
    cache.lines = {prefix + " line one", prefix + " line two"};
    cache.text_system = text_system;
    cache.text_system_generation = 17;
    cache.prepared_text = true;
    cache.prepared_packet = prefix + " packet";
    cache.prepared_lines.push_back({prefix + " prepared packet", 11, 12, 13});
    cache.prepared_origin_x = 14;
    cache.prepared_baseline_y = 15;
    cache.rect_x = 16;
    cache.rect_y = 17;
    cache.rect_width = 18;
    cache.rect_height = 19;
}

void cap_refusal_is_pre_mutation() {
    int context = 0;
    TextSystem* system = reinterpret_cast<TextSystem*>(&context);
    LayoutBox root;
    root.rect = {1, 2, 3, 4};
    fill_cache(root, system, "root");
    auto child = make_child();
    child->rect = {5, 6, 7, 8};
    fill_cache(*child, system, "child");
    root.children.push_back(std::move(child));

    LayoutMutationJournal journal;
    assert(journal.capture(root, 1) == LayoutMutationJournalStatus::ResourceLimit);
    assert(!journal.active());
    assert(root.rect.x == 1 && root.text_layout_cache.source_text == "root source text");
    assert(root.text_layout_cache.lines.size() == 2 && root.text_layout_cache.prepared_lines.size() == 1);
    const auto& retained_child = *root.children.front();
    assert(retained_child.rect.x == 5 && retained_child.text_layout_cache.source_text == "child source text");
    assert(retained_child.text_layout_cache.lines.size() == 2 &&
           retained_child.text_layout_cache.prepared_lines.front().packet == "child prepared packet");
}

void allocation_refusal_is_pre_mutation() {
    LayoutBox root;
    root.rect = {1, 2, 3, 4};
    root.content_geometry = {1250, 2750, 3000, 4000, false};
    root.text_layout_cache.source_text = "retained cache";
    for (int successful_allocations : {0, 1}) {
        LayoutMutationJournal journal;
        allocations_until_failure = successful_allocations;
        const auto status = journal.capture(root, 1);
        allocations_until_failure = -1;
        assert(status == LayoutMutationJournalStatus::ResourceLimit && !journal.active());
        assert(root.rect.x == 1 && root.content_geometry.x == 1250);
        assert(root.content_geometry.height() == 4000 && !root.content_geometry.height_definite());
        assert(root.text_layout_cache.source_text == "retained cache");
        assert(journal.capture(root, 1) == LayoutMutationJournalStatus::Ready);
        journal.rollback();
        assert(root.text_layout_cache.source_text == "retained cache");
    }
}

void rollback_restores_nested_rectangles_and_allocated_caches() {
    int context = 0;
    TextSystem* system = reinterpret_cast<TextSystem*>(&context);
    LayoutBox root;
    root.rect = {10, 20, 30, 40};
    root.content_geometry = {10250, 20750, 30125, 40250, true};
    fill_cache(root, system, "old root");
    auto child = make_child();
    child->rect = {50, 60, 70, 80};
    child->content_geometry = {50250, 60750, 70125, 80250, false};
    fill_cache(*child, system, "old child");
    auto grandchild = make_child();
    grandchild->rect = {90, 100, 110, 120};
    fill_cache(*grandchild, system, "old grandchild");
    child->children.push_back(std::move(grandchild));
    root.children.push_back(std::move(child));

    LayoutMutationJournal journal;
    assert(journal.capture(root, 3) == LayoutMutationJournalStatus::Ready);
    assert(journal.active() && journal.size() == 3);
    root.rect = {1, 1, 1, 1};
    root.content_geometry = {};
    root.text_layout_cache = TextLayoutCache{};
    root.text_layout_cache.lines.push_back("new root line");
    root.children[0]->rect = {2, 2, 2, 2};
    root.children[0]->content_geometry = {};
    root.children[0]->text_layout_cache = TextLayoutCache{};
    root.children[0]->text_layout_cache.prepared_lines.push_back({"new child", 0, 0, 0});
    root.children[0]->children[0]->rect = {3, 3, 3, 3};
    root.children[0]->children[0]->text_layout_cache = TextLayoutCache{};
    root.children[0]->children[0]->text_layout_cache.source_text = "new grandchild";

    journal.rollback();
    assert(!journal.active() && journal.size() == 0);
    assert(root.rect.x == 10 && root.rect.height == 40);
    assert(root.content_geometry.x == 10250 && root.content_geometry.width == 30125 &&
           root.content_geometry.height() == 40250 && root.content_geometry.height_definite());
    assert(root.text_layout_cache.source_text == "old root source text");
    assert(root.text_layout_cache.lines[1] == "old root line two");
    assert(root.text_layout_cache.prepared_lines[0].packet == "old root prepared packet");
    const auto& child_box = *root.children[0];
    assert(child_box.rect.x == 50 && child_box.rect.height == 80);
    assert(child_box.content_geometry.y == 60750 && child_box.content_geometry.height() == 80250 &&
           !child_box.content_geometry.height_definite());
    assert(child_box.text_layout_cache.rendered_text == "old child rendered text");
    assert(child_box.text_layout_cache.prepared_packet == "old child packet");
    const auto& grandchild_box = *child_box.children[0];
    assert(grandchild_box.rect.x == 90 && grandchild_box.rect.height == 120);
    assert(grandchild_box.text_layout_cache.source_text == "old grandchild source text");
    assert(grandchild_box.text_layout_cache.prepared_lines.size() == 1);
}

void commit_keeps_new_state() {
    LayoutBox root;
    root.rect = {1, 2, 3, 4};
    root.text_layout_cache.source_text = "old";
    LayoutMutationJournal journal;
    assert(journal.capture(root, 1) == LayoutMutationJournalStatus::Ready);
    root.rect = {5, 6, 7, 8};
    root.content_geometry = {5250, 6750, 7125, 8250, false};
    root.text_layout_cache = TextLayoutCache{};
    root.text_layout_cache.source_text = "new";
    journal.commit();
    journal.commit();
    journal.rollback();
    assert(!journal.active() && journal.size() == 0);
    assert(root.rect.x == 5 && root.rect.height == 8);
    assert(root.content_geometry.x == 5250 && root.content_geometry.height() == 8250 &&
           !root.content_geometry.height_definite());
    assert(root.text_layout_cache.source_text == "new");
}

void destruction_rolls_back_and_explicit_rollback_is_idempotent() {
    LayoutBox root;
    root.rect = {10, 20, 30, 40};
    root.text_layout_cache.source_text = "before";
    {
        LayoutMutationJournal journal;
        assert(journal.capture(root, 1) == LayoutMutationJournalStatus::Ready);
        root.rect = {1, 2, 3, 4};
        root.text_layout_cache.source_text = "during";
        journal.rollback();
        journal.rollback();
        assert(root.rect.x == 10 && root.text_layout_cache.source_text == "before");
    }
    assert(root.rect.x == 10 && root.rect.height == 40);
    assert(root.text_layout_cache.source_text == "before");

    {
        LayoutMutationJournal journal;
        assert(journal.capture(root, 1) == LayoutMutationJournalStatus::Ready);
        root.rect = {5, 6, 7, 8};
        root.text_layout_cache.source_text = "before destructor";
    }
    assert(root.rect.x == 10 && root.rect.height == 40);
    assert(root.text_layout_cache.source_text == "before");

    {
        LayoutMutationJournal journal;
        assert(journal.capture(root, 1) == LayoutMutationJournalStatus::Ready);
        root.rect = {50, 60, 70, 80};
        root.text_layout_cache.source_text = "committed";
        journal.commit();
    }
    assert(root.rect.x == 50 && root.rect.height == 80);
    assert(root.text_layout_cache.source_text == "committed");
}


}

int main() {
    cap_refusal_is_pre_mutation();
    allocation_refusal_is_pre_mutation();
    rollback_restores_nested_rectangles_and_allocated_caches();
    commit_keeps_new_state();
    destruction_rolls_back_and_explicit_rollback_is_idempotent();
    struct OldSlot { LayoutBox* box; Rect rect; TextLayoutCache cache; };
    struct CurrentSlot { LayoutBox* box; Rect rect; LayoutContentGeometry geometry; TextLayoutCache cache; };
    LayoutBox root;
    LayoutMutationJournal journal;
    largest_allocation_bytes = 0;
    assert(journal.capture(root, 1) == LayoutMutationJournalStatus::Ready);
    const auto allocated_slot_bytes = largest_allocation_bytes;
    assert(allocated_slot_bytes == sizeof(CurrentSlot));
    const auto exact_scratch = sizeof(CurrentSlot) + sizeof(const LayoutBox*);
    assert(journal.scratch_peak_bytes() == exact_scratch);
    journal.rollback();
    root.text_layout_cache.source_text = "retained";
    assert(journal.capture(root, 1, exact_scratch - 1) == LayoutMutationJournalStatus::ResourceLimit);
    assert(!journal.active() && root.text_layout_cache.source_text == "retained");
    assert(journal.capture(root, 1, exact_scratch) == LayoutMutationJournalStatus::Ready);
    assert(journal.scratch_peak_bytes() == exact_scratch);
    journal.rollback();
    std::cout << "journal slot old=" << sizeof(OldSlot) << " current=" << sizeof(CurrentSlot)
        << " actual single-slot allocation=" << allocated_slot_bytes
        << "; layout mutation journal tests passed\n";
}
