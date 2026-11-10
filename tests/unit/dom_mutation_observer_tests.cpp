#include "test_crt_diagnostics.h"
#include "render_core/dom.h"
#include "render_core/feature_config.h"
#include "render_core/form_control.h"
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
#include "render_core/form_submission.h"
#endif

#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <vector>

namespace {
long allocation_countdown = -1;
#if defined(_MSC_VER) && _ITERATOR_DEBUG_LEVEL != 0
// MSVC checked containers allocate their iterator proxy in noexcept default
// constructors. Exhaustion there terminates before the operation under test can
// catch bad_alloc. Allow that metadata, then fail the intended vector storage.
constexpr long vector_metadata_allocations = 1;
#else
constexpr long vector_metadata_allocations = 0;
#endif
}

void* operator new(std::size_t bytes) {
    if (allocation_countdown == 0) {
#if defined(_MSC_VER) && _ITERATOR_DEBUG_LEVEL != 0
        // Unwinding constructs checked-container proxies in Node destruction.
        // Inject one storage failure without breaking those noexcept helpers.
        allocation_countdown = -1;
#endif
        throw std::bad_alloc();
    }
    if (allocation_countdown > 0) --allocation_countdown;
    if (void* memory = std::malloc(bytes ? bytes : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

using namespace jellyframe;
namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Record {
    Node* owner;
    DomDirtyFlags flags;
    std::uint64_t generation;
};
using Records = std::vector<Record>;
void observe(Node& node, DomDirtyFlags flags, std::uint64_t generation, void* context) {
    static_cast<Records*>(context)->push_back({&node, flags, generation});
}
void count_destroyed(Node&, void* context) { ++*static_cast<unsigned*>(context); }
void report_registration_during_destruction(Node& node, void*) { mark_dirty(node, DomDirtyPaint); }

void exact_values_inheritance_and_matching_clear() {
    auto root = make_element("body");
    Node& owner = root->append_child(make_element("div"));
    Records inherited, overridden;
    root->set_mutation_observer(observe, &inherited);
    auto generation = root->mutation_generation;
    owner.set_attribute("id", "owner");
    check(inherited.size() == 1 && inherited[0].owner == &owner &&
          inherited[0].flags == (DomDirtyAttributes | DomDirtyStyle | DomDirtyLayout) &&
          inherited[0].generation == ++generation, "attribute observer has exact values");
    owner.set_attribute("id", "owner");
    mark_dirty(owner, DomDirtyNone);
    check(inherited.size() == 1 && root->mutation_generation == generation, "no-op writes are silent");
    owner.set_text_content("first");
    Node* text = owner.children.front().get();
    owner.set_text_content("second");
    check(inherited.back().owner == text && inherited.back().flags == (DomDirtyText | DomDirtyLayout),
          "sole text child is the actual mutation owner");
    text->set_mutation_observer(observe, &overridden);
    root->clear_mutation_observer(observe, &inherited);
    owner.set_attribute("data-cleared", "yes");
    text->set_text("override");
    check(overridden.size() == 1 && overridden[0].owner == text, "matching clear preserves subtree override");
    root->set_mutation_observer(observe, &inherited);
    auto detached = root->detach_child(owner);
    const auto detached_records = inherited.size();
    detached->set_attribute("detached", "yes");
    check(inherited.size() == detached_records, "detached inherited observer is cleared");
    text->set_mutation_observer(observe, &overridden);
    root->append_child(std::move(detached));
    text->set_text("reattached");
    check(inherited.back().owner == text && overridden.size() == 1, "attachment replaces subtree overrides");
    root->clear_mutation_observer(observe, &inherited);
    auto unobserved = make_element("unobserved");
    auto independent = make_element("independent");
    independent->set_mutation_observer(observe, &overridden);
    Node& unobserved_child = unobserved->append_child(std::move(independent));
    const auto override_count = overridden.size();
    mark_dirty(unobserved_child, DomDirtyPaint);
    check(overridden.size() == override_count, "attachment inherits a null parent registration");
}

void registration_allocation_failure_is_atomic_and_clear_is_noexcept() {
    auto root = make_element("body");
    Node& child = root->append_child(make_element("div"));
    Node& grandchild = child.append_child(make_element("span"));
    Records old_records, replacement;
    root->set_mutation_observer(observe, &old_records);
    for (long fail_after : {0L, 1L}) {
        bool threw = false;
        allocation_countdown = fail_after + vector_metadata_allocations;
        try { root->set_mutation_observer(observe, &replacement); }
        catch (const std::bad_alloc&) { threw = true; }
        allocation_countdown = -1;
        check(threw, "registration allocation failure is injected");
        mark_dirty(grandchild, DomDirtyPaint);
        mark_dirty(*root, DomDirtyPaint);
        check(replacement.empty() && old_records.size() == std::size_t(2 * (fail_after + 1)),
              "failed registration preserves every prior observer");
    }
    grandchild.set_mutation_observer(observe, &replacement);
    allocation_countdown = vector_metadata_allocations;
    root->clear_mutation_observer(observe, &old_records);
    allocation_countdown = -1;
    const auto previous = old_records.size();
    mark_dirty(child, DomDirtyPaint);
    mark_dirty(grandchild, DomDirtyPaint);
    check(old_records.size() == previous && replacement.size() == 1,
          "allocation-free matching clear retains independent override");
    grandchild.clear_mutation_observer(observe, &replacement);

    root->set_mutation_observer(observe, &old_records);
    auto leaf = make_element("p");
    unsigned destroyed = 0;
    leaf->add_destroy_observer(count_destroyed, &destroyed);
    const auto count = root->children.size();
    const auto generation = root->mutation_generation;
    bool threw = false;
    allocation_countdown = 2 * vector_metadata_allocations; // insert_child owns another temporary vector.
    try { root->append_child(std::move(leaf)); }
    catch (const std::bad_alloc&) { threw = true; }
    allocation_countdown = -1;
    check(threw && root->children.size() == count && root->mutation_generation == generation && destroyed == 1,
          "attachment preflight failure preserves parent and independent child destruction");
    root->clear_mutation_observer(observe, &old_records);

    auto empty = make_element("empty");
    empty->set_mutation_observer(observe, &old_records);
    auto pending = make_element("pending");
    bool insertion_threw = false;
    allocation_countdown = 1 + 3 * vector_metadata_allocations; // Includes the returned vector proxy in MSVC Debug.
    try { empty->append_child(std::move(pending)); }
    catch (const std::bad_alloc&) { insertion_threw = true; }
    allocation_countdown = -1;
    check(insertion_threw && empty->children.empty() && empty->mutation_generation == 0,
          "parent storage allocation failure cannot commit an attachment");
    empty->clear_mutation_observer(observe, &old_records);

    // insert_child owns its argument by value: failed preflight consumes and
    // destroys it. Destroy callbacks inspect the unchanged prior registrations.
    auto parent = make_element("parent");
    auto pending_tree = make_element("pending-tree");
    Node& pending_leaf = pending_tree->append_child(make_element("pending-leaf"));
    Records prior, inherited;
    prior.reserve(2);
    parent->set_mutation_observer(observe, &inherited);
    pending_tree->set_mutation_observer(observe, &prior);
    pending_tree->add_destroy_observer(report_registration_during_destruction, nullptr);
    pending_leaf.add_destroy_observer(report_registration_during_destruction, nullptr);
    bool collection_threw = false;
    allocation_countdown = 1 + 2 * vector_metadata_allocations; // Root collected, second subtree collection allocation fails.
    try { parent->append_child(std::move(pending_tree)); }
    catch (const std::bad_alloc&) { collection_threw = true; }
    allocation_countdown = -1;
    check(collection_threw && !pending_tree && parent->children.empty() &&
          parent->mutation_generation == 0 && prior.size() == 2 && inherited.empty(),
          "failed preflight consumes child without partial observer publication or tree insertion");
    parent->clear_mutation_observer(observe, &inherited);
}

void deep_tree_and_wide_tree_cleanup_survive_allocation_failure() {
    auto root = make_element("root");
    Node* deepest = root.get();
    for (unsigned index = 0; index < 20000; ++index)
        deepest = &deepest->append_child(make_element("n"));
    Records records;
    root->set_mutation_observer(observe, &records);
    mark_dirty(*deepest, DomDirtyPaint);
    check(records.size() == 1 && records.back().owner == deepest, "deep registration is stack safe");
    allocation_countdown = vector_metadata_allocations;
    root->clear_mutation_observer(observe, &records);
    allocation_countdown = -1;
    mark_dirty(*deepest, DomDirtyPaint);
    check(records.size() == 1, "deep allocation-free clear terminates and clears the leaf");

    auto wide = make_element("wide");
    for (unsigned index = 0; index < 1024; ++index) wide->append_child(make_element("n"));
    wide->set_mutation_observer(observe, &records);
    allocation_countdown = vector_metadata_allocations;
    wide->clear_mutation_observer(observe, &records);
    allocation_countdown = -1;
    for (auto& child : wide->children) mark_dirty(*child, DomDirtyPaint);
    check(records.size() == 1, "wide allocation-free clear visits every sibling");
}

struct Reentrant {
    Node* root;
    Node* other;
    Records records;
};
void reenter(Node& node, DomDirtyFlags flags, std::uint64_t generation, void* context) {
    auto& state = *static_cast<Reentrant*>(context);
    check(state.root->mutation_generation == generation,
          "callback observes final root generation");
    for (Node* ancestor = &node; ancestor; ancestor = ancestor->parent)
        check((ancestor->dirty_flags & flags) == flags,
              "callback observes completed ancestor dirty propagation");
    state.records.push_back({&node, flags, generation});
    if (&node != state.other) {
        node.clear_mutation_observer(reenter, context);
        mark_dirty(*state.other, DomDirtyPaint);
    }
}
void throw_after_commit(Node&, DomDirtyFlags, std::uint64_t, void*) { throw std::runtime_error("callback"); }
void callback_reentrance_exceptions_and_destroy_observers_are_independent() {
    auto root = make_element("root");
    Node& first = root->append_child(make_element("first"));
    Node& second = root->append_child(make_element("second"));
    Reentrant state{root.get(), &second, {}};
    root->set_mutation_observer(reenter, &state);
    const auto generation = root->mutation_generation;
    mark_dirty(first, DomDirtyLayout);
    check(state.records.size() == 2 && state.records[0].owner == &first &&
          state.records[1].owner == &second && state.records[0].generation == generation + 1 &&
          state.records[1].generation == generation + 2, "reentrant writes preserve exact owners and generations");
    root->clear_mutation_observer(reenter, &state);
    first.set_mutation_observer(throw_after_commit, nullptr);
    bool threw = false;
    try { first.set_attribute("committed", "yes"); }
    catch (const std::runtime_error&) { threw = true; }
    check(threw && first.attribute("committed") == "yes" && (first.dirty_flags & DomDirtyAttributes),
          "callback exception propagates after the mutation commits");
    first.clear_mutation_observer(throw_after_commit, nullptr);
    unsigned destroyed = 0;
    first.add_destroy_observer(count_destroyed, &destroyed);
    first.set_mutation_observer(throw_after_commit, nullptr);
    root->remove_child(first);
    check(destroyed == 1, "mutation registration does not replace independent destroy observers");
}

struct AttachReentrant { Node* parent; bool entered = false; };
void append_during_callback(Node& owner, DomDirtyFlags, std::uint64_t generation, void* context) {
    auto& state = *static_cast<AttachReentrant*>(context);
    check(owner.mutation_generation == generation && (owner.dirty_flags & DomDirtyTree),
          "callback observes completed generation and dirty propagation");
    if (!state.entered) {
        state.entered = true;
        for (unsigned i = 0; i < 64; ++i) state.parent->append_child(make_element("nested"));
    }
}
void attachment_return_survives_reentrant_sibling_insertions() {
    auto root = make_element("root");
    AttachReentrant state{root.get()};
    root->set_mutation_observer(append_during_callback, &state);
    auto child = make_element("first");
    Node* expected = child.get();
    Node& attached = root->append_child(std::move(child));
    check(&attached == expected && root->children.size() == 65,
          "attachment returns original child after reentrant sibling insertion");
    root->clear_mutation_observer(append_during_callback, &state);
}

void compound_helpers_report_each_core_owned_mutation() {
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
    auto form = make_element("form");
    Node& first = form->append_child(make_element("input"));
    first.set_attribute("value", "default");
    Node& second = form->append_child(make_element("input"));
    second.set_attribute("type", "checkbox");
    set_form_control_value(first, "changed");
    set_form_control_checked(second, true);
    Records records;
    form->set_mutation_observer(observe, &records);
    const auto generation = form->mutation_generation;
    check(reset_form(*form), "compound form reset completes");
    check(records.size() == 2 && records[0].owner == &first && records[1].owner == &second &&
          records[0].flags == (DomDirtyStyle | DomDirtyPaint) &&
          records[1].flags == (DomDirtyStyle | DomDirtyPaint) &&
          records[0].generation == generation + 1 && records[1].generation == generation + 2,
          "compound form helper emits each exact direct control mutation");
    form->clear_mutation_observer(observe, &records);
#endif
}
}

int main() {
    configure_test_crt_diagnostics();
    try {
        exact_values_inheritance_and_matching_clear();
        registration_allocation_failure_is_atomic_and_clear_is_noexcept();
        deep_tree_and_wide_tree_cleanup_survive_allocation_failure();
        callback_reentrance_exceptions_and_destroy_observers_are_independent();
        attachment_return_survives_reentrant_sibling_insertions();
        compound_helpers_report_each_core_owned_mutation();
    } catch (const std::exception& error) {
        allocation_countdown = -1;
        std::cerr << "DOM mutation observer test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "Core-owned exact DOM mutation observer tests passed\n";
    return 0;
}
