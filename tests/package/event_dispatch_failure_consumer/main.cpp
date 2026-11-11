#include <render_core/dom.h>
#include <render_core/event.h>

struct Probe { int completions = 0; bool failed = false; };
void complete(jellyframe::Event& event, void* context) noexcept {
    auto& probe = *static_cast<Probe*>(context);
    ++probe.completions;
    probe.failed = event.dispatch_failed() && !event.default_prevented() &&
        event.current_target() == nullptr && event.event_phase() == jellyframe::EventPhase::None;
}

int main() {
    using namespace jellyframe;
    static_assert(JELLYFRAME_EVENT_DISPATCH_FAILURE_API == 1, "failure API required");
    auto target = make_element("button");
    EventDispatchCompletion completion;
    Probe probe;
    bool injected = false;
    int later_callbacks = 0;
    target->add_event_listener("native", [&](Event& event) {
        injected = attach_event_dispatch_completion(event, completion, complete, &probe) &&
            fail_event_dispatch(event);
    }, {false, true});
    target->add_event_listener("native", [&](Event&) { ++later_callbacks; });
    Event event("native", true, false);
    if (dispatch_event(*target, event) || !injected || later_callbacks != 0 ||
        !probe.failed || probe.completions != 1) return 1;
    if (!dispatch_event(*target, event) || event.dispatch_failed() || later_callbacks != 1) return 2;
}
