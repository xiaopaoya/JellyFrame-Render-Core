#include "render_core/dom.h"
#include "render_core/html_parser.h"

#include <iostream>
#include <memory>
#include <stdexcept>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct DestroyObservation {
    bool called = false;
    Node* parent = nullptr;
};

void observe_destroyed_node_parent(Node& node, void* context) {
    auto& observation = *static_cast<DestroyObservation*>(context);
    observation.called = true;
    observation.parent = node.parent;
}

struct LifetimeObservation {
    int calls = 0;
    bool inactive_during_callback = false;
    bool late_registration_rejected = false;
    NodeLifetimeObserverRegistration* late_registration = nullptr;
};

void observe_node_lifetime(Node&, void* context) noexcept {
    auto& observation = *static_cast<LifetimeObservation*>(context);
    ++observation.calls;
}

void observe_and_probe_destroying_node(Node& node, void* context) noexcept {
    auto& observation = *static_cast<LifetimeObservation*>(context);
    ++observation.calls;
    observation.inactive_during_callback =
        observation.late_registration != nullptr && !observation.late_registration->active();
    observation.late_registration_rejected = node.add_lifetime_observer(
        *observation.late_registration, observe_node_lifetime, &observation);
}

struct PendingRemovalObservation {
    LifetimeObservation* lifetime = nullptr;
    std::unique_ptr<NodeLifetimeObserverRegistration>* pending = nullptr;
};

struct DestroyMutationObservation {
    int active_calls = 0;
    int pending_calls = 0;
    int appended_calls = 0;
};

void remove_pending_lifetime_registration(Node&, void* context) noexcept {
    auto& observation = *static_cast<PendingRemovalObservation*>(context);
    ++observation.lifetime->calls;
    observation.pending->reset();
}

void release_current_lifetime_registration(Node&, void* context) noexcept {
    auto* registration = static_cast<std::unique_ptr<NodeLifetimeObserverRegistration>*>(context);
    registration->reset();
}

void count_pending_destroy_observer(Node&, void* context) {
    ++static_cast<DestroyMutationObservation*>(context)->pending_calls;
}

void count_appended_destroy_observer(Node&, void* context) {
    ++static_cast<DestroyMutationObservation*>(context)->appended_calls;
}

void mutate_destroy_observers(Node& node, void* context) {
    auto& observation = *static_cast<DestroyMutationObservation*>(context);
    ++observation.active_calls;
    node.remove_destroy_observer(count_pending_destroy_observer, &observation);
    node.add_destroy_observer(count_appended_destroy_observer, &observation);
}

Node* find_first_by_tag(Node& node, const std::string& tag_name) {
    if (node.type == NodeType::Element && node.tag_name == tag_name) {
        return &node;
    }
    for (const auto& child : node.children) {
        Node* found = find_first_by_tag(*child, tag_name);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

void parser_returns_clean_document() {
    HtmlParser parser;
    auto document = parser.parse("<body><p>Hello</p></body>");

    check(subtree_dirty_flags(*document) == DomDirtyNone, "parsed document starts clean");
}

void compact_attribute_list_behaves_like_small_map() {
    auto element = make_element("button");
    const auto first_insert = element->attributes.emplace("id", "save");
    const auto duplicate_insert = element->attributes.emplace("id", "cancel");
    element->attributes["class"] = "primary";

    check(first_insert.second, "first attribute insert succeeds");
    check(!duplicate_insert.second, "duplicate attribute insert keeps first value");
    check(element->attribute("id") == "save", "duplicate attribute does not overwrite");
    check(element->attribute("class") == "primary", "operator[] stores attribute value");
    check(element->attributes.size() == 2, "attribute list size is compact");

    const auto class_it = element->attributes.find("class");
    check(class_it != element->attributes.end(), "attribute find locates class");
    element->attributes.erase(class_it);
    check(element->attributes.find("class") == element->attributes.end(), "attribute erase removes entry");
}

void append_and_remove_mark_tree_dirty() {
    auto document = make_element("document");
    Node& body = document->append_child(make_element("body"));
    clear_dirty_flags(*document);

    Node& paragraph = body.append_child(make_element("p"));
    check((subtree_dirty_flags(*document) & DomDirtyTree) != 0U, "append marks tree dirty");
    check((subtree_dirty_flags(*document) & DomDirtyLayout) != 0U, "append marks layout dirty");

    const DomDirtyFlags taken = take_dirty_flags(*document);
    check((taken & DomDirtyTree) != 0U, "take returns dirty flags");
    check(subtree_dirty_flags(*document) == DomDirtyNone, "take clears dirty flags");

    check(body.remove_child(paragraph), "remove child succeeds");
    check((subtree_dirty_flags(*document) & DomDirtyTree) != 0U, "remove marks tree dirty");
}

void insert_rejects_null_and_cycle_forming_nodes() {
    auto document = make_element("document");
    bool null_rejected = false;
    try {
        document->append_child(nullptr);
    } catch (const std::invalid_argument&) {
        null_rejected = true;
    }
    check(null_rejected, "null child insertion is rejected");

    Node& branch = document->append_child(make_element("branch"));
    Node& leaf = branch.append_child(make_element("leaf"));
    auto detached_branch = document->detach_child(branch);
    check(detached_branch != nullptr && detached_branch->parent == nullptr,
          "ancestor can be detached before cycle test");

    bool cycle_rejected = false;
    try {
        leaf.append_child(std::move(detached_branch));
    } catch (const std::invalid_argument&) {
        cycle_rejected = true;
    }
    check(cycle_rejected, "ancestor insertion into descendant is rejected");
}

void attributes_and_text_mark_specific_dirty_bits() {
    HtmlParser parser;
    auto document = parser.parse("<body><p id='note'>Old</p></body>");
    Node* paragraph = find_first_by_tag(*document, "p");
    check(paragraph != nullptr, "paragraph exists");

    paragraph->set_attribute("class", "highlight");
    DomDirtyFlags flags = subtree_dirty_flags(*document);
    check((flags & DomDirtyAttributes) != 0U, "attribute mutation marks attribute dirty");
    check((flags & DomDirtyStyle) != 0U, "class mutation marks style dirty");
    clear_dirty_flags(*document);

    paragraph->set_text_content("New");
    flags = subtree_dirty_flags(*document);
    check(paragraph->text_content() == "New", "text content updated");
    check((flags & DomDirtyTree) == 0U, "single text child update avoids tree dirty");
    check((flags & DomDirtyText) != 0U, "text content marks text dirty");
    check((flags & DomDirtyLayout) != 0U, "text content marks layout dirty");
    clear_dirty_flags(*document);

    check(paragraph->remove_attribute("class"), "attribute removed");
    flags = subtree_dirty_flags(*document);
    check((flags & DomDirtyAttributes) != 0U, "remove attribute marks attribute dirty");
}

void unchanged_text_content_stays_clean() {
    HtmlParser parser;
    auto document = parser.parse("<body><p>Same</p></body>");
    Node* paragraph = find_first_by_tag(*document, "p");
    check(paragraph != nullptr, "paragraph exists");

    paragraph->set_text_content("Same");
    check(subtree_dirty_flags(*document) == DomDirtyNone, "same textContent does not dirty document");
}

void text_content_replacement_detaches_nodes_before_destroy_observers() {
    auto document = make_element("document");
    Node& body = document->append_child(make_element("body"));
    Node& old_child = body.append_child(make_element("section"));
    DestroyObservation observation;
    old_child.add_destroy_observer(observe_destroyed_node_parent, &observation);

    body.set_text_content("replacement");

    check(observation.called, "replaced child destroy observer runs");
    check(observation.parent == nullptr, "replaced child is detached before its destroy observer runs");
    check(body.text_content() == "replacement", "replacement text remains attached");
}

void protected_lifetime_observers_survive_regular_replacement_and_removal() {
    auto node = make_element("div");
    NodeLifetimeObserverRegistration registration;
    LifetimeObservation observation;
    DestroyObservation ordinary_observation;
    check(node->add_lifetime_observer(registration, observe_node_lifetime, &observation),
          "protected lifetime observer registers with bounded observer storage");

    node->set_destroy_observer(observe_destroyed_node_parent, &ordinary_observation);
    node->remove_destroy_observer(observe_node_lifetime, &observation);
    check(registration.active(), "ordinary observer APIs cannot detach protected registration");

    node.reset();
    check(observation.calls == 1, "protected observer runs once before Node storage is released");
    check(!registration.active(), "registration is detached before its callback");
}

void lifetime_registration_detaches_before_node_and_during_callback() {
    LifetimeObservation observation;
    auto node = make_element("div");
    {
        NodeLifetimeObserverRegistration registration;
        check(node->add_lifetime_observer(registration, observe_node_lifetime, &observation),
              "temporary lifetime registration succeeds");
    }
    node.reset();
    check(observation.calls == 0, "destroying registration removes its notification");

    observation = {};
    auto second_node = make_element("div");
    auto pending = std::make_unique<NodeLifetimeObserverRegistration>();
    NodeLifetimeObserverRegistration current;
    PendingRemovalObservation pending_observation{&observation, &pending};
    check(second_node->add_lifetime_observer(*pending, observe_node_lifetime, &observation),
          "pending lifetime registration succeeds");
    check(second_node->add_lifetime_observer(current, remove_pending_lifetime_registration,
                                             &pending_observation),
          "active lifetime registration succeeds");
    second_node.reset();
    check(observation.calls == 1, "one callback may remove a pending registration safely");
    check(pending == nullptr, "pending registration owner can release its record in callback");
}

void lifetime_callbacks_cannot_rearm_a_destroying_node() {
    auto node = make_element("div");
    NodeLifetimeObserverRegistration registration;
    NodeLifetimeObserverRegistration late_registration;
    LifetimeObservation observation;
    observation.late_registration = &late_registration;
    check(node->add_lifetime_observer(registration, observe_and_probe_destroying_node, &observation),
          "lifetime callback registration succeeds");
    node.reset();
    check(observation.calls == 1, "lifetime callback runs once");
    check(observation.inactive_during_callback, "current registration is detached before callback");
    check(!observation.late_registration_rejected,
          "destroying Node refuses new protected lifetime registrations");
    check(!late_registration.active(), "failed rearm leaves registration detached");
}

void lifetime_callback_can_release_its_own_registration() {
    auto node = make_element("div");
    auto registration = std::make_unique<NodeLifetimeObserverRegistration>();
    check(node->add_lifetime_observer(*registration, release_current_lifetime_registration,
                                     &registration),
          "self-release lifetime registration succeeds");
    node.reset();
    check(registration == nullptr, "lifetime callback releases its own detached registration");
}

void destruction_drain_handles_observer_mutation_in_place() {
    auto node = make_element("div");
    DestroyMutationObservation observation;
    node->add_destroy_observer(mutate_destroy_observers, &observation);
    node->add_destroy_observer(count_pending_destroy_observer, &observation);
    node.reset();
    check(observation.active_calls == 1, "active destroy observer runs once");
    check(observation.pending_calls == 0, "active observer can remove a pending observer in place");
    check(observation.appended_calls == 0, "observer appended during drain waits for no later destruction pass");
}

void deep_subtree_replacement_and_teardown_are_iterative() {
    auto document = make_element("document");
    Node* current = document.get();
    for (int depth = 0; depth < 4096; ++depth) {
        current = &current->append_child(make_element("div"));
    }

    document->set_text_content("flat");
    check(document->children.size() == 1, "deep subtree replaced by one text node");
    check(document->children.front()->type == NodeType::Text, "replacement text node exists");
}

void deep_text_content_is_iterative_and_ordered() {
    auto document = make_element("document");
    Node* current = document.get();
    current->append_child(make_text("root-"));
    for (int depth = 0; depth < 4096; ++depth) {
        current = &current->append_child(make_element("span"));
        if (depth % 1024 == 0) {
            current->append_child(make_text("mid-"));
        }
    }
    current->append_child(make_text("leaf"));

    const std::string text = document->text_content();
    check(text == "root-mid-mid-mid-mid-leaf", "deep text content preserves document order");
}

void deep_dirty_flags_clear_iteratively() {
    auto document = make_element("document");
    Node* current = document.get();
    for (int depth = 0; depth < 4096; ++depth) {
        current = &current->append_child(make_element("div"));
    }
    clear_dirty_flags(*document);

    current->set_attribute("class", "changed");
    check(subtree_dirty_flags(*document) != DomDirtyNone, "deep mutation marks root dirty");
    clear_dirty_flags(*document);
    check(subtree_dirty_flags(*document) == DomDirtyNone, "deep dirty flags clear without recursion");
}

void dom_statistics_are_iterative_and_budget_oriented() {
    auto document = make_element("document");
    document->attributes["data-root"] = "1";
    Node& body = document->append_child(make_element("body"));
    body.attributes["class"] = "app";
    Node& paragraph = body.append_child(make_element("p"));
    paragraph.attributes["id"] = "intro";
    paragraph.append_child(make_text("Hello"));

    const DomStatistics statistics = compute_dom_statistics(*document);
    check(statistics.node_count == 4, "dom statistics node count");
    check(statistics.element_count == 3, "dom statistics element count");
    check(statistics.text_count == 1, "dom statistics text count");
    check(statistics.attribute_count == 3, "dom statistics attribute count");
    check(statistics.max_depth == 4, "dom statistics max depth");
    check(statistics.max_child_count == 1, "dom statistics max child count");
    check(statistics.max_attributes_per_element == 1, "dom statistics max attributes");
    check(statistics.string_bytes == 43, "dom statistics include tag, text and attribute bytes");
}

} // namespace

int main() {
    try {
        parser_returns_clean_document();
        compact_attribute_list_behaves_like_small_map();
        append_and_remove_mark_tree_dirty();
        insert_rejects_null_and_cycle_forming_nodes();
        attributes_and_text_mark_specific_dirty_bits();
        unchanged_text_content_stays_clean();
        text_content_replacement_detaches_nodes_before_destroy_observers();
        protected_lifetime_observers_survive_regular_replacement_and_removal();
        lifetime_registration_detaches_before_node_and_during_callback();
        lifetime_callbacks_cannot_rearm_a_destroying_node();
        lifetime_callback_can_release_its_own_registration();
        destruction_drain_handles_observer_mutation_in_place();
        deep_subtree_replacement_and_teardown_are_iterative();
        deep_text_content_is_iterative_and_ordered();
        deep_dirty_flags_clear_iteratively();
        dom_statistics_are_iterative_and_budget_oriented();
    } catch (const std::exception& error) {
        std::cerr << "dom mutation test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "dom mutation tests passed\n";
    return 0;
}
