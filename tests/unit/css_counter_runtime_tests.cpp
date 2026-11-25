#include "css_counter_program.h"
#include "render_core/css_parser.h"
#include "render_core/feature_config.h"
#include "render_core/html_parser.h"
#include "render_core/layer_tree.h"
#include "render_core/layout.h"
#include "render_core/prepared_text.h"
#include "render_core/render_tree.h"
#include "render_core/software_renderer.h"
#include "core_text_test_fixture.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jellyframe;
using namespace jellyframe::detail;

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool prepared_text_contains(const DisplayList& commands, std::string_view expected) {
    for (const auto& command : commands) {
        if (command.type() != DisplayCommandType::PreparedText) continue;
        PreparedTextView view;
        if (!decode_prepared_text(command.text, view)) continue;
        std::string text;
        for (std::size_t index = 0; index < view.size(); ++index) {
            PreparedTextGlyph glyph;
            if (!view.read(index, glyph)) continue;
            if (glyph.codepoint > 0x7fU) return false;
            text.push_back(static_cast<char>(glyph.codepoint));
        }
        if (text.find(expected) != std::string::npos) return true;
    }
    return false;
}

void parser_cases() {
    std::vector<CounterOperation> operations{{"retained", 9}};
    check(parse_counter_operations("chapter -2 section +3", CounterOperationKind::Reset, operations),
          "signed reset values parse");
    check(operations.size() == 2 && operations[0].name == "chapter" && operations[0].value == -2 &&
          operations[1].name == "section" && operations[1].value == 3,
          "counter operation order and values are retained");

    check(parse_counter_operations("chapter", CounterOperationKind::Increment, operations) &&
          operations.size() == 1 && operations[0].value == 1,
          "increment defaults to one");
    check(parse_counter_operations("section", CounterOperationKind::Set, operations) &&
          operations.size() == 1 && operations[0].value == 0,
          "set defaults to zero");
    check(parse_counter_operations("none", CounterOperationKind::Reset, operations) && operations.empty(),
          "none clears the declaration");

    operations = {{"retained", 9}};
    check(!parse_counter_operations("chapter 1 2", CounterOperationKind::Reset, operations) &&
          operations.size() == 1 && operations[0].name == "retained" && operations[0].value == 9,
          "malformed declarations do not publish a partial parse");
    check(!parse_counter_operations("chapter 2147483648", CounterOperationKind::Set, operations),
          "integer overflow is rejected");
    check(!parse_counter_operations("chapter -", CounterOperationKind::Increment, operations),
          "a bare sign is not consumed as an integer");
    check(!parse_counter_operations("default", CounterOperationKind::Reset, operations),
          "reserved CSS-wide identifiers are rejected as counter names");
    check(!parse_counter_operations(std::string(4097, 'x'), CounterOperationKind::Reset, operations),
          "declaration source budget is enforced");
    check(!parse_counter_operations("a b c d e f g h i j k l m n o p q", CounterOperationKind::Reset,
                                    operations),
          "operation count budget is enforced");
}

void declaration_cases() {
    CounterProgram program;
    check(apply_counter_declaration(program, "counter-reset", "chapter 4"),
          "reset declaration applies");
    check(apply_counter_declaration(program, "counter-increment", "chapter 2"),
          "increment declaration applies");
    check(program.reset.operations.size() == 1 && program.reset.operations[0].value == 4 &&
          program.increment.operations.size() == 1 && program.increment.operations[0].value == 2,
          "independent counter declarations coexist");

    check(!apply_counter_declaration(program, "counter-reset", "chapter 1.5") &&
          program.reset.operations.size() == 1 && program.reset.operations[0].value == 4,
          "an invalid replacement preserves the previous declaration");
    check(apply_counter_declaration(program, "counter-set", "chapter 7"),
          "set declaration is represented for inheritance");
    const CounterProgram parent = program;
    CounterProgram child;
    child.set.inherits = true;
    inherit_counter_declarations(child, &parent);
    check(!child.set.inherits && child.set.operations.size() == 1 && child.set.operations[0].name == "chapter",
          "explicit inheritance copies the parent's computed declaration");
    check(counter_declaration_syntax_supported("counter-increment", "initial") &&
          counter_declaration_syntax_supported("counter-reset", "none") &&
          counter_declaration_syntax_supported("counter-reset", "revert") &&
          !counter_declaration_syntax_supported("counter-reset", "revert-layer") &&
          !counter_declaration_syntax_supported("counter-set", "x 1.5"),
          "property syntax gate accepts the bounded CSS-wide values and rejects fractional integers");
}

void content_cases() {
    std::string name = "preserved";
    std::string suffix = "old";
    check(parse_decimal_counter_content("counter(chapter)", name, suffix) &&
          name == "chapter" && suffix.empty(), "decimal counter function parses");
    check(parse_decimal_counter_content("counter(ch\\61 pter, DECIMAL) \".\\31 \"", name, suffix) &&
          name == "chapter" && suffix == ".1", "escaped identifiers, format and string suffix decode");

    name = "preserved";
    suffix = "old";
    check(!parse_decimal_counter_content("counter(chapter, lower-roman)", name, suffix) &&
          name == "preserved" && suffix == "old", "unsupported formats leave outputs untouched");
    check(!parse_decimal_counter_content("counter(chapter) trailing", name, suffix),
          "trailing tokens after the counter function are rejected");
}

RenderObject* make_child(RenderObject& parent) {
    RenderObjectPtr child(new RenderObject, RenderObjectDeleter{});
    child->type = RenderObjectType::Block;
    auto* result = child.get();
    parent.children.push_back(std::move(child));
    return result;
}

void counter_tree_cases() {
    RenderObject root;
    auto* first = make_child(root);
    auto* nested = make_child(*first);
    auto* sibling = make_child(root);

    root.style.before_content_kind = GeneratedContentKind::Counter;
    root.style.before_counter_name = "chapter";
    root.style.before_counter_suffix = ".";
    root.style.after_content_kind = GeneratedContentKind::Counter;
    root.style.after_counter_name = "chapter";
    first->style.before_content_kind = GeneratedContentKind::Counter;
    first->style.before_counter_name = "chapter";
    first->style.after_content_kind = GeneratedContentKind::Counter;
    first->style.after_counter_name = "chapter";
    nested->style.before_content_kind = GeneratedContentKind::Counter;
    nested->style.before_counter_name = "chapter";
    sibling->style.before_content_kind = GeneratedContentKind::Counter;
    sibling->style.before_counter_name = "chapter";

    CounterPrograms programs;
    CounterProgram root_program;
    check(apply_counter_declaration(root_program, "counter-reset", "chapter 1"),
          "root reset program parses");
    RenderCounterPrograms root_programs;
    root_programs.element = std::move(root_program);
    programs.emplace(&root, std::move(root_programs));
    CounterProgram first_program;
    check(apply_counter_declaration(first_program, "counter-increment", "chapter"),
          "child increment program parses");
    RenderCounterPrograms first_programs;
    first_programs.element = std::move(first_program);
    programs.emplace(first, std::move(first_programs));
    CounterProgram nested_program;
    check(apply_counter_declaration(nested_program, "counter-reset", "chapter 8"),
          "nested reset program parses");
    check(apply_counter_declaration(nested_program, "counter-increment", "chapter 2"),
          "nested increment program parses");
    RenderCounterPrograms nested_programs;
    nested_programs.element = std::move(nested_program);
    programs.emplace(nested, std::move(nested_programs));

    check(finalize_counter_tree(root, programs, nullptr), "nested counter tree evaluates");
    check(root.style.before_content_text == "1." && root.style.after_content_text == "2",
          "before and after pseudo content observes source-order counter state");
    check(first->style.before_content_text == "2" && first->style.after_content_text == "2",
          "element increments apply before before-content and remain after descendants");
    check(nested->style.before_content_text == "10" && sibling->style.before_content_text == "2",
          "nested reset scope is visible to descendants then popped for siblings");
}

void refusal_is_transactional() {
    RenderObject root;
    auto* child = make_child(root);
    root.style.before_content_kind = GeneratedContentKind::Counter;
    root.style.before_counter_name = "items";
    root.style.before_content_text = "stale-root";
    child->style.before_content_kind = GeneratedContentKind::Counter;
    child->style.before_counter_name = "items";
    child->style.before_content_text = "stale-child";

    CounterPrograms programs;
    CounterProgram root_program;
    check(apply_counter_declaration(root_program, "counter-reset", "items 2147483647"),
          "maximum counter value parses");
    RenderCounterPrograms root_programs;
    root_programs.element = std::move(root_program);
    programs.emplace(&root, std::move(root_programs));
    CounterProgram child_program;
    check(apply_counter_declaration(child_program, "counter-increment", "items"),
          "overflowing increment parses as an integer declaration");
    RenderCounterPrograms child_programs;
    child_programs.element = std::move(child_program);
    programs.emplace(child, std::move(child_programs));

    VectorDiagnosticSink diagnostics;
    check(!finalize_counter_tree(root, programs, &diagnostics), "counter overflow refuses the complete tree");
    check(root.style.before_content_text == "stale-root" && child->style.before_content_text == "stale-child",
          "failed evaluation publishes none of its generated text");
    check(diagnostics.size() == 1 &&
          diagnostics.diagnostics().front().code == "css-counter-evaluation-refused",
          "counter evaluation refusal reports one bounded diagnostic");
}

RenderObject* find_render_object(RenderObject& root, std::string_view id) {
    if (root.node != nullptr && root.node->attribute("id") == id) return &root;
    for (auto& child : root.children) {
        if (auto* found = find_render_object(*child, id)) return found;
    }
    return nullptr;
}

Node* find_dom_node(Node& root, std::string_view id) {
    if (root.attribute("id") == id) return &root;
    for (auto& child : root.children) {
        if (auto* found = find_dom_node(*child, id)) return found;
    }
    return nullptr;
}

void render_tree_counter_integration() {
    test::CoreTextTestFixture fonts;
    check(fonts.ready(), "Core TextSystem fixture binds for generated counter paint");
    auto document = HtmlParser{}.parse(
        "<div id='root'><p id='first'>one</p><p id='second'>two</p><p id='third'>three</p></div>");
    const auto stylesheet = CssParser{}.parse(
        "#root { --start: 3; counter-reset: chapter var(--start); }"
        "#first { counter-increment: chapter 2; }"
        "#first.bump { counter-increment: chapter 11; }"
        "#second { counter-set: chapter 20; counter-increment: chapter 3; }"
        "#third { counter-increment: chapter; }"
        "p::before { content: counter(chapter) '.'; color: #ff0000; }");
    StyleResolver resolver(stylesheet);
    RenderObjectPtr tree = RenderTreeBuilder(resolver).build(*document);

    const auto* first = find_render_object(*tree, "first");
    const auto* second = find_render_object(*tree, "second");
    const auto* third = find_render_object(*tree, "third");
    check(first != nullptr && second != nullptr && third != nullptr,
          "counter integration builds all ordinary block-flow elements");
    check(first->style.before_content_kind == GeneratedContentKind::Counter &&
          first->style.before_content_text == "5.",
          "cascade resolves custom-property reset and per-element increment before generated content");
    check(second->style.before_content_text == "20." && third->style.before_content_text == "21.",
          "set follows increment and retains source-order state across following siblings");

    LayoutEngineOptions layout_options;
    layout_options.text_system = &fonts.system();
    auto layout = LayoutEngine(resolver, layout_options).layout(*tree, 240, 120);
    LayerTreeBuilderOptions layer_options;
    layer_options.text_system = &fonts.system();
    LayerTreeBuilder layer_builder(layer_options);
    auto layers = layer_builder.build(*layout);
    const DisplayList commands = layer_builder.flatten(*layers);
    check(prepared_text_contains(commands, "5.") && prepared_text_contains(commands, "20.") &&
          prepared_text_contains(commands, "21."),
          "resolved counter content reaches the established Core-prepared text paint path");
    SoftwareCompositor::Options paint_options;
    paint_options.text_system = &fonts.system();
    const auto frame = SoftwareCompositor({}, paint_options).render(
        *layers, 240, 120, Color{255, 255, 255, 255});
    check(std::any_of(frame.pixels.begin(), frame.pixels.end(), [](Color pixel) {
        return pixel.r > 200 && pixel.g < 80 && pixel.b < 80;
    }), "generated counter glyphs are rasterized into the software-composited pixels");

    Node* mutable_first = find_dom_node(*document, "first");
    check(mutable_first != nullptr, "counter mutation fixture finds its target node");
    mutable_first->set_attribute("class", "bump");
    auto mutated_tree = RenderTreeBuilder(resolver).build(*document);
    check(find_render_object(*mutated_tree, "first")->style.before_content_text == "14." &&
          find_render_object(*mutated_tree, "second")->style.before_content_text == "20.",
          "DOM mutation rebuilds the counter sequence and downstream sibling values");
}

void counter_values_respect_style_reuse_boundary() {
    auto document = HtmlParser{}.parse(
        "<div id='root'><p id='first'>one</p><p id='second'>two</p></div>");
    StyleResolver resolver(CssParser{}.parse(
        "#root { counter-reset: section 3; }"
        "#first { counter-increment: section 2; }"
        "#first.bump { counter-increment: section 11; }"
        "p::before { content: counter(section); }"));
    RenderTreeOptions options;
    RenderTreeStyleReuse reuse;
    check(!reuse.prepare_tracked(*document, resolver, options),
          "scoped style reuse refuses a counter-dependent stylesheet");
    auto first_tree = RenderTreeBuilder(resolver, options).build(*document);
    check(find_render_object(*first_tree, "first")->style.before_content_text == "5" &&
          find_render_object(*first_tree, "second")->style.before_content_text == "5",
          "counter baseline resolves initial values");

    find_dom_node(*document, "first")->set_attribute("class", "bump");
    auto next_tree = RenderTreeBuilder(resolver, options).build(*document);
    check(next_tree && find_render_object(*next_tree, "first")->style.before_content_text == "14" &&
          find_render_object(*next_tree, "second")->style.before_content_text == "14",
          "after style reuse is refused, mutated counters and downstream sibling state are rebuilt");
}

void supports_query_cases() {
    const auto accepted = CssParser{}.parse(
        "@supports (counter-reset: chapter) { #supported { width: 31px; } }"
        "@supports (counter-increment: chapter -2) { #integer { width: 37px; } }"
        "@supports (counter-set: chapter 1.5) { #fraction { width: 41px; } }");
    auto document = HtmlParser{}.parse(
        "<div id='supported'></div><div id='integer'></div><div id='fraction'></div>");
    StyleResolver resolver(accepted);
    auto tree = RenderTreeBuilder(resolver).build(*document);
    const auto* supported = find_render_object(*tree, "supported");
    const auto* integer = find_render_object(*tree, "integer");
    const auto* fraction = find_render_object(*tree, "fraction");
    check(supported != nullptr && supported->style.width == 31,
          "supports accepts a valid counter-reset declaration");
    check(integer != nullptr && integer->style.width == 37,
          "supports accepts signed integral counter operation values");
    check(fraction != nullptr && fraction->style.width != 41,
          "supports rejects fractional counter values");
}

void unsupported_layout_topology_is_refused() {
    auto document = HtmlParser{}.parse("<div id='root'><p id='count'></p></div>");
    StyleResolver resolver(CssParser{}.parse(
        "#root { display: flex; } #count::before { content: counter(chapter); }"));
    VectorDiagnosticSink diagnostics;
    RenderTreeOptions options;
    options.diagnostics = &diagnostics;
    auto tree = RenderTreeBuilder(resolver, options).build(*document);
    const auto* count = find_render_object(*tree, "count");
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    check(count != nullptr && count->style.before_content_text.empty(),
          "counter values are not partially committed for unsupported Flex topology");
    check(std::any_of(diagnostics.diagnostics().begin(), diagnostics.diagnostics().end(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.code == "css-counter-topology-refused";
        }), "unsupported counter topology reports a bounded refusal diagnostic");
#else
    check(count != nullptr && count->style.before_content_text == "0",
          "without Flex support the CSS declaration falls back to ordinary block-flow counters");
#endif
}

void css_wide_counter_values_in_tree() {
    auto document = HtmlParser{}.parse(
        "<div id='root'><p id='inherited'></p><p id='none'></p><p id='unset'></p></div>");
    StyleResolver resolver(CssParser{}.parse(
        "#root { counter-reset: section 5; }"
        "#inherited { counter-reset: inherit; }"
        "#none { counter-reset: none; counter-increment: section; }"
        "#unset { counter-increment: unset; }"
        "p::before { content: counter(section); }"));
    auto tree = RenderTreeBuilder(resolver).build(*document);
    const auto* inherited = find_render_object(*tree, "inherited");
    const auto* none = find_render_object(*tree, "none");
    const auto* unset = find_render_object(*tree, "unset");
    check(inherited != nullptr && inherited->style.before_content_text == "5",
          "explicit inherit copies the parent's counter-reset computed declaration");
    check(none != nullptr && none->style.before_content_text == "6",
          "none clears the local reset and increment updates the active ancestor counter");
    check(unset != nullptr && unset->style.before_content_text == "6",
          "unset restores the non-inherited initial increment value");
}

void revert_counter_values_in_tree() {
    auto document = HtmlParser{}.parse(
        "<div id='root'><p id='reset'></p><p id='increment'></p></div>");
    StyleResolver resolver(CssParser{}.parse(
        "#root { counter-reset: section 5; }"
        "#reset { counter-reset: section 9; counter-reset: revert; }"
        "#increment { counter-increment: section 3; counter-increment: revert; }"
        "p::before { content: counter(section); }"));
    auto tree = RenderTreeBuilder(resolver).build(*document);
    const auto* reset = find_render_object(*tree, "reset");
    const auto* increment = find_render_object(*tree, "increment");
    check(reset != nullptr && reset->style.before_content_text == "5",
          "revert restores the initial author-origin reset behavior");
    check(increment != nullptr && increment->style.before_content_text == "5",
          "revert removes the author-origin increment and preserves the active counter");
}

void pseudo_counter_operations_in_tree() {
    auto document = HtmlParser{}.parse(
        "<div id='root'><p id='one'></p><p id='two'></p></div>");
    StyleResolver resolver(CssParser{}.parse(
        "#root { counter-reset: item; }"
        "p::before { counter-increment: item; content: counter(item) '.'; }"));
    auto tree = RenderTreeBuilder(resolver).build(*document);
    const auto* one = find_render_object(*tree, "one");
    const auto* two = find_render_object(*tree, "two");
    check(one != nullptr && one->style.before_content_text == "1." &&
          two != nullptr && two->style.before_content_text == "2.",
          "before pseudo counter increments run in document order and flow into later siblings");
}

void pseudo_counter_reset_scope() {
    auto document = HtmlParser{}.parse(
        "<div id='root'><p id='nested'></p><p id='sibling'></p></div>");
    StyleResolver resolver(CssParser{}.parse(
        "#root { counter-reset: item; }"
        "#nested::before { counter-reset: item 8; counter-increment: item 2; content: counter(item); }"
        "#sibling::before { counter-increment: item; content: counter(item); }"));
    auto tree = RenderTreeBuilder(resolver).build(*document);
    const auto* nested = find_render_object(*tree, "nested");
    const auto* sibling = find_render_object(*tree, "sibling");
    check(nested != nullptr && nested->style.before_content_text == "10" &&
          sibling != nullptr && sibling->style.before_content_text == "1",
          "before pseudo reset scopes are popped after generated content while outer increments persist");
}

void render_tree_counter_program_budget() {
    std::string markup = "<div id='root'>";
    constexpr std::size_t item_count = 300;
    for (std::size_t index = 0; index < item_count; ++index)
        markup += "<p class='item'></p>";
    markup += "</div>";
    auto document = HtmlParser{}.parse(markup);
    StyleResolver resolver(CssParser{}.parse(
        "#root { counter-reset: item; }"
        ".item::before { counter-increment: item; content: counter(item); }"));
    VectorDiagnosticSink diagnostics;
    RenderTreeOptions options;
    options.diagnostics = &diagnostics;
    options.max_render_objects = 1024;
    auto tree = RenderTreeBuilder(resolver, options).build(*document);
    std::size_t visible_counter_sources = 0;
    std::size_t resolved_counter_sources = 0;
    std::vector<const RenderObject*> pending{tree.get()};
    while (!pending.empty()) {
        const auto* current = pending.back();
        pending.pop_back();
        if (is_generated_counter(current->style.before_content_kind)) {
            ++visible_counter_sources;
            if (!current->style.before_content_text.empty()) ++resolved_counter_sources;
        }
        for (const auto& child : current->children) pending.push_back(child.get());
    }
    check(visible_counter_sources == item_count && resolved_counter_sources == 0,
          "aggregate counter-program admission refuses all generated values at its bound");
    check(std::any_of(diagnostics.diagnostics().begin(), diagnostics.diagnostics().end(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.code == "css-counter-topology-refused";
        }), "aggregate counter-program refusal reports a bounded diagnostic");
}

void document_root_counter_program_budget() {
    constexpr std::size_t names_per_declaration = 16;
    constexpr std::size_t name_size = 60;
    std::string names;
    for (std::size_t index = 0; index < names_per_declaration; ++index) {
        if (!names.empty()) names.push_back(' ');
        names.push_back('n');
        names.append(name_size - 1, static_cast<char>('a' + (index % 26)));
    }
    const std::string declaration =
        "counter-reset: " + names + "; counter-set: " + names +
        "; counter-increment: " + names + ";";
    const std::string markup = "<html id='root'><p></p></html>";
    const std::string stylesheet =
        "#root { " + declaration + " }"
        "#root::before { " + declaration + " content: counter(n); }"
        "#root::after { " + declaration + " content: counter(n); }";
    auto document = HtmlParser{}.parse(markup);
    StyleResolver resolver(CssParser{}.parse(stylesheet));
    VectorDiagnosticSink diagnostics;
    RenderTreeOptions options;
    options.diagnostics = &diagnostics;
    auto tree = RenderTreeBuilder(resolver, options).build(*document);
    const auto* root = find_render_object(*tree, "root");
    check(root != nullptr && root->style.before_content_text.empty() &&
          root->style.after_content_text.empty(),
          "document-root counter programs over the aggregate name-byte budget are refused");
    check(std::any_of(diagnostics.diagnostics().begin(), diagnostics.diagnostics().end(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.code == "css-counter-topology-refused";
        }), "document-root counter-program refusal reports a bounded diagnostic");
}

} // namespace

int main() {
    parser_cases();
    declaration_cases();
    content_cases();
    counter_tree_cases();
    refusal_is_transactional();
    render_tree_counter_integration();
    counter_values_respect_style_reuse_boundary();
    supports_query_cases();
    unsupported_layout_topology_is_refused();
    css_wide_counter_values_in_tree();
    revert_counter_values_in_tree();
    pseudo_counter_operations_in_tree();
    pseudo_counter_reset_scope();
    render_tree_counter_program_budget();
    document_root_counter_program_budget();
}
