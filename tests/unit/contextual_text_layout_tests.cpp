#include "render_core/contextual_text_layout.h"
#include "render_core/arena.h"
#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/software_renderer.h"
#include "contextual_text_fixture.h"
#include "contextual_text_layout_internal.h"

#include <algorithm>
#include <iostream>

using namespace jellyframe;
using namespace jellyframe::contextual_text;
using namespace shaped_binding_fixture;

namespace {
constexpr std::uint64_t budget = 32U * 1024U * 1024U;
const Rect viewport{0, 0, 240, 320};

void check_markers(const LayoutBox& box, bool contextual) {
  check(detail::is_contextual_layout_box(box) == contextual, "layout marker isolation");
  for (const auto& child : box.children) check_markers(*child, contextual);
}

bool same_pixels(const FrameBuffer& a, const FrameBuffer& b) {
  return a.pixels.size() == b.pixels.size() &&
         std::equal(a.pixels.begin(), a.pixels.end(), b.pixels.begin(), [](Color lhs, Color rhs) {
           return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
         });
}

void paint_paragraph(const LayoutParagraph& p, FrameBuffer& target) {
  for (const auto& line : p.paragraph.lines()) {
    const auto slack = std::int64_t(p.content_width_q1000) - line.metrics.advance_x;
    const auto offset = p.align == TextAlign::Center ? slack / 2 : p.align == TextAlign::End ? slack : 0;
    auto work = budget;
    check(paint(p.paragraph.binding(), line.packet, target,
                static_cast<std::int32_t>(p.origin_x_q1000 + offset),
                p.origin_y_q1000 + line.baseline_y_q1000, viewport, p.color, {}, work) == Status::ok,
          "paint captured line");
  }
}

FrameBuffer draw_capture(const LayerCapture& capture, bool all_backgrounds_first = false) {
  FrameBuffer target(viewport.width, viewport.height, {255, 255, 255, 255});
  SoftwareRasterizer raster;
  const auto& commands = capture.layer_tree()->display_list;
  const auto& paragraphs = capture.layout().paragraphs();
  const auto& insertion_points = capture.after_command_counts();
  check(insertion_points.size() == paragraphs.size(), "one insertion point per paragraph");
  std::size_t emitted = 0;
  for (std::size_t i = 0; i < paragraphs.size(); ++i) {
    const auto stop = all_backgrounds_first ? commands.size() : insertion_points[i];
    check(stop >= emitted && stop <= commands.size(), "bounded, ordered insertion points");
    while (emitted < stop) raster.rasterize(commands[emitted++], target, viewport);
    paint_paragraph(paragraphs[i], target);
  }
  while (emitted < commands.size()) raster.rasterize(commands[emitted++], target, viewport);
  return target;
}

void check_capture(const char* fixture_root) {
  LayerCapture capture;
  LayoutResult expired_layout;
  check(!capture.valid() && !capture.layer_tree() && !capture.layout().valid() &&
        capture.after_command_counts().empty(), "empty capture accessors");
  FrameBuffer expected(viewport.width, viewport.height, {255, 255, 255, 255});
  {
    Fixture fixture(fixture_root, "regular", true);
    LayoutOptions options;
    auto work = budget;
    check(create_binding(fixture.as_inputs(), {}, work, options.binding) == Status::ok, "capture binding");
    auto doc = HtmlParser().parse("<body><section><p>office AV fi</p><div>Second AV</div>"
                                 "<aside></aside></section></body>");
    StyleResolver styles(CssParser().parse(
        "body{margin:4px;width:200px;background:#dddddd}section{padding:3px;background:#ffeeaa}"
        "p,div{font-size:24px;line-height:5px;margin:0}p{background:#99ddff}div{background:#ffaaaa}"
        "aside{padding:5px;background-color:#009900;background-clip:content-box}"));
    auto render = RenderTreeBuilder(styles).build(*doc);
    LayoutResult layout;
    work = budget;
    check(LayoutEngine(styles).layout_contextual(*render, viewport.width, viewport.height,
                                                  options, work, layout) == Status::ok,
          "capture fixture layout");
    check(LayerTreeBuilder().build_contextual(layout, capture) && capture.valid(), "build contextual capture");
    check(capture.layer_tree()->display_list.size() == 4 &&
          capture.after_command_counts() == std::vector<std::uint32_t>({3, 4}),
          "background order, empty clip emits no command");

    // Independent traversal oracle: positive border-box backgrounds fill
    // their layout rects, while the empty content-box background is omitted.
    SoftwareRasterizer raster;
    std::size_t paragraph = 0;
    const auto visit = [&](const auto& self, const LayoutBox& box) -> void {
      if (box.node->type == NodeType::Text) {
        check(paragraph < layout.paragraphs().size(), "paragraph in DOM order");
        paint_paragraph(layout.paragraphs()[paragraph++], expected);
      } else if (box.style.background_color.a && box.node->tag_name != "aside") {
        DisplayCommand command;
        command.rect = box.rect;
        command.color = box.style.background_color;
        raster.rasterize(command, expected, viewport);
      }
      for (const auto& child : box.children) self(self, *child);
    };
    visit(visit, *layout.tree());
    check(same_pixels(draw_capture(capture), expected), "capture matches paint-order pixel oracle");
    check(!same_pixels(draw_capture(capture, true), expected), "fixture detects incorrect background batching");

    MonotonicArena arena;
    check(!LayerTreeBuilder().build(*layout.tree()) &&
          !LayerTreeBuilder().build(*layout.paragraphs()[0].owner, arena),
          "scalar layer build refuses contextual root and subtree");
    LayerTreeBuilderOptions exact;
    exact.max_display_commands = 4;
    for (const auto& p : layout.paragraphs()) for (const auto& line : p.paragraph.lines()) {
      if (line.metrics.has_ink) ++exact.max_display_commands;
    }
    check(LayerTreeBuilder(exact).build_contextual(layout, capture), "exact command limit");
    const auto* previous = capture.layer_tree();
    --exact.max_display_commands;
    check(!LayerTreeBuilder(exact).build_contextual(layout, capture) && capture.layer_tree() == previous,
          "one below command limit preserves capture");
    exact.max_layers = 0;
    check(!LayerTreeBuilder(exact).build_contextual(layout, capture) && capture.layer_tree() == previous,
          "zero-layer limit preserves capture");

    doc->set_attribute("data-change", "1");
    check(!LayerTreeBuilder().build_contextual(layout, capture) && capture.layer_tree() == previous,
          "stale DOM generation preserves capture");

    Node source(NodeType::Element), target(NodeType::Element);
    source.tag_name = target.tag_name = "div";
    auto subtree = std::make_unique<Node>(NodeType::Element);
    subtree->tag_name = "p";
    auto text = std::make_unique<Node>(NodeType::Text);
    text->set_text("Moved AV");
    subtree->append_child(std::move(text));
    auto& attached = source.append_child(std::move(subtree));
    auto branch = RenderTreeBuilder(styles).build(attached);
    LayoutResult moved_layout;
    work = budget;
    check(LayoutEngine(styles).layout_contextual(*branch, viewport.width, viewport.height,
                                                  options, work, moved_layout) == Status::ok,
          "subtree layout with a document ancestor");
    target.append_child(source.detach_child(attached));
    check(target.mutation_generation == moved_layout.source_generation(), "fixture has equal generations");
    check(!LayerTreeBuilder().build_contextual(moved_layout, capture) && capture.layer_tree() == previous,
          "same-generation different document preserves capture");

    branch = RenderTreeBuilder(styles).build(attached);
    work = budget;
    check(LayoutEngine(styles).layout_contextual(*branch, viewport.width, viewport.height,
                                                  options, work, moved_layout) == Status::ok,
          "layout before removing the subtree");
    check(target.remove_child(attached), "destroy the layout subtree");
    check(!LayerTreeBuilder().build_contextual(moved_layout, capture) && capture.layer_tree() == previous,
          "removed subtree preserves capture");

    auto standalone = make_element("div");
    standalone->append_child(make_text("Standalone AV"));
    branch = RenderTreeBuilder(styles).build(*standalone);
    work = budget;
    check(LayoutEngine(styles).layout_contextual(*branch, viewport.width, viewport.height,
                                                  options, work, moved_layout) == Status::ok,
          "layout of standalone subtree");
    auto& temporarily_attached = target.append_child(std::move(standalone));
    temporarily_attached.children.front()->set_text("Changed AV");
    standalone = target.detach_child(temporarily_attached);
    check(!LayerTreeBuilder().build_contextual(moved_layout, capture) && capture.layer_tree() == previous,
          "attach and detach invalidates the earlier standalone layout");

    work = budget;
    check(LayoutEngine(styles).layout_contextual(*render, viewport.width, viewport.height,
                                                  options, work, expired_layout) == Status::ok,
          "layout before destroying its document");
  }
  const auto* previous = capture.layer_tree();
  check(!LayerTreeBuilder().build_contextual(expired_layout, capture) && capture.layer_tree() == previous,
        "expired document preserves capture");
  check(same_pixels(draw_capture(capture), expected), "capture retains paint after source and binding destruction");
}
}

int main(int argc, char** argv) {
  try {
    check(argc == 2, "fixture root");
    check_capture(argv[1]);
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
