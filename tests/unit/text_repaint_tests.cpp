#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "core_text_test_fixture.h"

#include <iostream>
#include <stdexcept>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

LayoutBox* find_text(LayoutBox& box) {
    if (box.node != nullptr && box.node->type == NodeType::Text) return &box;
    for (const auto& child : box.children)
        if (auto* found = find_text(*child)) return found;
    return nullptr;
}

void text_mutation_uses_fresh_core_layout() {
    test::CoreTextTestFixture fonts;
    check(fonts.ready(), "Core TextSystem fixture is ready");
    auto document = HtmlParser{}.parse("<body><p id='label'>AB</p></body>");
    StyleResolver resolver(CssParser{}.parse(
        "body{margin:0} p{display:block;width:80px;margin:0;font-size:16px;line-height:20px}"));
    LayoutEngineOptions options;
    options.text_system = &fonts.system();
    LayoutEngine engine(resolver, options);

    auto before = engine.layout(*document, 100, 40);
    auto* old_text = find_text(*before);
    check(old_text != nullptr && old_text->text_layout_cache.prepared_text &&
          old_text->text_layout_cache.lines == std::vector<std::string>{"AB"},
          "initial Core layout caches prepared text");
    const int old_width = old_text->rect.width;

    Node* label = nullptr;
    const auto find_label = [&](const auto& self, Node& node) -> void {
        if (node.attribute("id") == "label") label = &node;
        for (auto& child : node.children) self(self, *child);
    };
    find_label(find_label, *document);
    check(label != nullptr, "text owner exists");
    label->set_text_content("ABAB");

    auto after = engine.layout(*document, 100, 40);
    auto* new_text = find_text(*after);
    check(new_text != nullptr && new_text->text_layout_cache.prepared_text &&
          new_text->text_layout_cache.source_text == "ABAB" && new_text->rect.width != old_width,
          "text mutation is resolved by a fresh Core layout");
    check(new_text->text_layout_cache.text_system == &fonts.system() &&
          new_text->text_layout_cache.text_system_generation == fonts.system().generation(),
          "fresh text layout is bound to the current Core font generation");
}

} // namespace

int main() {
    try {
        text_mutation_uses_fresh_core_layout();
    } catch (const std::exception& error) {
        std::cerr << "text layout mutation test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "Core text mutation layout tests passed\n";
    return 0;
}
