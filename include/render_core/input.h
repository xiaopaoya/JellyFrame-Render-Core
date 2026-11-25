#pragma once

#include "render_core/event.h"
#include "render_core/hit_test.h"
#include "render_core/layer_tree.h"

#include <cstddef>
#include <string>
#include <vector>

#if JELLYFRAME_RENDER_CORE_CONTEXTUAL_LAYOUT_ENABLED
#define JELLYFRAME_CONTEXTUAL_INPUT_CONTROLLER_API_VERSION 1
#endif

namespace jellyframe {

enum class PointerButton {
    None = -1,
    Primary = 0,
    Middle = 1,
    Secondary = 2,
};

struct InputModifiers {
    bool alt = false;
    bool ctrl = false;
    bool meta = false;
    bool shift = false;
};

struct PointerInput {
    int x = 0;
    int y = 0;
    PointerButton button = PointerButton::None;
    int buttons = 0;
    InputModifiers modifiers;
};

struct WheelInput {
    int x = 0;
    int y = 0;
    int delta_x = 0;
    int delta_y = 0;
    InputModifiers modifiers;
};

enum class KeyCode {
    Unknown,
    Backspace,
    Enter,
    Space,
    Tab,
    ArrowUp,
    ArrowDown,
};

struct KeyInput {
    KeyCode code = KeyCode::Unknown;
    InputModifiers modifiers;
};

struct InteractionInvalidationOptions {
    bool hover_style = true;
    bool active_style = true;
    bool focus_style = true;
};

class InputController {
public:
    explicit InputController(const LayerNode& layer_tree,
                             InteractionInvalidationOptions invalidation_options = {},
                             bool dispatch_initial_autofocus = true);
    ~InputController();

    const Node* hovered_node() const;
    const Node* active_node() const;
    const Node* focused_node() const;
    const Node* modal_root() const;
    void set_focused_node(const Node* node);
    void set_interaction_state(const Node* hovered_node, const Node* active_node, const Node* focused_node);
    // Hosts opt in only while one dialog is modal. All input remains in this subtree.
    void set_modal_root(const Node* node);

    const Node* pointer_move(const PointerInput& input);
    const Node* pointer_down(const PointerInput& input);
    const Node* pointer_up(const PointerInput& input);
    const Node* wheel(const WheelInput& input);
#if JELLYFRAME_RENDER_CORE_CONTEXTUAL_LAYOUT_ENABLED
    // Dispatch against the live snapshot whose layer the controller uses.
    // The caller owns presentation identity and keeps the snapshot alive.
    const Node* pointer_move(const PointerInput& input, const contextual_text::LayerCapture& capture);
    const Node* pointer_down(const PointerInput& input, const contextual_text::LayerCapture& capture);
    const Node* pointer_up(const PointerInput& input, const contextual_text::LayerCapture& capture);
    const Node* wheel(const WheelInput& input, const contextual_text::LayerCapture& capture);
    void set_interaction_state(const Node* hovered_node, const Node* active_node,
                               const Node* focused_node, const contextual_text::LayerCapture& capture);
#endif
    bool text_input(const std::string& utf8_text);
    bool key_down(const KeyInput& input);
    // Apply a deterministic semantic form action while preserving the same
    // input/change listener ordering as a user interaction.
    bool set_control_value(Node& node, std::string value);
    bool set_control_checked(Node& node, bool checked);
    bool set_control_selected_index(Node& node, int selected_index);
    const Node* focus_next();
    const Node* focus_previous();
    bool activate_focused();
    void clear_pointer_state();

private:
    const LayerNode& layer_tree_;
    HitTester hit_tester_;
    InteractionInvalidationOptions invalidation_options_;
    const Node* hovered_node_ = nullptr;
    const Node* active_node_ = nullptr;
    const Node* focused_node_ = nullptr;
    const Node* modal_root_ = nullptr;
    const LayoutBox* active_box_ = nullptr;

    HitTestResult hit(int x, int y) const;
    HitTestResult admit_hit(HitTestResult result) const;
    const Node* move_at(const PointerInput& input, HitTestResult result);
    const Node* down_at(const PointerInput& input, HitTestResult result);
    const Node* up_at(const PointerInput& input, const Node* target);
    const Node* wheel_at(const WheelInput& input, const Node* target);
#if JELLYFRAME_RENDER_CORE_CONTEXTUAL_LAYOUT_ENABLED
    bool accepts_capture(const contextual_text::LayerCapture& capture) const;
#endif
    void restore_interaction_state(const Node* hovered_node, const Node* active_node,
                                   const Node* focused_node, const LayoutBox* active_box);
    bool contains_node(const Node* node) const;
    bool visible_node(const Node* node) const;
    bool accepts_node(const Node* node) const;
    const LayoutBox* focus_root_box() const;
    void set_hovered_node(const Node* node);
    void set_active_node(const Node* node, const LayoutBox* box = nullptr);
    MouseEvent make_mouse_event(const char* type, const PointerInput& input) const;
    WheelEvent make_wheel_event(const WheelInput& input) const;
    void dispatch_mouse_event(const Node* target, MouseEvent& event) const;
    bool dispatch_simple_event(const Node* target, const char* type) const;
    void update_hover(const Node* next_hover, const PointerInput& input);
    static void observed_node_destroyed(Node& node, void* context);
    void observe_node(const Node* node);
    void remove_observers_from(std::size_t first) noexcept;
    bool observes_node(const Node* node) const;
    void unobserve_unused_nodes();
    std::vector<Node*> observed_nodes_;
};

} // namespace jellyframe
