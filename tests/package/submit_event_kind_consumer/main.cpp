#include <render_core/dom.h>
#include <render_core/feature_config.h>
#include <render_core/form_submission.h>
#include <utility>

int main() {
    using namespace jellyframe;
    static_assert(JELLYFRAME_EVENT_SUBMIT_KIND_API == 1, "Submit kind API required");
    static_assert(static_cast<int>(EventKind::Generic) == 0);
    static_assert(static_cast<int>(EventKind::Mouse) == 1);
    static_assert(static_cast<int>(EventKind::Wheel) == 2);
    auto form = make_element("form");
    auto button = make_element("button");
    Node* submitter = button.get();
    form->append_child(std::move(button));
    SubmitEvent native(submitter);
    const Event& event = native;
    Event generic("submit");
    if (event.kind() != EventKind::Submit || generic.kind() != EventKind::Generic ||
        static_cast<const SubmitEvent&>(event).submitter() != submitter) return 1;
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
    int callbacks = 0;
    bool matched = false;
    form->add_event_listener("submit", [&](Event& dispatched) {
        ++callbacks;
        matched = dispatched.kind() == EventKind::Submit &&
            static_cast<const SubmitEvent&>(dispatched).submitter() == submitter;
        dispatched.prevent_default();
    });
    const auto result = request_form_submit(*form, submitter);
    if (callbacks != 1 || !matched || !result.default_prevented || result.submitted) return 2;
#endif
}
