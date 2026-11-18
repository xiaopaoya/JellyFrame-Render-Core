#include "render_core/contextual_text_layout.h"
#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "contextual_text_fixture.h"
#include "contextual_text_layout_internal.h"

#include <iostream>

using namespace jellyframe;
using namespace jellyframe::contextual_text;
using namespace shaped_binding_fixture;

namespace {
constexpr std::uint64_t budget = 32U * 1024U * 1024U;

void check_markers(const LayoutBox& box, bool contextual) {
  check(detail::is_contextual_layout_box(box) == contextual, "layout marker isolation");
  for (const auto& child : box.children) check_markers(*child, contextual);
}
}

int main(int argc, char** argv) {
  try {
    check(argc == 2, "fixture root");
    Fixture fixture(argv[1], "regular", true);
    auto work = budget;
    LayoutOptions options;
    check(create_binding(fixture.as_inputs(), {}, work, options.binding) == Status::ok, "binding");
    HtmlParser html;
    CssParser css;
    LayoutResult result;
    for (int width : {55, 104, 150}) {
      for (int font : {16, 20, 24}) {
        auto doc = html.parse("<body><main><section><p>office AV fi \xE4\xB8\xAD e\xCC\x81 A A A</p></section>"
                              "<div>Second AV</div></main></body>");
        StyleResolver styles(css.parse("body{margin:4px}main{width:" + std::to_string(width) +
            "px;padding:7px}section{padding:3px}p,div{margin:0;font-size:" + std::to_string(font) +
            "px;line-height:29px;text-align:center;overflow-wrap:anywhere}"));
        auto render = RenderTreeBuilder(styles).build(*doc);
        LayoutEngine engine(styles);
        LayoutUsage usage;
        work = budget;
        auto status = engine.layout_contextual(*render, 240, 320, options, work, result, &usage);
        if (status != Status::ok) {
          std::cerr << "width=" << width << " font=" << font << " status=" << int(status)
                    << " boxes=" << usage.boxes << " paragraphs=" << usage.paragraphs << '\n';
        }
        check(status == Status::ok && result.paragraphs().size() == 2, "nested text hosts");
        const auto& first = result.paragraphs()[0];
        const auto& second = result.paragraphs()[1];
        check(first.align == TextAlign::Center && first.origin_x_q1000 == 14000 &&
              first.content_width_q1000 == (width - 6) * 1000, "block geometry and alignment");
        check(second.origin_y_q1000 == first.origin_y_q1000 + first.paragraph.height_q1000() + 3000,
              "following block reflects shaped height");
        check(usage.paragraphs == 2 && usage.lines >= 2 && usage.text_result_bytes > 0 &&
              usage.work == budget - work, "aggregate budgets");
        Paragraph direct;
        ParagraphOptions paragraph;
        paragraph.size_q1000 = font * 1000;
        paragraph.width_q1000 = (width - 6) * 1000;
        paragraph.line_height_q1000 = 29000;
        paragraph.emergency_wrap = true;
        work = budget;
        check(layout_paragraph(options.binding, first.owner->node->text, paragraph, {}, work, direct) == Status::ok,
              "direct paragraph reference");
        check(direct.lines().size() == first.paragraph.lines().size(), "DOM wrap matches reference");
        for (std::size_t i = 0; i < direct.lines().size(); ++i) {
          check(direct.lines()[i].packet == first.paragraph.lines()[i].packet &&
                direct.lines()[i].baseline_y_q1000 == first.paragraph.lines()[i].baseline_y_q1000,
                "DOM packets and baselines match reference");
        }
        check_markers(*result.tree(), true);
        auto ordinary = engine.layout(*render, 240, 320);
        check(bool(ordinary), "ordinary layout still works");
        check_markers(*ordinary, false);
        const auto* previous = result.tree();
        work = 1;
        check(engine.layout_contextual(*render, 240, 320, options, work, result) == Status::limit &&
              work == 0 && result.tree() == previous, "refusal preserves output and charges work");
      }
    }
    const auto* previous = result.tree();
    for (const auto* rule : {"div{opacity:0.5}", "div{position:relative}", "div{overflow:hidden}",
                             "div{width:50.5px}", "div{height:20px}", "div{font-weight:700}"}) {
      auto doc = html.parse("<div>office AV</div>");
      StyleResolver styles(css.parse(rule));
      auto render = RenderTreeBuilder(styles).build(*doc);
      work = budget;
      check(LayoutEngine(styles).layout_contextual(*render, 240, 320, options, work, result) == Status::invalid &&
            result.tree() == previous, "unsupported style preserves output");
    }
    for (const auto* source : {"<div>A<span>B</span></div>", "<div>A<img src='x'></div>",
                              "<div>A<br>B</div>"}) {
      auto doc = html.parse(source);
      StyleResolver styles(Stylesheet{});
      auto render = RenderTreeBuilder(styles).build(*doc);
      work = budget;
      check(LayoutEngine(styles).layout_contextual(*render, 240, 320, options, work, result) == Status::invalid &&
            result.tree() == previous, "unsupported topology preserves output");
    }
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
