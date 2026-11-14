#include "render_core/css_parser.h"
#include "render_core/hit_test.h"
#include "render_core/html_parser.h"
#include "render_core/layer_tree.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"
#include "../../src/style_resolver_test_hooks.h"

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

const RenderObject* find_first_by_id(const RenderObject& object, const std::string& id) {
    if (object.node != nullptr && object.node->type == NodeType::Element && object.node->attribute("id") == id) {
        return &object;
    }
    for (const auto& child : object.children) {
        if (const RenderObject* found = find_first_by_id(*child, id)) {
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

const LayoutBox* find_layout_box_for_node(const LayoutBox& box, const Node* node) {
    if (box.node == node) return &box;
    for (const auto& child : box.children) {
        if (const LayoutBox* found = find_layout_box_for_node(*child, node)) return found;
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
              css->style.aspect_ratio_width == 3 && css->style.aspect_ratio_height == 2 && !css->style.aspect_ratio_auto,
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
    check(zero && zero->style.aspect_ratio_width == 0 && zero->style.aspect_ratio_height == 1,
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

void display_contents_flattens_effective_children_and_preserves_inheritance() {
    auto document = HtmlParser{}.parse(
        "<body><main><section id='contents'><span>Text</span></section></main></body>");
    StyleResolver resolver(CssParser{}.parse(
        "@supports (display:contents){#contents{display:contents;color:#123456;"
        "font-size:23px;white-space:nowrap}}"));
    auto tree = RenderTreeBuilder(resolver).build(*document);
    const auto* main = find_first_by_tag(*tree, "main");
    const auto* contents = find_first_by_tag(*tree, "section");
    const auto* span = find_first_by_tag(*tree, "span");
    const auto* text = find_first_text(*tree);
    check(main && contents == nullptr && span && text, "contents element is omitted and descendants remain");
    check(main->children.size() == 1 && main->children.front()->node == span->node,
          "effective child is attached to the nearest rendered parent");
    check(span->style.color.r == 0x12 && span->style.font_size == 23 && span->style.white_space_nowrap,
          "contents style is retained as the inheritance parent");
    check(text->style.color.r == 0x12 && text->style.white_space_nowrap,
          "flattened text inherits through the contents element");
}

void display_contents_keeps_root_and_replaced_exceptions_and_pre_whitespace() {
    auto document = HtmlParser{}.parse(
        "<html><body><div id='pre'><pre style='display:contents'>\n  keep\n</pre></div>"
        "<picture style='display:contents'><img id='picture-image'></picture>"
        "<canvas style='display:contents'><span id='canvas-fallback'>Canvas fallback</span></canvas>"
        "<object style='display:contents'><span id='object-fallback'>Object fallback</span></object>"
        "<img id='image' style='display:contents'></body></html>");
    StyleResolver resolver(CssParser{}.parse("html{display:contents}"));
    auto tree = RenderTreeBuilder(resolver).build(*document);
    const auto* html = find_first_by_tag(*tree, "html");
    check(html && html->style.display == Display::Block, "document element retains root box semantics");
    check(find_first_by_id(*tree, "image") == nullptr, "replaced contents element is suppressed");
    check(find_first_by_tag(*tree, "picture") == nullptr && find_first_by_tag(*tree, "img") != nullptr,
          "picture contents retains its effective image child");
    check(find_first_by_tag(*tree, "canvas") == nullptr &&
              !render_text_exists(*tree, "Canvas fallback"),
          "canvas contents does not flatten fallback content");
    check(find_first_by_tag(*tree, "object") == nullptr &&
              !render_text_exists(*tree, "Object fallback"),
          "object contents does not flatten fallback content");
    check(render_text_exists(*tree, "\n  keep\n"), "preformatted whitespace survives contents flattening");
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

void inheritance_preserves_local_style_and_parent_values() {
    auto document = HtmlParser{}.parse(
        "<body><main><span>Inherited</span><b>Specified</b></main></body>");
    StyleResolver resolver(CssParser{}.parse(
        "main{visibility:hidden;color:#123456;font-size:23px;font-weight:700;"
        "line-height:31px;letter-spacing:2px;text-transform:uppercase;"
        "text-decoration:underline;white-space:nowrap;text-overflow:ellipsis;"
        "caption-side:bottom;border-collapse:collapse;border-spacing:7px 9px;aspect-ratio:3/2}"
        "span{aspect-ratio:inherit}"
        "b{visibility:visible;color:#abcdef;font-size:14px;font-weight:400;"
        "line-height:20px;letter-spacing:0;text-transform:none;white-space:normal}"));
    auto tree = RenderTreeBuilder(resolver).build(*document);
    const auto* parent = find_first_by_tag(*tree, "main");
    const auto* inherited = find_first_by_tag(*tree, "span");
    const auto* specified = find_first_by_tag(*tree, "b");
    check(parent && inherited && specified, "inheritance fixture creates every element");
    check(inherited->style.visibility_hidden && inherited->style.color.r == 0x12 &&
              inherited->style.font_size == 23 && inherited->style.font_weight == 700 &&
              inherited->style.line_height == 31 && inherited->style.letter_spacing == 2 &&
              inherited->style.text_transform == TextTransform::Uppercase &&
              inherited->style.text_decoration_underline && inherited->style.white_space_nowrap,
          "local style receives inherited text fields in place");
    check(!inherited->style.text_overflow_ellipsis &&
              inherited->style.aspect_ratio_width == 3 && inherited->style.aspect_ratio_height == 2 &&
              table_caption_side(inherited->style) == TableCaptionSide::Bottom &&
              table_border_is_collapsed(inherited->style) &&
              table_border_spacing_x(inherited->style) == 7 && table_border_spacing_y(inherited->style) == 9,
          "element finalization preserves ellipsis ratio and table inheritance");
    check(!specified->style.visibility_hidden && specified->style.color.r == 0xab &&
              specified->style.font_size == 14 && specified->style.font_weight == 400 &&
              specified->style.line_height == 20 && specified->style.letter_spacing == 0 &&
              specified->style.text_transform == TextTransform::None && !specified->style.white_space_nowrap,
          "explicit child defaults survive inheritance");
    const auto* text = find_first_text(*inherited);
    check(text && text->style.font_size == 23 && text->style.visibility_hidden &&
              parent->style.font_size == 23 && parent->style.text_overflow_ellipsis,
          "text inheritance preserves the owning parent style");
}

void separator_filter_preserves_context_overrides_and_budget() {
    auto document = HtmlParser{}.parse(
        "<body> \n<main dir='rtl' style='display:contents'>\t"
        "<span> leading </span> <i style='display:inline-block'> trailing </i>\n"
        "<wbr> <br><pre><span> \t\n</span></pre></main></body>");
    StyleResolver resolver(CssParser{}.parse(":root{--fg:#123456}main{color:var(--fg)}"));
    auto tree = RenderTreeBuilder(resolver).build(*document);
    const auto fast_text_calls = detail::test::text_style_fast_path_counters().calls;
    std::vector<StyleOverride> overrides(1);
    RenderTreeOptions fallback_options;
    fallback_options.style_overrides = &overrides;
    MonotonicArena arena(256);
    auto fallback = RenderTreeBuilder(resolver, fallback_options).build(*document, arena);
    const auto fallback_text_calls = detail::test::text_style_fast_path_counters().calls;
    check(count_render_objects(*tree) == count_render_objects(*fallback),
          "early separators preserve fallback object count with arena and overrides");
    check(render_text_exists(*tree, " leading ") && render_text_exists(*tree, " trailing ") &&
              render_text_exists(*tree, " \t\n") && find_first_by_tag(*tree, "wbr") &&
              find_first_by_tag(*tree, "br"),
          "leading trailing preserving and break sources survive early filtering");
    check(find_first_text(*tree)->style.color.r == 0x12 &&
              find_first_text(*fallback)->style.color.r == 0x12,
          "custom properties still reach nonseparator text on both paths");
    check(fallback_text_calls > fast_text_calls,
          "separator shortcut avoids resolving discarded text while overrides retain the fallback");

    auto budget_document = make_element("document");
    auto separator = std::make_unique<Node>(NodeType::Text);
    separator->text = " \t\n\r\f\v";
    budget_document->append_child(std::move(separator));
    VectorDiagnosticSink diagnostics;
    RenderTreeOptions budget_options;
    budget_options.max_render_objects = 1;
    budget_options.diagnostics = &diagnostics;
    auto bounded = RenderTreeBuilder(resolver, budget_options).build(*budget_document);
    check(count_render_objects(*bounded) == 1 && has_diagnostic_code(diagnostics, "render-object-limit"),
          "separator filtering preserves exhausted object budget diagnostics");
}

void text_style_fast_path_preserves_inheritance_and_overrides() {
    auto document = HtmlParser{}.parse(
        "<body><main style='--ink:#234567;color:var(--ink);font-size:22px'>"
        "Alpha<strong>Beta</strong></main></body>");
    StyleResolver resolver(CssParser{}.parse(
        ":root{--ink:#123456}main{font-size:22px}strong{font-weight:700}"));
    RenderTreeOptions options;
    std::vector<StyleOverride> overrides(1);
    StyleOverride text_override;
    const auto find_dom_text = [&](const Node& node, const auto& self) -> const Node* {
        if (node.type == NodeType::Text) return &node;
        for (const auto& child : node.children)
            if (const Node* found = self(*child, self)) return found;
        return nullptr;
    };
    text_override.node = find_dom_text(*document, find_dom_text);
    text_override.has_color = true;
    text_override.color = Color{0x91, 0x32, 0x45, 255};
    overrides[0] = text_override;
    options.style_overrides = &overrides;
    detail::test::reset_text_style_fast_path_calls();
    auto tree = RenderTreeBuilder(resolver, options).build(*document);

    const auto fast_path = detail::test::text_style_fast_path_counters();
    check(fast_path.calls == 2,
          "each text node takes the direct default-style path and skips element-only work");
    const auto* main = find_first_by_tag(*tree, "main");
    const auto* strong = find_first_by_tag(*tree, "strong");
    const auto* first_text = find_first_text(*tree);
    check(main && strong && first_text, "text fast path retains the complete render tree");
    check(main->style.color.r == 0x23 && main->style.color.g == 0x45 &&
              main->style.color.b == 0x67 && first_text->style.color.r == 0x91 &&
              first_text->style.color.g == 0x32 && first_text->style.font_size == 22,
          "custom-property-resolved parent values inherit and host text overrides still apply");
    const auto* nested_text = find_first_text(*strong);
    check(nested_text && nested_text->style.color.r == 0x23 &&
              nested_text->style.font_size == 22 && nested_text->style.font_weight == 700,
          "nested text receives inherited color, font size, and explicit element weight");

    auto layout = LayoutEngine(resolver).layout(*tree, 320, 120);
    check(layout != nullptr && !layout->children.empty(), "text fast path produces layout geometry");
    const LayoutBox* text_box = find_layout_box_for_node(*layout, first_text->node);
    check(text_box && text_box->rect.width > 0 && text_box->rect.height > 0,
          "text geometry remains attached to its source node");
    const auto layers = LayerTreeBuilder{}.build(*layout);
    const int hit_x = text_box->rect.x + text_box->rect.width / 2;
    const int hit_y = text_box->rect.y + text_box->rect.height / 2;
    const auto layout_hit = HitTester{}.hit_test(*layout, hit_x, hit_y);
    const auto layer_hit = HitTester{}.hit_test(*layers, hit_x, hit_y);
    check(static_cast<bool>(layout_hit) && static_cast<bool>(layer_hit) &&
              layout_hit.node == layer_hit.node && layout_hit.local_x == layer_hit.local_x &&
              layout_hit.local_y == layer_hit.local_y,
          "layout and layer hit results remain equivalent at text geometry");
    check(count_layer_display_commands(*layers) > 0, "text fast path retains display commands");
}

void text_resolver_overloads_skip_element_resolution_work() {
    Node parent(NodeType::Element);
    parent.tag_name = "main";
    parent.attributes["style"] = "--ink:#234567";
    auto text = std::make_unique<Node>(NodeType::Text);
    text->text = "direct";
    Node* text_node = text.get();
    parent.append_child(std::move(text));
    StyleResolver resolver(CssParser{}.parse("main{--tone:var(--ink);color:var(--tone)}"));
    StyleResolveContext context;
    const auto before = resolver.statistics();
    detail::test::reset_text_style_fast_path_calls();

    const Style plain = resolver.resolve(*text_node);
    const Style contextual = resolver.resolve(*text_node, context);
    const auto after = resolver.statistics();
    const auto counters = detail::test::text_style_fast_path_counters();
    check(plain.display == Display::Inline && contextual.display == Display::Inline,
          "both direct Text resolver overloads retain the Text default style");
    check(counters.calls == 2,
          "both direct Text resolver overloads skip custom properties, selectors, and cascade slots");
    check(counters.selector_lookup_attempts == 0, "Text performs no selector lookup attempts");
    resolver.resolve(parent);
    resolver.resolve(parent, context);
    check(detail::test::text_style_fast_path_counters().selector_lookup_attempts >= 2,
          "Element resolution still performs selector lookups");
    check(before.candidate_cache_hits == after.candidate_cache_hits &&
              before.candidate_cache_misses == after.candidate_cache_misses,
          "direct Text resolution leaves the element candidate cache untouched");
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
        display_contents_flattens_effective_children_and_preserves_inheritance();
        display_contents_keeps_root_and_replaced_exceptions_and_pre_whitespace();
        inheritance_preserves_local_style_and_parent_values();
        separator_filter_preserves_context_overrides_and_budget();
        text_style_fast_path_preserves_inheritance_and_overrides();
        text_resolver_overloads_skip_element_resolution_work();
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
