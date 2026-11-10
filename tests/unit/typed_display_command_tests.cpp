#include "render_core/geometry.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Exception, typename Operation>
void check_throws(Operation operation, const char* message) {
    try {
        operation();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message);
}

void check_defaults_and_type_changes() {
    DisplayCommand command;
    check(command.type() == DisplayCommandType::FillRect, "default command is a fill");
    check(command.basic_paint().stroke_width == 1, "basic stroke defaults to one");
    check(command.basic_paint().gradient_stop_percent == 100, "gradient stop defaults to full range");
    command.basic_paint().border_radius = 17;
    command.set_type(DisplayCommandType::FillRect);
    check(command.basic_paint().border_radius == 17, "same type preserves payload");
    command.set_type(DisplayCommandType::BoxShadow);
    check(command.basic_paint().border_radius == 0, "different basic type resets payload");

    command.set_type(DisplayCommandType::Text);
    check(command.text_paint().font_size == 14, "text font size default");
    check(command.text_paint().font_weight == 400, "text font weight default");
    check(command.text_paint().text_single_line, "text defaults to single line");
    command.text_paint().origin_x = 45;
    command.set_type(DisplayCommandType::PreparedText);
    check(command.text_paint().origin_x == 0, "changing text kind resets origin");

    command.set_type(DisplayCommandType::Image);
    check(command.image_paint().image_handle == 0, "image handle default");
    check(command.image_paint().object_position.x_percent == 50, "image position default");
    check_throws<std::invalid_argument>([&] {
        command.set_type(static_cast<DisplayCommandType>(255));
    }, "unknown command type must be rejected");
    check(command.type() == DisplayCommandType::Image, "invalid type preserves current kind");
    check(command.image_paint().object_position.x_percent == 50, "invalid type preserves payload");
}

void check_access_guards() {
    DisplayCommand command;
    check_throws<std::logic_error>([&] { command.text_paint(); }, "basic command rejects text access");
    check_throws<std::logic_error>([&] { command.image_paint(); }, "basic command rejects image access");
    check_throws<std::logic_error>([&] {
        set_prepared_text_origin(command, 1, 2);
    }, "basic command rejects prepared origin");
    command.set_type(DisplayCommandType::Image);
    const DisplayCommand& constant = command;
    check_throws<std::logic_error>([&] { constant.basic_paint(); }, "const image rejects basic access");
    check_throws<std::logic_error>([&] { constant.text_paint(); }, "const image rejects text access");
    command.set_type(DisplayCommandType::Text);
    check_throws<std::logic_error>([&] {
        prepared_text_origin_x(command);
    }, "plain text rejects prepared origin");
}

void check_value_copies() {
    DisplayCommand original(DisplayCommandType::Image);
    original.rect = {10, 20, 30, 40};
    original.text = std::string(100, 'x');
    original.trace_owner_token = 73;
    original.transform.enabled = true;
    original.transform.source_clip_index = 7;
    original.image_paint().image_handle = 91;

    DisplayCommand copied(original);
    check(display_commands_equal(original, copied), "copy construction preserves image value");
    DisplayCommand assigned(DisplayCommandType::Text);
    assigned = original;
    check(display_commands_equal(original, assigned), "copy assignment activates image payload");
    assigned = assigned;
    check(display_commands_equal(original, assigned), "self assignment preserves value");
    DisplayCommand moved(std::move(copied));
    check(display_commands_equal(original, moved), "move construction preserves value");
    DisplayCommand move_assigned;
    move_assigned = std::move(assigned);
    check(display_commands_equal(original, move_assigned), "move assignment preserves value");
    move_assigned.image_paint().object_position.x_percent = 9;
    check(!display_commands_equal(original, move_assigned), "equality includes active image position");
    check(original.image_paint().object_position.x_percent == 50, "copied image position is independent");
    moved.text[0] = 'y';
    check(original.text[0] == 'x', "copied string is owned independently");
}

void check_prepared_origin_and_translation() {
    DisplayCommand command(DisplayCommandType::PreparedText);
    command.rect = {10, 20, 30, 40};
    set_prepared_text_origin(command, 12, 35);
    DisplayCommand copied = command;
    check(display_commands_equal(command, copied), "copy preserves explicit text origin");
    check(translate_display_command(copied, -3, 6), "prepared translation succeeds");
    check(copied.rect.x == 7 && copied.rect.y == 26, "translation moves prepared bounds");
    check(prepared_text_origin_x(copied) == 9, "translation moves prepared x origin");
    check(prepared_text_baseline_y(copied) == 41, "translation moves prepared baseline");
    check(!display_commands_equal(command, copied), "equality observes translated command");
    set_prepared_text_origin(copied, std::numeric_limits<int>::max(), std::numeric_limits<int>::min());
    translate_display_command(copied, 1, -1);
    check(prepared_text_origin_x(copied) == std::numeric_limits<int>::max(), "origin saturates upward");
    check(prepared_text_baseline_y(copied) == std::numeric_limits<int>::min(), "baseline saturates downward");


}

} // namespace

int main() {
    check_defaults_and_type_changes();
    check_access_guards();
    check_value_copies();
    check_prepared_origin_and_translation();
    std::cout << "BasicPaint=" << sizeof(BasicPaint)
              << " TextPaint=" << sizeof(TextPaint)
              << " ImagePaint=" << sizeof(ImagePaint)
              << " DisplayCommand=" << sizeof(DisplayCommand) << '\n';
}
