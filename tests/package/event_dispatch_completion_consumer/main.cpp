#include <render_core/dom.h>
#include <render_core/event.h>

#include <cstdlib>

#if !defined(JELLYFRAME_EVENT_DISPATCH_COMPLETION_API) || JELLYFRAME_EVENT_DISPATCH_COMPLETION_API != 1
#error "Installed Core does not expose dispatch completion API v1"
#endif

namespace {
void complete(jellyframe::Event& event, void* context) noexcept {
    auto* calls = static_cast<int*>(context);
    ++*calls;
    if (event.current_target() != nullptr || event.event_phase() != jellyframe::EventPhase::None) {
        std::abort();
    }
}
}

int main() {
    using namespace jellyframe;
    auto target = make_element("button");
    EventDispatchCompletion completion;
    int calls = 0;
    target->add_event_listener("probe", [&](Event& event) {
        if (!attach_event_dispatch_completion(event, completion, complete, &calls)) {
            std::abort();
        }
    });
    Event event("probe");
    if (!dispatch_event(*target, event) || calls != 1) {
        return 1;
    }
    return 0;
}
