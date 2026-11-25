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

const LayoutBox* find_box_by_id(const LayoutBox& box, const std::string& id) {
  const auto found = box.node->attributes.find("id");
  if (found != box.node->attributes.end() && found->second == id) return &box;
  for (const auto& child : box.children) {
    if (const auto* match = find_box_by_id(*child, id)) return match;
  }
  return nullptr;
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

void check_text_hit_capture(const char* fixture_root) {
  Fixture fixture(fixture_root, "regular", true);
  LayoutOptions options;
  auto work = budget;
  check(create_binding(fixture.as_inputs(), {}, work, options.binding) == Status::ok,
        "hit capture binding");
  auto doc = HtmlParser().parse("<div>  A  \xE4\xB8\xAD  </div>");
  StyleResolver styles(CssParser().parse(
      "*{margin:0}div{width:20px;font-size:16px;line-height:22px;text-align:center}"));
  auto render = RenderTreeBuilder(styles).build(*doc);
  LayoutResult layout;
  work = budget;
  check(LayoutEngine(styles).layout_contextual(*render, viewport.width, viewport.height,
                                                options, work, layout) == Status::ok,
        "build wrapped UTF-8 hit capture fixture");
  check(layout.paragraphs().size() == 1 && layout.paragraphs()[0].paragraph.lines().size() == 2,
        "hit capture fixture wraps to two lines");

  LayerTreeBuilder builder;
  LayerCapture paint_only;
  check(builder.build_contextual(layout, paint_only) && paint_only.text_hit_fragments().empty(),
        "ordinary contextual capture leaves hit capture disabled");
  TextHitCaptureOptions hit_options;
  LayerCapture capture;
  check(builder.build_contextual(layout, hit_options, capture), "capture text hits");
  const auto& hits = capture.text_hit_fragments();
  check(hits.size() == 2 && hits[0].line_index == 0 && hits[0].source_begin == 2 &&
        hits[0].source_end == 3 && hits[1].line_index == 1 && hits[1].source_begin == 5 &&
        hits[1].source_end == 8 && hits[0].owner_index == 0 && hits[1].owner_index == 0 &&
        hits[0].paragraph_index == 0 && hits[1].paragraph_index == 0,
        "hit fragments retain original byte ranges across whitespace collapse and wrap");
  check(hits[0].ink.x > 0 && hits[1].ink.x > 0 &&
        hits[0].ink.y + hits[0].ink.height <= hits[1].ink.y,
        "hit bounds follow aligned and wrapped glyph placement");

  const auto pixels = draw_capture(capture);
  check(same_pixels(pixels, draw_capture(paint_only)), "text hit capture preserves pixels");
  for (const auto& hit : hits) {
    std::size_t ink_pixels = 0;
    for (int y = hit.ink.y; y < hit.ink.y + hit.ink.height; ++y) {
      for (int x = hit.ink.x; x < hit.ink.x + hit.ink.width; ++x) {
        if (pixels.pixel(x, y).r < 255) ++ink_pixels;
      }
    }
    check(ink_pixels > 0, "each retained hit bound covers painted ink");
  }
  hit_options.max_fragments = 2;
  check(builder.build_contextual(layout, hit_options, capture) &&
        capture.text_hit_fragments().size() == 2, "fragment limit is inclusive");
  const auto* prior_tree = capture.layer_tree();
  const auto* prior_hits = capture.text_hit_fragments().data();
  hit_options.max_fragments = 1;
  check(!builder.build_contextual(layout, hit_options, capture) &&
        capture.layer_tree() == prior_tree && capture.text_hit_fragments().data() == prior_hits,
        "fragment limit preserves prior capture and hit data");
  hit_options.max_fragments = 2;
  hit_options.max_work = 1;
  check(!builder.build_contextual(layout, hit_options, capture) &&
        capture.layer_tree() == prior_tree && capture.text_hit_fragments().data() == prior_hits,
        "work limit preserves prior capture and hit data");
  doc->set_attribute("data-change", "1");
  check(!builder.build_contextual(layout, TextHitCaptureOptions{}, capture) &&
        capture.layer_tree() == prior_tree && capture.text_hit_fragments().data() == prior_hits,
        "stale document preserves prior capture and hit data");
}

void check_capture(const char* fixture_root) {
  LayerCapture capture;
  LayoutResult expired_layout;
  check(!capture.valid() && !capture.layer_tree() && !capture.layout().valid() &&
        capture.after_command_counts().empty(), "empty capture accessors");
  FrameBuffer expected(viewport.width, viewport.height, {221, 221, 221, 255});
  {
    Fixture fixture(fixture_root, "regular", true);
    LayoutOptions options;
    auto work = budget;
    check(create_binding(fixture.as_inputs(), {}, work, options.binding) == Status::ok, "capture binding");
    auto doc = HtmlParser().parse("<body><section><p>office AV fi</p><div>Second AV</div>"
                                 "<aside></aside></section></body>");
    StyleResolver styles(CssParser().parse(
        "html{background:#dddddd}body{margin:4px;width:200px;background:#bbbbbb}"
        "section{padding:3px;background:#ffeeaa}"
        "p,div{font-size:24px;line-height:5px;margin:0}p{background:#99ddff}div{background:#ffaaaa}"
        "aside{padding:5px;background-color:#009900;background-clip:content-box}"));
    auto render = RenderTreeBuilder(styles).build(*doc);
    LayoutResult layout;
    work = budget;
    check(LayoutEngine(styles).layout_contextual(*render, viewport.width, viewport.height,
                                                  options, work, layout) == Status::ok,
          "capture fixture layout");
    check(LayerTreeBuilder().build_contextual(layout, capture) && capture.valid(), "build contextual capture");
    check(capture.layer_tree()->bounds.x == 0 && capture.layer_tree()->bounds.y == 0 &&
          capture.layer_tree()->bounds.width == viewport.width &&
          capture.layer_tree()->bounds.height == viewport.height,
          "contextual root layer covers the viewport");
    check(!capture.layer_tree()->display_list.empty() &&
          capture.layer_tree()->display_list.front().rect.x == 0 &&
          capture.layer_tree()->display_list.front().rect.y == 0 &&
          capture.layer_tree()->display_list.front().rect.width == viewport.width &&
          capture.layer_tree()->display_list.front().rect.height == viewport.height &&
          capture.layer_tree()->display_list.front().color.r == 221,
          "canvas background uses the html background across the viewport");
    check(capture.layer_tree()->display_list.size() == 5 &&
          capture.after_command_counts() == std::vector<std::uint32_t>({4, 5}),
          "background order and empty clip emit no command");

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
    exact.max_display_commands = 5;
    for (const auto& p : layout.paragraphs()) for (const auto& line : p.paragraph.lines()) {
      if (line.metrics.has_ink) ++exact.max_display_commands;
    }
    check(LayerTreeBuilder(exact).build_contextual(layout, capture), "exact command limit");
    const auto* previous = capture.layer_tree();
    --exact.max_display_commands;
    check(!LayerTreeBuilder(exact).build_contextual(layout, capture) && capture.layer_tree() == previous,
          "one below command limit preserves capture");
    ++exact.max_display_commands;
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

void check_nowrap_flex_row(const char* fixture_root) {
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
  Fixture fixture(fixture_root, "regular", true);
  LayoutOptions options;
  auto work = budget;
  check(create_binding(fixture.as_inputs(), {}, work, options.binding) == Status::ok,
        "Flex binding");
  auto doc = HtmlParser().parse(
      "<body><main id='row'><div id='first'>office AV</div><div id='second'>Second fi</div></main></body>");
  StyleResolver styles(CssParser().parse(
      "body{margin:0}main{display:flex;flex-flow:row nowrap;width:160px;column-gap:10px;"
      "align-items:flex-start;color:#aa0000;font-size:16px}"
      "#first{width:60px;min-width:0;color:#0000cc;font-size:20px;line-height:24px;text-align:center}"
      "#second{width:90px;min-width:0;color:#008800;font-size:16px;line-height:20px;text-align:end}"));
  auto render = RenderTreeBuilder(styles).build(*doc);
  LayoutResult result;
  work = budget;
  const auto status = LayoutEngine(styles).layout_contextual(
      *render, viewport.width, viewport.height, options, work, result);
  check(status == Status::ok && result.valid() && result.paragraphs().size() == 2,
        "definite-width nowrap Flex row lays out contextual text");

  const auto* row = find_box_by_id(*result.tree(), "row");
  const auto* first = find_box_by_id(*result.tree(), "first");
  const auto* second = find_box_by_id(*result.tree(), "second");
  check(row && first && second, "Flex boxes retained in layout");
  check(row->rect.width == 160 && first->rect.x == row->rect.x && first->rect.width == 60 &&
        second->rect.x == first->rect.x + first->rect.width + 10 && second->rect.width == 90,
        "Flex allocator positions definite row items and gap");

  const auto& first_paragraph = result.paragraphs()[0];
  const auto& second_paragraph = result.paragraphs()[1];
  check(first_paragraph.owner->node->text == "office AV" &&
        first_paragraph.origin_x_q1000 == std::int64_t(first->content_geometry.x) &&
        first_paragraph.content_width_q1000 == 60000 && first_paragraph.align == TextAlign::Center &&
        first_paragraph.color.r == 0 && first_paragraph.color.g == 0 && first_paragraph.color.b == 204,
        "first item paragraph uses its host styles and exact allocated frame");
  check(second_paragraph.owner->node->text == "Second fi" &&
        second_paragraph.origin_x_q1000 == std::int64_t(second->content_geometry.x) &&
        second_paragraph.content_width_q1000 == 90000 && second_paragraph.align == TextAlign::End &&
        second_paragraph.color.r == 0 && second_paragraph.color.g == 136 && second_paragraph.color.b == 0,
        "second item paragraph uses its host styles and exact allocated frame");
  check(first->rect.height >= 24 && second->rect.height >= 20 &&
        second->rect.y == first->rect.y,
        "row cross-size preserves natural text host heights");
  check_markers(*result.tree(), true);

  const LayoutBox* previous = result.tree();
  auto limited_options = options;
  limited_options.paragraph.shape_calls = 1;
  work = budget;
  check(LayoutEngine(styles).layout_contextual(*render, viewport.width, viewport.height,
            limited_options, work, result) == Status::limit && result.tree() == previous,
        "second item shape refusal rolls back the partially prepared row");
  const auto refuse = [&](const std::string& css, const std::string& html,
                          const char* message) {
    auto rejected_doc = HtmlParser().parse(html);
    StyleResolver rejected_styles(CssParser().parse(css));
    auto rejected_render = RenderTreeBuilder(rejected_styles).build(*rejected_doc);
    auto rejected_work = budget;
    check(LayoutEngine(rejected_styles).layout_contextual(
              *rejected_render, viewport.width, viewport.height, options, rejected_work, result) ==
              Status::invalid && result.tree() == previous,
          message);
  };
  const std::string items = "<body><main><div>First</div><div>Second</div></main></body>";
  refuse("main{display:flex;flex-direction:row;flex-wrap:wrap;width:160px}div{width:60px}", items,
         "wrapped row refusal preserves prior result");
  auto wrapped_doc = HtmlParser().parse(
      "<body><main id='wrapped'><div id='wide'>A</div><div id='short'>A</div>"
      "<div id='tall'>A</div></main></body>");
  StyleResolver wrapped_styles(CssParser().parse(
      "body{margin:0}main{display:flex;flex-direction:row;flex-wrap:wrap;width:54px;column-gap:4px;row-gap:5px;"
      "align-items:center;color:#000000}"
      "#wide{width:30px;min-width:0;flex:0 0 30px;font-size:16px;line-height:12px}"
      "#short{width:25px;min-width:0;flex:0 0 25px;font-size:16px;line-height:12px}"
      "#tall{width:20px;min-width:0;flex:0 0 20px;font-size:16px;line-height:20px}"));
  auto wrapped_render = RenderTreeBuilder(wrapped_styles).build(*wrapped_doc);
  work = budget;
  const auto wrapped_status = LayoutEngine(wrapped_styles).layout_contextual(
            *wrapped_render, viewport.width, viewport.height, options, work, result);
  check(wrapped_status == Status::ok &&
        result.paragraphs().size() == 3,
        "fixed-width contextual Flex items wrap with staged row geometry");
  const auto* wrapped = find_box_by_id(*result.tree(), "wrapped");
  const auto* wide = find_box_by_id(*result.tree(), "wide");
  const auto* short_item = find_box_by_id(*result.tree(), "short");
  const auto* tall = find_box_by_id(*result.tree(), "tall");
  check(wrapped && wide && short_item && tall && wrapped->rect.width == 54 &&
        wrapped->rect.height == 37 && wide->rect.x == wrapped->rect.x && wide->rect.y == wrapped->rect.y &&
        short_item->rect.y == wrapped->rect.y + 21 && tall->rect.y == wrapped->rect.y + 17 &&
        short_item->rect.x + short_item->rect.width + 4 == tall->rect.x,
        "row planner wraps before overflow, honors gaps, and centers within the second line");
  check(result.paragraphs()[0].origin_y_q1000 == std::int64_t(wide->content_geometry.y) &&
        result.paragraphs()[1].origin_y_q1000 == std::int64_t(short_item->content_geometry.y) &&
        result.paragraphs()[2].origin_y_q1000 == std::int64_t(tall->content_geometry.y),
        "paragraph baselines use the committed wrapped item positions");
  LayerCapture wrapped_capture;
  check(LayerTreeBuilder().build_contextual(result, wrapped_capture), "capture wrapped contextual rows");
  FrameBuffer expected_wrapped(viewport.width, viewport.height, {255, 255, 255, 255});
  for (const auto& paragraph : result.paragraphs()) paint_paragraph(paragraph, expected_wrapped);
  const auto wrapped_pixels = draw_capture(wrapped_capture);
  check(same_pixels(wrapped_pixels, expected_wrapped),
        "wrapped contextual pixels match the final-geometry paint oracle");
  const auto known_wrapped_ink = wrapped_pixels.pixel(4, 1);
  check(known_wrapped_ink.r == 0 && known_wrapped_ink.g == 0 && known_wrapped_ink.b == 0,
        "wrapped row paints the known Noto A pixel at its first-line origin");
  const auto check_justification = [&](const char* justify,
      std::int64_t first_x, std::int64_t second_x, std::int64_t third_x) {
    auto justified_doc = HtmlParser().parse(
        "<body><main id='row'><div id='first'>A</div><div id='second'>A</div>"
        "<div id='third'>A</div></main></body>");
    StyleResolver justified_styles(CssParser().parse(
        std::string("body{margin:0}main{display:flex;flex-direction:row;flex-wrap:wrap;width:60px;"
                    "column-gap:4px;justify-content:") +
        justify + "}"
        "#first{width:20px;min-width:0;flex:0 0 20px;font-size:16px;line-height:12px}"
        "#second{width:30px;min-width:0;flex:0 0 30px;font-size:16px;line-height:12px}"
        "#third{width:40px;min-width:0;flex:0 0 40px;font-size:16px;line-height:12px}"));
    auto justified_render = RenderTreeBuilder(justified_styles).build(*justified_doc);
    auto justified_work = budget;
    LayoutResult justified_result;
    check(LayoutEngine(justified_styles).layout_contextual(*justified_render, viewport.width,
            viewport.height, options, justified_work, justified_result) == Status::ok,
          "wrapped contextual row accepts a supported justify-content mode");
    const auto* first = find_box_by_id(*justified_result.tree(), "first");
    const auto* second = find_box_by_id(*justified_result.tree(), "second");
    const auto* third = find_box_by_id(*justified_result.tree(), "third");
    check(first && second && third && first->content_geometry.x == first_x &&
          second->content_geometry.x == second_x && third->content_geometry.x == third_x &&
          first->content_geometry.y == second->content_geometry.y &&
          third->content_geometry.y > second->content_geometry.y,
          "each wrapped line distributes free space independently using Q1000 geometry");
    LayerCapture capture;
    check(LayerTreeBuilder().build_contextual(justified_result, capture),
          "capture justified contextual row");
    FrameBuffer expected(viewport.width, viewport.height, {255, 255, 255, 255});
    for (const auto& paragraph : justified_result.paragraphs()) paint_paragraph(paragraph, expected);
    check(same_pixels(draw_capture(capture), expected),
          "justified wrapped-row pixels match the final-geometry paint oracle");
  };
  check_justification("start", 0, 24000, 0);
  check_justification("center", 3000, 27000, 10000);
  check_justification("end", 6000, 30000, 20000);
  check_justification("space-between", 0, 30000, 0);
  check_justification("space-around", 1500, 28500, 10000);
  check_justification("space-evenly", 2000, 28000, 10000);
  auto exact_doc = HtmlParser().parse(
      "<body><main id='exact'><div id='left'>A</div><div id='right'>A</div></main></body>");
  StyleResolver exact_styles(CssParser().parse(
      "body{margin:0}main{display:flex;flex-direction:row;flex-wrap:wrap;width:54px;column-gap:4px}"
      "#left,#right{width:25px;min-width:0;flex:0 0 25px;font-size:16px;line-height:12px}"));
  auto exact_render = RenderTreeBuilder(exact_styles).build(*exact_doc);
  work = budget;
  check(LayoutEngine(exact_styles).layout_contextual(
            *exact_render, viewport.width, viewport.height, options, work, result) == Status::ok,
        "exact-fit wrapped row lays out");
  const auto* exact = find_box_by_id(*result.tree(), "exact");
  const auto* left = find_box_by_id(*result.tree(), "left");
  const auto* right = find_box_by_id(*result.tree(), "right");
  check(exact && left && right && right->rect.x == left->rect.x + 29 &&
        right->rect.y == left->rect.y && exact->rect.height == 12,
        "exact-fit row includes its inter-item gap without wrapping");
  const auto* previous_exact = result.tree();
  previous = previous_exact;
  auto overfull_doc = HtmlParser().parse("<body><main><div>A</div></main></body>");
  StyleResolver overfull_styles(CssParser().parse(
      "body{margin:0}main{display:flex;flex-direction:row;flex-wrap:wrap;width:19px;justify-content:center}"
      "div{width:20px;min-width:0;flex:0 0 20px;font-size:16px;line-height:12px}"));
  auto overfull_render = RenderTreeBuilder(overfull_styles).build(*overfull_doc);
  work = budget;
  check(LayoutEngine(overfull_styles).layout_contextual(
            *overfull_render, viewport.width, viewport.height, options, work, result) == Status::invalid &&
        result.tree() == previous_exact,
        "overfull fixed item refusal preserves the previously committed result");
  refuse("main{display:flex;flex-direction:column;flex-wrap:nowrap;width:160px}div{height:24px}", items,
         "auto-height column refusal preserves prior result");
  refuse("main{display:flex;flex-flow:row nowrap;width:auto}div{width:60px}", items,
         "indefinite-width row refusal preserves prior result");
  refuse("main{display:flex;flex-flow:row nowrap;width:160px}div{width:60px}",
         "<body><main><div>First<span>Nested</span></div></main></body>",
         "nested flex item topology refusal preserves prior result");
  refuse("main{display:flex;flex-flow:row nowrap;width:160px}div{width:60px}",
         "<body><main><div>First</div><span>Anonymous</span></main></body>",
         "anonymous flex item refusal preserves prior result");

  auto column_doc = HtmlParser().parse(
      "<body><main id='column'><div id='top'>First line</div><div id='bottom'>Second</div></main></body>");
  StyleResolver column_styles(CssParser().parse(
      "body{margin:0}main{display:flex;flex-direction:column;flex-wrap:nowrap;width:120px;height:100px;"
      "row-gap:5px;align-items:stretch}"
      "#top{width:120px;height:30px;min-height:0;color:#0000cc;font-size:20px;line-height:24px;text-align:center}"
      "#bottom{width:120px;height:40px;min-height:0;color:#008800;font-size:16px;line-height:20px;text-align:end}"));
  auto column_render = RenderTreeBuilder(column_styles).build(*column_doc);
  work = budget;
  LayoutUsage column_usage;
  const auto column_status = LayoutEngine(column_styles).layout_contextual(
            *column_render, viewport.width, viewport.height, options, work, result, &column_usage);
  check(column_status == Status::ok &&
        result.paragraphs().size() == 2 && column_usage.paragraphs == 2 && column_usage.lines >= 2,
        "definite-size nowrap Flex column lays out contextual text");
  previous = result.tree();
  const auto* column = find_box_by_id(*result.tree(), "column");
  const auto* top = find_box_by_id(*result.tree(), "top");
  const auto* bottom = find_box_by_id(*result.tree(), "bottom");
  check(column && top && bottom && column->rect.width == 120 && column->rect.height == 100 &&
        top->rect.width == 120 && top->rect.height == 30 && top->rect.y == column->content_geometry.y / 1000 &&
        bottom->rect.width == 120 && bottom->rect.height == 40 && bottom->rect.y == top->rect.y + 35,
        "column allocator preserves definite item geometry and row gap");
  check(result.paragraphs()[0].origin_y_q1000 == std::int64_t(top->content_geometry.y) &&
        result.paragraphs()[1].origin_y_q1000 == std::int64_t(bottom->content_geometry.y) &&
        result.paragraphs()[0].content_width_q1000 == 120000 &&
        result.paragraphs()[1].content_width_q1000 == 120000,
        "column paragraphs use final stretched cross-axis width and item origin");
  LayerCapture column_capture;
  check(LayerTreeBuilder().build_contextual(result, column_capture) &&
        column_capture.layer_tree()->display_list.empty(), "capture definite-size text-only column");
  FrameBuffer expected_column(viewport.width, viewport.height, {255, 255, 255, 255});
  for (const auto& paragraph : result.paragraphs()) paint_paragraph(paragraph, expected_column);
  check(same_pixels(draw_capture(column_capture), expected_column),
        "captured column pixels match direct final-paragraph painting");
  const auto* previous_column = result.tree();
  refuse("main{display:flex;flex-direction:column;flex-wrap:nowrap;width:160px;height:100px}div{height:60px}", items,
         "overflowing definite column refusal preserves prior result");
  refuse("main{display:flex;flex-direction:column;flex-wrap:nowrap;width:160px;height:100px}div{height:40%}", items,
         "percentage item height refusal preserves prior result");
  check(result.tree() == previous_column, "column refusals preserve committed layout");
#else
  Fixture fixture(fixture_root, "regular", true);
  LayoutOptions options;
  auto work = budget;
  check(create_binding(fixture.as_inputs(), {}, work, options.binding) == Status::ok,
        "Flex-disabled binding");
  auto doc = HtmlParser().parse(
      "<body><main id='row'><div>First</div><div>Second</div></main></body>");
  StyleResolver styles(CssParser().parse(
      "body{margin:0}main{display:flex;flex-flow:row nowrap;width:160px}div{width:60px}"));
  auto render = RenderTreeBuilder(styles).build(*doc);
  const auto find_row = [&](const auto& self, RenderObject& object) -> RenderObject* {
    const auto id = object.node->attributes.find("id");
    if (id != object.node->attributes.end() && id->second == "row") return &object;
    for (auto& child : object.children) {
      if (auto* match = self(self, *child)) return match;
    }
    return nullptr;
  };
  auto* row = find_row(find_row, *render);
  check(row != nullptr, "Flex row retained in render tree");
  row->style.display = Display::Block;
  LayoutResult result;
  work = budget;
  check(LayoutEngine(styles).layout_contextual(
            *render, viewport.width, viewport.height, options, work, result) == Status::ok &&
        result.valid(), "block fallback establishes prior contextual output");
  const auto* previous = result.tree();
  row->style.display = Display::Flex;
  work = budget;
  check(LayoutEngine(styles).layout_contextual(
            *render, viewport.width, viewport.height, options, work, result) == Status::invalid &&
        result.tree() == previous,
        "Flex-disabled contextual layout refuses flex row and preserves prior result");
#endif
}

void check_centered_flex_line_heights(const char* fixture_root) {
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
  Fixture fixture(fixture_root, "regular", true);
  LayoutOptions options;
  auto work = budget;
  check(create_binding(fixture.as_inputs(), {}, work, options.binding) == Status::ok,
        "centered Flex binding");
  auto doc = HtmlParser().parse(
      "<body><main id='row'><div id='short'>A</div><div id='tall'>A</div></main></body>");
  StyleResolver styles(CssParser().parse(
      "body{margin:0}main{display:flex;flex-flow:row nowrap;width:80px;align-items:center}"
      "#short{width:20px;min-width:0;font-size:16px;line-height:24px;color:#000000}"
      "#tall{width:30px;min-width:0;font-size:24px;line-height:48px;color:#000000}"));
  auto render = RenderTreeBuilder(styles).build(*doc);
  LayoutResult result;
  work = budget;
  check(LayoutEngine(styles).layout_contextual(
            *render, viewport.width, viewport.height, options, work, result) == Status::ok &&
        result.paragraphs().size() == 2,
        "centered Flex row prepares contextual paragraphs");

  const auto* row = find_box_by_id(*result.tree(), "row");
  const auto* short_item = find_box_by_id(*result.tree(), "short");
  const auto* tall_item = find_box_by_id(*result.tree(), "tall");
  check(row && short_item && tall_item && row->rect.height == 48 &&
        short_item->rect.height == 24 && short_item->rect.y == 12 &&
        tall_item->rect.height == 48 && tall_item->rect.y == 0,
        "align-items center places the 24px line box halfway in the 48px row");

  // The packaged Noto Sans SC hhea metrics are ascent 1160 and descent 288
  // in 1000 units per em. CSS line-height supplies 24px/48px line boxes.
  // Descent is positive and extends below the baseline. Therefore paragraph
  // baselines are 12 + 24/2 + 16*(1160-288)/2000 = 30.976px and
  // 48/2 + 24*(1160-288)/2000 = 34.464px.
  const auto& short_line = result.paragraphs()[0].paragraph.lines().front();
  const auto& tall_line = result.paragraphs()[1].paragraph.lines().front();
  check(result.paragraphs()[0].origin_y_q1000 == 12000 &&
        result.paragraphs()[1].origin_y_q1000 == 0 &&
        short_line.baseline_y_q1000 == 18976 &&
        tall_line.baseline_y_q1000 == 34464,
        "CSS line-height and centered item geometry reach JFSP2 baselines");

  LayerCapture capture;
  check(LayerTreeBuilder().build_contextual(result, capture),
        "capture centered Flex paragraphs");
  const auto pixels = draw_capture(capture);
  // The packaged U+0041 bitmap has bearing (0,-12), size 10x12, and full
  // coverage at bitmap pixel (4,0). Raster scaling puts that pixel at x=4/y=19
  // for the 16px run and x+6/y=16 for the 24px run.
  const auto short_ink = pixels.pixel(4, 19);
  const auto tall_ink = pixels.pixel(tall_item->rect.x + 6, 16);
  check(short_ink.r == 0 && short_ink.g == 0 && short_ink.b == 0 &&
        tall_ink.r == 0 && tall_ink.g == 0 && tall_ink.b == 0,
        "JFSP2 baselines place independently known Noto A ink pixels");
#endif
}
}

int main(int argc, char** argv) {
  try {
    check(argc == 2, "fixture root");
    check_capture(argv[1]);
    check_text_hit_capture(argv[1]);
    check_nowrap_flex_row(argv[1]);
    check_centered_flex_line_heights(argv[1]);
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
