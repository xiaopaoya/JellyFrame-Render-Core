#include "render_core/dom.h"
#include "render_core/event.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Node& append_element(Node& parent, const char* tag) {
    return parent.append_child(make_element(tag));
}

void dispatch_runs_capture_target_and_bubble() {
    auto document = make_element("document");
    Node& html = append_element(*document, "html");
    Node& body = append_element(html, "body");
    Node& button = append_element(body, "button");

    std::vector<std::string> order;
    document->add_event_listener("click", [&](Event& event) {
        check(event.event_phase() == EventPhase::Capturing, "document capture phase");
        order.push_back("document-capture");
    }, EventListenerOptions{true, false});
    body.add_event_listener("click", [&](Event& event) {
        check(event.event_phase() == EventPhase::Capturing, "body capture phase");
        order.push_back("body-capture");
    }, EventListenerOptions{true, false});
    button.add_event_listener("click", [&](Event& event) {
        check(event.event_phase() == EventPhase::AtTarget, "button target capture phase");
        order.push_back("button-capture");
    }, EventListenerOptions{true, false});
    button.add_event_listener("click", [&](Event& event) {
        check(event.event_phase() == EventPhase::AtTarget, "button target bubble phase");
        order.push_back("button-bubble");
    });
    body.add_event_listener("click", [&](Event& event) {
        check(event.event_phase() == EventPhase::Bubbling, "body bubble phase");
        order.push_back("body-bubble");
    });

    MouseEvent event("click", 10, 12);
    const bool allowed = dispatch_event(button, event);

    check(allowed, "click default allowed");
    check(event.target() == &button, "event target preserved");
    check(order.size() == 5, "event listener count");
    check(order[0] == "document-capture", "capture order root first");
    check(order[1] == "body-capture", "capture order parent");
    check(order[2] == "button-capture", "target capture before target bubble");
    check(order[3] == "button-bubble", "target bubble");
    check(order[4] == "body-bubble", "bubble parent");
}

void prevent_default_stop_and_once_work() {
    auto document = make_element("document");
    Node& body = append_element(*document, "body");
    Node& button = append_element(body, "button");

    int once_count = 0;
    int body_bubble_count = 0;
    button.add_event_listener("click", [&](Event& event) {
        ++once_count;
        event.prevent_default();
        event.stop_propagation();
    }, EventListenerOptions{false, true});
    body.add_event_listener("click", [&](Event&) {
        ++body_bubble_count;
    });

    MouseEvent first("click", 1, 2);
    const bool first_allowed = dispatch_event(button, first);
    MouseEvent second("click", 1, 2);
    const bool second_allowed = dispatch_event(button, second);

    check(!first_allowed, "prevent default affects dispatch return");
    check(second_allowed, "once listener removed before second dispatch");
    check(once_count == 1, "once listener called once");
    check(body_bubble_count == 1, "stop propagation only affected first dispatch");
}

void once_listener_is_not_reentrant() {
    auto button = make_element("button");
    int calls = 0;
    button->add_event_listener("click", [&](Event&) {
        ++calls;
        MouseEvent nested("click", 0, 0);
        dispatch_event(*button, nested);
    }, EventListenerOptions{false, true});

    MouseEvent outer("click", 0, 0);
    dispatch_event(*button, outer);

    check(calls == 1, "once listener does not reenter before removal");
    check(button->event_listener_count() == 0, "once listener is removed after its outer callback");
}

void listener_mutation_during_dispatch_is_stable() {
    auto document = make_element("document");
    Node& button = append_element(*document, "button");

    int first_count = 0;
    int added_count = 0;
    EventTarget::ListenerId removable_id = 0;

    button.add_event_listener("click", [&](Event&) {
        ++first_count;
        button.add_event_listener("click", [&](Event&) {
            ++added_count;
        });
        if (first_count == 1) {
            check(button.remove_event_listener(removable_id), "listener removed during dispatch");
        }
    });
    removable_id = button.add_event_listener("click", [&](Event&) {
        throw std::runtime_error("removed listener should not run in the same dispatch");
    });

    MouseEvent first("click", 1, 2);
    dispatch_event(button, first);
    check(first_count == 1, "first listener ran once");
    check(added_count == 0, "new listener waits until the next dispatch");

    MouseEvent second("click", 1, 2);
    dispatch_event(button, second);
    check(first_count == 2, "existing listener still runs on second dispatch");
    check(added_count == 1, "added listener runs on later dispatch");
}

void bounded_cpp_listener_registration_respects_limit() {
    auto button = make_element("button");
    int count = 0;
    const EventTarget::ListenerId first =
        button->add_event_listener_bounded("click", [&](Event&) { ++count; }, 1);
    const EventTarget::ListenerId second =
        button->add_event_listener_bounded("click", [&](Event&) { count += 10; }, 1);

    check(first != 0, "first bounded listener registered");
    check(second == 0, "second bounded listener rejected");
    check(button->event_listener_count() == 1, "bounded listener count");
    check(button->remove_event_listener(first), "returned bounded listener id removes listener");
    check(button->event_listener_count() == 0, "removed bounded listener count");

    const EventTarget::ListenerId third =
        button->add_event_listener_bounded("click", [&](Event&) { ++count; }, 1);
    check(third != 0 && third != first, "bounded listener ids remain stable after removal");

    MouseEvent event("click", 0, 0);
    dispatch_event(*button, event);
    check(count == 1, "only registered bounded listener runs");
}

void dispatch_stops_safely_when_a_listener_destroys_its_target() {
    auto document = make_element("document");
    Node& parent = append_element(*document, "div");
    Node& child = append_element(parent, "button");
    int child_calls = 0;
    int parent_calls = 0;
    child.add_event_listener("click", [&](Event&) {
        ++child_calls;
        parent.set_text_content("replaced");
    });
    child.add_event_listener("click", [&](Event&) { ++child_calls; });
    parent.add_event_listener("click", [&](Event&) { ++parent_calls; });

    MouseEvent event("click", 0, 0);
    check(dispatch_event(child, event), "destroying listener preserves dispatch result");
    check(event.target_destroyed(), "destroyed target is reported without exposing a dangling target");
    check(event.target() == nullptr, "destroyed target is cleared from the event snapshot");
    check(child_calls == 1, "destroyed target does not invoke later listeners");
    check(parent_calls == 0, "destroyed path does not bubble");
}

void dispatch_continues_when_a_listener_destroys_an_unrelated_node() {
    auto document = make_element("document");
    Node& parent = append_element(*document, "div");
    Node& target = append_element(parent, "button");
    Node& unrelated = append_element(parent, "span");
    int target_calls = 0;
    int parent_calls = 0;
    target.add_event_listener("click", [&](Event&) {
        ++target_calls;
        check(parent.remove_child(unrelated), "unrelated child is removed");
    });
    target.add_event_listener("click", [&](Event&) { ++target_calls; });
    parent.add_event_listener("click", [&](Event&) { ++parent_calls; });

    MouseEvent event("click", 0, 0);
    check(dispatch_event(target, event), "unrelated destruction preserves dispatch result");
    check(target_calls == 2, "unrelated destruction keeps target listeners running");
    check(parent_calls == 1, "unrelated destruction keeps bubbling path running");
}

struct CompletionProbe {
    int calls = 0;
    Event* expected_event = nullptr;
    const Node* expected_current_target = nullptr;
    int order = 0;
    int* sequence = nullptr;
    bool check_final_state = false;
    bool expected_default_prevented = false;
    bool expected_target_destroyed = false;
};

void complete_probe(Event& event, void* context) noexcept {
    auto& probe = *static_cast<CompletionProbe*>(context);
    ++probe.calls;
    if (probe.expected_event != nullptr && &event != probe.expected_event) {
        std::terminate();
    }
    if (event.current_target() != probe.expected_current_target || event.event_phase() != EventPhase::None) {
        std::terminate();
    }
    if (probe.check_final_state &&
        (event.default_prevented() != probe.expected_default_prevented ||
         event.target_destroyed() != probe.expected_target_destroyed)) {
        std::terminate();
    }
    if (probe.sequence != nullptr) {
        probe.order = ++*probe.sequence;
    }
}

void complete_and_release_registration(Event&, void* context) noexcept {
    auto* registration = static_cast<std::unique_ptr<EventDispatchCompletion>*>(context);
    registration->reset();
}

struct ClosingDispatchProbe {
    const Node* target = nullptr;
    Event* original = nullptr;
    EventDispatchCompletion* closing_registration = nullptr;
    int same_event_listener_calls = 0;
    int distinct_event_listener_calls = 0;
    int attempt_callback_calls = 0;
    int verifier_callback_calls = 0;
    bool same_event_dispatch_result = true;
    bool distinct_event_dispatch_result = false;
    bool closing_enrollment_result = true;
    bool verifier_saw_final_default_prevented = false;
};

void verify_original_after_closing_dispatch_attempt(Event& event, void* context) noexcept;

void attempt_dispatch_while_closing(Event&, void* context) noexcept {
    auto& probe = *static_cast<ClosingDispatchProbe*>(context);
    ++probe.attempt_callback_calls;
    probe.closing_enrollment_result = attach_event_dispatch_completion(
        *probe.original, *probe.closing_registration,
        verify_original_after_closing_dispatch_attempt, &probe);
    probe.same_event_dispatch_result = dispatch_event(*probe.target, *probe.original);
    Event distinct("distinct-from-completion");
    probe.distinct_event_dispatch_result = dispatch_event(*probe.target, distinct);
}

void verify_original_after_closing_dispatch_attempt(Event& event, void* context) noexcept {
    auto& probe = *static_cast<ClosingDispatchProbe*>(context);
    ++probe.verifier_callback_calls;
    probe.verifier_saw_final_default_prevented = event.default_prevented();
}

void completion_enrollment_covers_exit_paths_and_nested_dispatch() {
    auto root = make_element("root");
    Node& parent = append_element(*root, "parent");
    Node& target = append_element(parent, "target");
    EventDispatchCompletion first;
    EventDispatchCompletion second;
    CompletionProbe first_probe;
    CompletionProbe second_probe;
    int sequence = 0;
    first_probe.sequence = &sequence;
    second_probe.sequence = &sequence;

    target.add_event_listener("normal", [&](Event& event) {
        first_probe.expected_event = &event;
        check(attach_event_dispatch_completion(event, first, complete_probe, &first_probe), "first completion enrolled");
        check(attach_event_dispatch_completion(event, second, complete_probe, &second_probe), "second completion enrolled");
        check(!attach_event_dispatch_completion(event, first, complete_probe, &first_probe), "armed completion rejected");
        check(!dispatch_event(target, event), "same Event recursive dispatch refused");
        check(event.current_target() == &target, "refused recursive dispatch preserves outer currentTarget");
        Event nested("nested");
        check(dispatch_event(target, nested), "different Event nested dispatch succeeds");
    });
    Event normal("normal");
    check(!attach_event_dispatch_completion(normal, first, complete_probe, &first_probe), "inactive Event enrollment refused");
    check(dispatch_event(target, normal), "normal dispatch succeeds");
    check(first_probe.calls == 1 && second_probe.calls == 1, "normal completion called exactly once");
    check(first_probe.order == 2 && second_probe.order == 1, "completion order is LIFO");

    EventDispatchCompletion stopped;
    CompletionProbe stopped_probe;
    stopped_probe.check_final_state = true;
    stopped_probe.expected_default_prevented = true;
    target.add_event_listener("stop", [&](Event& event) {
        check(attach_event_dispatch_completion(event, stopped, complete_probe, &stopped_probe), "stop completion enrolled");
        event.prevent_default();
        event.stop_immediate_propagation();
    });
    Event stop("stop");
    check(!dispatch_event(target, stop), "short-circuit dispatch preserves preventDefault result");
    check(stopped_probe.calls == 1, "short-circuit completion called once");

    EventDispatchCompletion throwing;
    CompletionProbe throwing_probe;
    int throwing_calls = 0;
    target.add_event_listener("throw", [&](Event& event) {
        ++throwing_calls;
        check(attach_event_dispatch_completion(event, throwing, complete_probe, &throwing_probe), "exception completion enrolled");
        throw std::runtime_error("listener failure");
    }, EventListenerOptions{false, true});
    Event thrown("throw");
    bool propagated = false;
    try {
        dispatch_event(target, thrown);
    } catch (const std::runtime_error&) {
        propagated = true;
    }
    check(propagated, "native listener exception propagates");
    check(throwing_probe.calls == 1, "exception completion called once");
    check(thrown.current_target() == nullptr && thrown.event_phase() == EventPhase::None, "exception clears transient event state");
    Event after_throw("throw");
    check(dispatch_event(target, after_throw), "dispatch after once exception succeeds");
    check(throwing_calls == 1, "throwing once listener is removed");
}

void completion_detach_and_destroyed_current_target_are_safe() {
    auto root = make_element("root");
    Node& parent = append_element(*root, "parent");
    Node& target = append_element(parent, "target");
    EventDispatchCompletion pending;
    EventDispatchCompletion detached;
    CompletionProbe pending_probe;
    CompletionProbe detached_probe;
    target.add_event_listener("detach", [&](Event& event) {
        check(attach_event_dispatch_completion(event, detached, complete_probe, &detached_probe), "pending completion enrolled");
        check(attach_event_dispatch_completion(event, pending, complete_probe, &pending_probe), "detached completion enrolled");
        detach_event_dispatch_completion(detached);
    });
    Event detach("detach");
    dispatch_event(target, detach);
    check(detached_probe.calls == 0 && pending_probe.calls == 1, "detached registration is skipped");

    CompletionProbe destroyed_probe;
    destroyed_probe.check_final_state = true;
    destroyed_probe.expected_target_destroyed = true;
    EventDispatchCompletion destroyed_completion;
    parent.add_event_listener("destroy-current", [&](Event& event) {
        check(attach_event_dispatch_completion(event, destroyed_completion, complete_probe, &destroyed_probe), "destroy completion enrolled");
        check(root->remove_child(parent), "currentTarget parent removed during callback");
        check(event.current_target() == nullptr, "destroyed currentTarget clears immediately");
        check(event.event_phase() == EventPhase::None, "destroyed currentTarget phase clears immediately");
    }, EventListenerOptions{true, false});
    Event destroyed("destroy-current");
    dispatch_event(target, destroyed);
    check(destroyed_probe.calls == 1, "completion runs after path node destruction");
    check(destroyed.current_target() == nullptr, "destroyed path leaves no currentTarget");
}

void completion_callback_can_release_its_own_registration() {
    auto target = make_element("target");
    auto completion = std::make_unique<EventDispatchCompletion>();
    target->add_event_listener("self-release", [&](Event& event) {
        check(attach_event_dispatch_completion(event, *completion,
                                              complete_and_release_registration, &completion),
              "self-release completion enrolled");
    });
    Event event("self-release");
    check(dispatch_event(*target, event), "self-release dispatch succeeds");
    check(completion == nullptr, "completion callback releases its own detached registration");
}

void closing_completion_rejects_same_event_and_allows_distinct_event() {
    auto target = make_element("target");
    ClosingDispatchProbe probe;
    probe.target = target.get();
    EventDispatchCompletion verifier;
    EventDispatchCompletion attempt;
    probe.closing_registration = &attempt;
    target->add_event_listener("closing-guard", [&](Event& event) {
        ++probe.same_event_listener_calls;
        if (probe.same_event_listener_calls == 1) {
            probe.original = &event;
            event.prevent_default();
            check(attach_event_dispatch_completion(event, verifier,
                                                  verify_original_after_closing_dispatch_attempt, &probe),
                  "closing verifier completion enrolled");
            check(attach_event_dispatch_completion(event, attempt, attempt_dispatch_while_closing, &probe),
                  "closing redispatch attempt completion enrolled");
        }
    });
    target->add_event_listener("distinct-from-completion", [&](Event&) {
        ++probe.distinct_event_listener_calls;
    });

    Event original("closing-guard");
    probe.original = &original;
    check(!dispatch_event(*target, original), "original dispatch retains default prevention");
    check(probe.attempt_callback_calls == 1 && probe.verifier_callback_calls == 1,
          "both completion callbacks run exactly once");
    check(probe.same_event_listener_calls == 1 && !probe.same_event_dispatch_result,
          "same-Event dispatch is refused while completions drain");
    check(!probe.closing_enrollment_result, "completion enrollment into a closing scope is refused");
    check(probe.verifier_saw_final_default_prevented && original.default_prevented(),
          "refused same-Event dispatch preserves final state for later completion");
    check(probe.distinct_event_dispatch_result && probe.distinct_event_listener_calls == 1,
          "distinct Event dispatch from completion succeeds");
}

struct FailureCompletionProbe {
    int calls = 0;
    bool saw_failure = false;
    bool saw_final_state = false;
    bool closing_failure_result = true;
};

void observe_dispatch_failure(Event& event, void* context) noexcept {
    auto& probe = *static_cast<FailureCompletionProbe*>(context);
    ++probe.calls;
    probe.saw_failure = event.dispatch_failed();
    probe.saw_final_state = event.current_target() == nullptr &&
        event.event_phase() == EventPhase::None && !event.default_prevented();
    probe.closing_failure_result = fail_event_dispatch(event);
}

void native_failure_stops_every_phase_without_default_prevention() {
    for (bool cancelable : {false, true}) {
        for (int failing_phase = 0; failing_phase != 4; ++failing_phase) {
            auto root = make_element("root");
            Node& target = append_element(*root, "button");
            Event event("native-failure", true, cancelable);
            check(!fail_event_dispatch(event) && !event.dispatch_failed(),
                  "inactive failure request leaves Event unchanged");
            EventDispatchCompletion completion;
            FailureCompletionProbe probe;
            std::vector<int> observed;
            bool inject_failure = true;
            auto invoke = [&](int phase, Event& current) {
                observed.push_back(phase);
                if (phase == 0) {
                    check(attach_event_dispatch_completion(current, completion,
                          observe_dispatch_failure, &probe), "failure completion enrolled");
                }
                if (inject_failure && phase == failing_phase) {
                    check(fail_event_dispatch(current), "native failure admitted during active phase");
                    check(fail_event_dispatch(current), "repeated failure is idempotent");
                }
            };
            root->add_event_listener(event.type(), [&](Event& current) { invoke(0, current); }, {true, false});
            target.add_event_listener(event.type(), [&](Event& current) { invoke(1, current); }, {true, false});
            target.add_event_listener(event.type(), [&](Event& current) { invoke(2, current); });
            root->add_event_listener(event.type(), [&](Event& current) { invoke(3, current); });
            root->add_event_listener(event.type(), [&](Event&) { observed.push_back(4); });
            check(!dispatch_event(target, event), "native failure returns false even for noncancelable Event");
            check(observed.size() == static_cast<std::size_t>(failing_phase + 1),
                  "native failure stops target, bubble and later same-node callbacks");
            check(event.dispatch_failed() && !event.default_prevented(),
                  "native failure has distinct snapshot from default prevention");
            check(probe.calls == 1 && probe.saw_failure && probe.saw_final_state &&
                  !probe.closing_failure_result, "completion observes finalized immutable failure");
            check(!fail_event_dispatch(event), "completed Event failure request is refused");
            inject_failure = false;
            observed.clear();
            check(dispatch_event(target, event) && !event.dispatch_failed(),
                  "normal redispatch resets prior native failure");
            check(observed.size() == 5 && probe.calls == 2 && !probe.saw_failure &&
                  !probe.closing_failure_result, "normal completion cannot turn success into failure");
        }
    }
}

void nested_failure_and_destroyed_target_keep_independent_outcomes() {
    auto target = make_element("target");
    target->add_event_listener("inner", [](Event& event) {
        check(fail_event_dispatch(event), "nested failure admitted");
    });
    target->add_event_listener("outer", [&](Event& event) {
        Event inner("inner", true, false);
        check(!dispatch_event(*target, inner) && inner.dispatch_failed(), "nested noncancelable failure returned");
        check(!event.dispatch_failed(), "inner failure does not poison outer Event");
    });
    Event outer("outer");
    check(dispatch_event(*target, outer), "outer dispatch succeeds after inner failure");

    target->add_event_listener("destroy", [&](Event& event) {
        check(fail_event_dispatch(event), "failure recorded before target destruction");
        target.reset();
    });
    Event destroyed("destroy", true, false);
    check(!dispatch_event(*target, destroyed) && destroyed.dispatch_failed() &&
          destroyed.target_destroyed(), "destroyed-path early return preserves native failure");
}

void throwing_once_cleanup_preserves_listener_failure_and_idle_state() {
    for (bool listener_throws : {false, true}) {
        auto target = make_element("button");
        int cleanup_calls = 0;
        EventListenerOptions options;
        options.once = true;
        options.cleanup_context = &cleanup_calls;
        options.cleanup = [](void* context) {
            ++*static_cast<int*>(context);
            throw std::runtime_error("cleanup failure");
        };
        target->add_event_listener("cleanup-throws", [&](Event&) {
            if (listener_throws) throw std::runtime_error("listener failure");
        }, options);
        Event event("cleanup-throws");
        bool propagated_expected = false;
        try { dispatch_event(*target, event); }
        catch (const std::runtime_error& error) {
            propagated_expected = std::string(error.what()) ==
                (listener_throws ? "listener failure" : "cleanup failure");
        }
        check(propagated_expected && cleanup_calls == 1,
              "unwind preserves original listener exception; normal cleanup propagates its own");
        check(event.current_target() == nullptr && event.event_phase() == EventPhase::None,
              "throwing once cleanup leaves finalized transient state");
        check(target->event_listener_count() == 0 && dispatch_event(*target, event) && cleanup_calls == 1,
              "throwing cleanup removes once listener exactly once and permits redispatch");
    }
}

} // namespace

int main() {
    try {
        dispatch_runs_capture_target_and_bubble();
        prevent_default_stop_and_once_work();
        once_listener_is_not_reentrant();
        listener_mutation_during_dispatch_is_stable();
        bounded_cpp_listener_registration_respects_limit();
        dispatch_stops_safely_when_a_listener_destroys_its_target();
        dispatch_continues_when_a_listener_destroys_an_unrelated_node();
        completion_enrollment_covers_exit_paths_and_nested_dispatch();
        completion_detach_and_destroyed_current_target_are_safe();
        completion_callback_can_release_its_own_registration();
        closing_completion_rejects_same_event_and_allows_distinct_event();
        native_failure_stops_every_phase_without_default_prevention();
        nested_failure_and_destroyed_target_keep_independent_outcomes();
        throwing_once_cleanup_preserves_listener_failure_and_idle_state();
    } catch (const std::exception& error) {
        std::cerr << "event test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "event tests passed\n";
    return 0;
}
