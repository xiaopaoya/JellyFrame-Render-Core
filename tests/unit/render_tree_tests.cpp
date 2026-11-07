#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const RenderObject* find_first_by_tag(const RenderObject& object, const std::string& tag_name) {
    if (object.node != nullptr && object.node->type == NodeType::Element && object.node->tag_name == tag_name) {
        return &object;
    }
    for (const auto& child : object.children) {
        const RenderObject* found = find_first_by_tag(*child, tag_name);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

const RenderObject* find_first_text(const RenderObject& object) {
    if (object.type == RenderObjectType::Text) {
        return &object;
    }
    for (const auto& child : object.children) {
        const RenderObject* found = find_first_text(*child);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

bool render_text_exists(const RenderObject& object, const std::string& text) {
    if (object.type == RenderObjectType::Text && object.node != nullptr && object.node->text == text) {
        return true;
    }
    for (const auto& child : object.children) {
        if (render_text_exists(*child, text)) {
            return true;
        }
    }
    return false;
}

bool has_diagnostic_code(const VectorDiagnosticSink& sink, const std::string& code) {
    for (const Diagnostic& diagnostic : sink.diagnostics()) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

void render_tree_filters_non_rendered_nodes() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<!doctype html><head><title>x</title><script>1 < 2</script></head>"
        "<body><form id='search'><input><button>Go</button></form><template><button>Hidden</button></template></body>");
    StyleResolver resolver(css_parser.parse("#search { width: 320px; } input { width: 240px; }"));
    RenderTreeBuilder builder(resolver);
    auto render_tree = builder.build(*document);

    check(find_first_by_tag(*render_tree, "head") == nullptr, "head skipped");
    check(find_first_by_tag(*render_tree, "script") == nullptr, "script skipped");
    check(find_first_by_tag(*render_tree, "template") == nullptr, "template skipped");
    check(find_first_by_tag(*render_tree, "form") != nullptr, "form rendered");
    check(find_first_by_tag(*render_tree, "input") != nullptr, "input rendered");
    check(find_first_by_tag(*render_tree, "button") != nullptr, "button rendered");
}

void render_tree_carries_computed_style_and_text_inheritance() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><button class='primary'>Go</button></body>");
    StyleResolver resolver(css_parser.parse(".primary { color: #2563eb; padding: 8px; }"));
    RenderTreeBuilder builder(resolver);
    auto render_tree = builder.build(*document);

    const RenderObject* button = find_first_by_tag(*render_tree, "button");
    const RenderObject* text = find_first_text(*render_tree);
    check(button != nullptr, "button object exists");
    check(button->style.padding.top == 8, "button computed padding");
    check(text != nullptr, "text object exists");
    check(text->style.color.b == 0xeb, "text inherited color");
}

void html_dimension_attributes_are_low_priority_sizing_hints() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><img id='image' width='80' height='40'><video id='video' width='64' height='32'>"
        "</video><iframe id='frame' width='120'></iframe><div id='ordinary' width='90' height='45'></div>"
        "<img id='css' width='80' height='40'><img id='invalid' width='-1' height='24'>"
        "<img id='suffix' width='80px' height='24'></body>");
    StyleResolver resolver(css_parser.parse(
        "#css{width:12px;height:7px;aspect-ratio:3/2}"));
    auto render_tree = RenderTreeBuilder(resolver).build(*document);
    const auto find_by_id = [&](const char* id) {
        const RenderObject* result = nullptr;
        const auto visit = [&](const RenderObject& object, const auto& self) -> void {
            if (object.node != nullptr && object.node->attribute("id") == id) {
                result = &object;
                return;
            }
            for (const auto& child : object.children) self(*child, self);
        };
        visit(*render_tree, visit);
        return result;
    };
    const auto* image = find_by_id("image");
    const auto* video = find_by_id("video");
    const auto* frame = find_by_id("frame");
    const auto* ordinary = find_by_id("ordinary");
    const auto* css = find_by_id("css");
    const auto* invalid = find_by_id("invalid");
    const auto* suffix = find_by_id("suffix");
    check(image && image->style.width == 80 && image->style.height == 40 &&
              image->style.aspect_ratio_width == 2 && image->style.aspect_ratio_height == 1,
          "image dimension attributes set size and automatic ratio hints");
    check(video && video->style.width == 64 && video->style.height == 32 &&
              video->style.aspect_ratio_width == 2 && video->style.aspect_ratio_height == 1,
          "video dimension attributes set size and automatic ratio hints");
    check(frame && frame->style.width == 120 && frame->style.height == -1,
          "embedded content dimension attributes set independent size hints");
    check(ordinary && ordinary->style.width == -1 && ordinary->style.height == -1,
          "dimension attributes on ordinary elements are ignored");
    check(css && css->style.width == 12 && css->style.height == 7 &&
              css->style.aspect_ratio_width == 3000 && css->style.aspect_ratio_height == 2000,
          "author CSS overrides HTML dimension hints");
    check(invalid && invalid->style.width == -1 && invalid->style.height == 24,
          "invalid dimension hints are rejected independently");
    check(suffix && suffix->style.width == 80 && suffix->style.height == 24,
          "dimension attributes consume the HTML numeric prefix");

    auto fractional_document = html_parser.parse(
        "<body><img id='fraction' width='64.5' height='32.25'>"
        "<object id='percent' width='37.5%garbage'></object>"
        "<canvas id='canvas' width='+300.9' height='150.1'></canvas>"
        "<canvas id='negative' width='-1' height='5'></canvas>"
        "<img id='zero' width='0' height='5'>"
        "<img id='tiny' width='0.0001' height='0.0002'>"
        "<img id='large' width='2147483648' height='1073741824'>"
        "<img id='clear' width='80' height='40' style='width:initial;height:unset;aspect-ratio:auto'>"
        "<img id='priority' width='80' height='40' style='height:7px!important;height:9px'></body>");
    auto fractional_tree = RenderTreeBuilder(resolver).build(*fractional_document);
    const auto find_fractional = [&](const RenderObject& root, const char* id) {
        const RenderObject* result = nullptr;
        const auto visit = [&](const RenderObject& object, const auto& self) -> void {
            if (object.node != nullptr && object.node->attribute("id") == id) {
                result = &object;
                return;
            }
            for (const auto& child : object.children) self(*child, self);
        };
        visit(root, visit);
        return result;
    };
    const auto* fraction = find_fractional(*fractional_tree, "fraction");
    const auto* percent = find_fractional(*fractional_tree, "percent");
    const auto* canvas = find_fractional(*fractional_tree, "canvas");
    const auto* negative = find_fractional(*fractional_tree, "negative");
    const auto* zero = find_fractional(*fractional_tree, "zero");
    check(fraction && fraction->style.width == 65 && fraction->style.height == 32 &&
              fraction->style.aspect_ratio_width == 2 && fraction->style.aspect_ratio_height == 1,
          "fractional HTML dimensions round into ABI 3 scalar storage while preserving ratio");
    check(percent && percent->style.width == -1 && percent->style.width_percent == 38,
          "percentage HTML dimensions map to the ABI 3 percentage storage");
    check(canvas && canvas->style.aspect_ratio_width == 2 && canvas->style.aspect_ratio_height == 1 &&
              canvas->style.width == -1 && canvas->style.height == -1,
          "canvas dimensions contribute a ratio without becoming CSS sizes");
    check(negative && negative->style.aspect_ratio_width == 0 && negative->style.aspect_ratio_height == 0,
          "negative canvas dimensions are rejected");
    check(zero && zero->style.aspect_ratio_width == 0 && zero->style.aspect_ratio_height == 1000,
          "zero dimensions retain a representable degenerate ratio");
    const auto* tiny = find_fractional(*fractional_tree, "tiny");
    const auto* large = find_fractional(*fractional_tree, "large");
    const auto* clear = find_fractional(*fractional_tree, "clear");
    const auto* priority = find_fractional(*fractional_tree, "priority");
    check(tiny && tiny->style.aspect_ratio_width == 1 && tiny->style.aspect_ratio_height == 2,
          "tiny dimension ratios survive independent size rounding");
    check(large && large->style.aspect_ratio_width == 2 && large->style.aspect_ratio_height == 1,
          "large dimension pairs reduce before bounded ratio storage");
    check(clear && clear->style.width == -1 && clear->style.height == -1 &&
              clear->style.aspect_ratio_width == 0 && clear->style.aspect_ratio_height == 0,
          "initial unset and auto declarations clear presentational hints");
    check(priority && priority->style.height == 7,
          "inline important suffix beats a later normal declaration");

    auto layout_tree = LayoutEngine(resolver).layout(*render_tree, 320, 240);
    const auto find_layout_by_id = [&](const char* id) {
        const LayoutBox* result = nullptr;
        const auto visit = [&](const LayoutBox& box, const auto& self) -> void {
            if (box.node != nullptr && box.node->attribute("id") == id) {
                result = &box;
                return;
            }
            for (const auto& child : box.children) self(*child, self);
        };
        visit(*layout_tree, visit);
        return result;
    };
    const auto* image_layout = find_layout_by_id("image");
    check(image_layout && image_layout->rect.width == 80 && image_layout->rect.height == 40,
          "HTML dimensions flow through style resolution into layout geometry");
}

void closed_dialog_is_not_rendered_by_default() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><dialog><p>Hidden</p></dialog><dialog open><p>Shown</p></dialog></body>");
    StyleResolver resolver(css_parser.parse(""));
    RenderTreeBuilder builder(resolver);
    auto render_tree = builder.build(*document);

    const RenderObject* dialog = find_first_by_tag(*render_tree, "dialog");
    check(dialog != nullptr, "open dialog rendered");
    check(dialog->node != nullptr && dialog->node->attributes.find("open") != dialog->node->attributes.end(),
          "first rendered dialog is open");
}

void hidden_attribute_skips_rendering() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><p id='gone' hidden>Gone</p><p id='shown'>Shown</p></body>");
    StyleResolver resolver(css_parser.parse(""));
    RenderTreeBuilder builder(resolver);
    auto render_tree = builder.build(*document);

    check(find_first_by_tag(*render_tree, "p") != nullptr, "visible paragraph rendered");
    const RenderObject* text = find_first_text(*render_tree);
    check(text != nullptr && text->node != nullptr && text->node->text == "Shown", "hidden text skipped");
}

void closed_details_renders_only_first_summary() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><details><summary>More</summary><summary>Alt</summary><p>Hidden</p></details>"
        "<details open><summary>Open</summary><p>Shown</p></details></body>");
    StyleResolver resolver(css_parser.parse(""));
    RenderTreeBuilder builder(resolver);
    auto render_tree = builder.build(*document);

    check(render_text_exists(*render_tree, "More"), "closed details renders first summary");
    check(!render_text_exists(*render_tree, "Alt"), "closed details skips later summary");
    check(!render_text_exists(*render_tree, "Hidden"), "closed details skips content");
    check(render_text_exists(*render_tree, "Open"), "open details renders summary");
    check(render_text_exists(*render_tree, "Shown"), "open details renders content");
}

void formatting_whitespace_text_is_skipped() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body>\n  <main>\n    <button>Go</button>\n  </main>\n</body>");
    StyleResolver resolver(css_parser.parse(""));
    RenderTreeBuilder builder(resolver);
    auto render_tree = builder.build(*document);

    int text_count = 0;
    const auto count_text = [&](const RenderObject& object, const auto& self) -> void {
        if (object.type == RenderObjectType::Text) {
            ++text_count;
        }
        for (const auto& child : object.children) {
            self(*child, self);
        }
    };
    count_text(*render_tree, count_text);
    check(text_count == 1, "formatting whitespace text skipped");

    auto pre_document = html_parser.parse("<body><pre>\n  keep\n</pre></body>");
    auto pre_render_tree = builder.build(*pre_document);
    const RenderObject* pre_text = find_first_text(*pre_render_tree);
    check(pre_text != nullptr && pre_text->node != nullptr && pre_text->node->text.find("  keep") != std::string::npos,
          "pre whitespace text preserved");
}

void render_tree_respects_object_budget() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><p>A</p><p>B</p><p>C</p></main></body>");
    StyleResolver resolver(css_parser.parse(""));
    RenderTreeBuilder builder(resolver, RenderTreeOptions{4});
    auto render_tree = builder.build(*document);

    int count = 0;
    const auto count_objects = [&](const RenderObject& root, const auto& self) -> void {
        ++count;
        for (const auto& child : root.children) {
            self(*child, self);
        }
    };
    count_objects(*render_tree, count_objects);
    check(count == 4, "render tree is capped by object budget");
}

void render_tree_reports_object_budget_diagnostic() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><p>A</p><p>B</p><p>C</p></main></body>");
    StyleResolver resolver(css_parser.parse(""));
    VectorDiagnosticSink diagnostics;
    RenderTreeOptions options;
    options.max_render_objects = 4;
    options.diagnostics = &diagnostics;
    RenderTreeBuilder builder(resolver, options);
    auto render_tree = builder.build(*document);

    check(count_render_objects(*render_tree) == 4, "render budget still caps objects");
    check(has_diagnostic_code(diagnostics, "render-object-limit"), "render budget diagnostic is reported");
}

void render_tree_can_use_monotonic_arena() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><p>A</p><p>B</p></main></body>");
    StyleResolver resolver(css_parser.parse("p { color: #2563eb; }"));
    RenderTreeBuilder builder(resolver);
    MonotonicArena arena(256);
    auto render_tree = builder.build(*document, arena);

    check(render_tree != nullptr, "arena render tree root exists");
    check(arena.used_bytes() > 0, "arena render tree consumes arena storage");
    check(find_first_by_tag(*render_tree, "main") != nullptr, "arena render tree contains element");
    check(find_first_text(*render_tree) != nullptr, "arena render tree contains text");
}

} // namespace

int main() {
    try {
        render_tree_filters_non_rendered_nodes();
        render_tree_carries_computed_style_and_text_inheritance();
        html_dimension_attributes_are_low_priority_sizing_hints();
        closed_dialog_is_not_rendered_by_default();
        hidden_attribute_skips_rendering();
        closed_details_renders_only_first_summary();
        formatting_whitespace_text_is_skipped();
        render_tree_respects_object_budget();
        render_tree_reports_object_budget_diagnostic();
        render_tree_can_use_monotonic_arena();
    } catch (const std::exception& error) {
        std::cerr << "render tree test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "render tree tests passed\n";
    return 0;
}
