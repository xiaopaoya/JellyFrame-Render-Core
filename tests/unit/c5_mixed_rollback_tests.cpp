#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/text_system.h"
#include "../../src/computed_size_values.h"
#include "../../src/layout_mutation_journal.h"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace jellyframe;
using namespace jellyframe::detail;

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

const LayoutBox* find(const LayoutBox& box, const char* id) {
    if (box.node && box.node->attribute("id") == id) return &box;
    for (const auto& child : box.children)
        if (const auto* result = find(*child, id)) return result;
    return nullptr;
}

RenderObject* find(RenderObject& object, const char* id) {
    if (object.node && object.node->attribute("id") == id) return &object;
    for (const auto& child : object.children)
        if (auto* result = find(*child, id)) return result;
    return nullptr;
}

void compare_geometry(const LayoutBox& left, const LayoutBox& right) {
    check(left.rect.x == right.rect.x && left.rect.y == right.rect.y &&
          left.rect.width == right.rect.width && left.rect.height == right.rect.height,
          "late mixed refusal differs from ordinary replay geometry");
    check(left.content_geometry.x == right.content_geometry.x &&
          left.content_geometry.y == right.content_geometry.y &&
          left.content_geometry.width == right.content_geometry.width &&
          left.content_geometry.height() == right.content_geometry.height() &&
          left.content_geometry.height_definite() == right.content_geometry.height_definite(),
          "late mixed refusal differs from ordinary replay content geometry");
    check(left.text_layout_cache.valid == right.text_layout_cache.valid &&
          left.text_layout_cache.prepared_text == right.text_layout_cache.prepared_text &&
          left.text_layout_cache.source_text == right.text_layout_cache.source_text &&
          left.text_layout_cache.rendered_text == right.text_layout_cache.rendered_text &&
          left.text_layout_cache.lines == right.text_layout_cache.lines &&
          left.text_layout_cache.prepared_packet == right.text_layout_cache.prepared_packet &&
          left.text_layout_cache.prepared_lines.size() == right.text_layout_cache.prepared_lines.size(),
          "late mixed refusal differs from ordinary replay text cache");
    check(left.children.size() == right.children.size(), "fallback tree shape differs");
    for (std::size_t index = 0; index < left.children.size(); ++index)
        compare_geometry(*left.children[index], *right.children[index]);
}

void compare_layout_style(const LayoutBox& left, const LayoutBox& right) {
    const auto& a = left.style;
    const auto& b = right.style;
    check(a.computed_sizes.equals(b.computed_sizes),
          "late mixed refusal changes typed computed-size storage");
    check(a.width == b.width && a.height == b.height &&
          a.min_width == b.min_width && a.min_height == b.min_height &&
          a.max_width == b.max_width && a.max_height == b.max_height &&
          a.width_percent == b.width_percent && a.height_percent == b.height_percent &&
          a.min_width_percent == b.min_width_percent && a.min_height_percent == b.min_height_percent &&
          a.max_width_percent == b.max_width_percent && a.max_height_percent == b.max_height_percent,
          "late mixed refusal changes dimension compatibility fields");
    check(a.flex_basis == b.flex_basis && a.flex_basis_kind == b.flex_basis_kind &&
          a.flex_grow == b.flex_grow && a.flex_shrink == b.flex_shrink &&
          a.flex_direction == b.flex_direction && a.flex_wrap == b.flex_wrap &&
          a.table_properties == b.table_properties &&
          a.grid_template_column_count == b.grid_template_column_count &&
          a.grid_template_column_widths == b.grid_template_column_widths &&
          a.grid_template_row_heights == b.grid_template_row_heights &&
          a.grid_template_row_count == b.grid_template_row_count &&
          a.grid_column_span == b.grid_column_span && a.grid_row_span == b.grid_row_span,
          "late mixed refusal changes flex/grid/table style provenance");
    check(left.children.size() == right.children.size(), "style comparison tree shape differs");
    for (std::size_t index = 0; index < left.children.size(); ++index)
        compare_layout_style(*left.children[index], *right.children[index]);
}

struct Scene {
    std::unique_ptr<Node> document;
    LayoutBoxPtr boxes;
    LayoutIntrinsicQueryStatistics stats;
};

Scene layout_mixed_wrapped_row(std::size_t max_work) {
    Scene scene;
    scene.document = HtmlParser{}.parse(
        "<body><div id='outside'><div id='before'></div>"
        "<main id='flex'><div id='first'></div>"
        "<div id='second'></div></main><div id='after'></div></div></body>");
    check(scene.document != nullptr, "mixed rollback fixture parses");
    const char* css = "*{margin:0;padding:0;border:0}"
        "#outside{width:140px}#before,#after{height:3px}"
        "#flex{display:flex;flex-direction:row;flex-wrap:wrap;width:100px;align-items:flex-start}"
        "#first,#second{display:block;flex:0 0 auto;height:10px}";
    StyleResolver resolver(CssParser{}.parse(css));
    auto tree = RenderTreeBuilder(resolver).build(*scene.document);
    auto* first = find(*tree, "first");
    auto* second = find(*tree, "second");
    check(first && second &&
          apply_computed_length_percentage(first->style, SizeProperty::FlexBasis, 20, 30) &&
          apply_computed_length_percentage(second->style, SizeProperty::FlexBasis, 60, 0),
          "mixed FlexBasis publishes to both wrapped-row items");
    LayoutEngineOptions options;
    TextSystem empty_text_system;
    options.text_system = &empty_text_system;
    options.intrinsic_query_limits.max_work = max_work;
    options.intrinsic_query_statistics = &scene.stats;
    scene.boxes = LayoutEngine(resolver, {}, options).layout(*tree, 320, 240);
    return scene;
}

void wrapped_row_mixed_basis_late_refusal_replays_after_journal_rollback() {
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
    auto complete = layout_mixed_wrapped_row(65536);
    check(complete.boxes && complete.stats.work > 1,
          "mixed wrapped-row precise plan performs bounded work");

    auto late = layout_mixed_wrapped_row(complete.stats.work - 1);
    auto ordinary = layout_mixed_wrapped_row(0);
    check(late.stats.work == complete.stats.work - 1 && late.stats.resource_refusals > 0,
          "one-unit-short work allowance refuses after precise wrapped-row work");
    compare_geometry(*late.boxes, *ordinary.boxes);

    const auto* first = find(*late.boxes, "first");
    const auto* second = find(*late.boxes, "second");
    SizeKind first_kind = SizeKind::Pixels;
    std::int64_t first_length = 0;
    std::int64_t first_percentage = 0;
    check(first && second && first->style.computed_sizes.read_typed(
              SizeProperty::FlexBasis, first_kind, first_length, first_percentage) &&
          first_kind == SizeKind::LengthPercentage && first_length == 20000 &&
          first_percentage == 30000 && first->style.flex_basis == -1 &&
          first->style.flex_basis_kind == FlexBasisKind::Length,
          "late refusal retains authored typed FlexBasis and compatibility state");
    check(first->rect.y == 3 && second->rect.y > first->rect.y,
          "mixed fixture reaches the wrapped line-publication path");
    compare_layout_style(*late.boxes, *ordinary.boxes);
#endif
}

void journal_restores_every_box_in_the_captured_consumer_subtree() {
    LayoutBox consumer;
    consumer.rect = {1, 2, 3, 4};
    consumer.content_geometry = {1000, 2000, 3000, 4000, true};
    consumer.text_layout_cache.valid = true;
    consumer.text_layout_cache.source_text = "consumer source";
    consumer.text_layout_cache.rendered_text = "consumer rendered";
    consumer.text_layout_cache.lines = {"consumer line"};
    consumer.text_layout_cache.text_system_generation = 17;
    consumer.text_layout_cache.prepared_text = true;
    consumer.text_layout_cache.prepared_packet = "consumer packet";
    consumer.text_layout_cache.prepared_lines.push_back({"consumer prepared line", 11, 12, 13});
    consumer.text_layout_cache.prepared_origin_x = 14;
    consumer.text_layout_cache.prepared_baseline_y = 15;
    consumer.text_layout_cache.rect_x = 16;
    consumer.text_layout_cache.rect_y = 17;
    consumer.text_layout_cache.rect_width = 18;
    consumer.text_layout_cache.rect_height = 19;

    auto item = LayoutBoxPtr(new LayoutBox, LayoutBoxDeleter{false});
    item->rect = {5, 6, 7, 8};
    item->content_geometry = {5000, 6000, 7000, 8000, false};
    item->text_layout_cache.valid = true;
    item->text_layout_cache.source_text = "item source";
    item->text_layout_cache.prepared_packet = "item packet";
    item->text_layout_cache.prepared_lines.push_back({"item prepared line", 21, 22, 23});
    item->text_layout_cache.rect_width = 28;

    auto descendant = LayoutBoxPtr(new LayoutBox, LayoutBoxDeleter{false});
    descendant->rect = {9, 10, 11, 12};
    descendant->content_geometry = {9000, 10000, 11000, 12000, true};
    descendant->text_layout_cache.valid = true;
    descendant->text_layout_cache.source_text = "descendant source";
    descendant->text_layout_cache.lines = {"descendant line one", "descendant line two"};
    descendant->text_layout_cache.prepared_text = true;
    descendant->text_layout_cache.prepared_packet = "descendant packet";
    descendant->text_layout_cache.prepared_lines.push_back({"descendant prepared line", 31, 32, 33});
    descendant->text_layout_cache.prepared_origin_x = 34;
    descendant->text_layout_cache.prepared_baseline_y = 35;
    descendant->text_layout_cache.rect_x = 36;
    descendant->text_layout_cache.rect_y = 37;
    descendant->text_layout_cache.rect_width = 38;
    descendant->text_layout_cache.rect_height = 39;
    item->children.push_back(std::move(descendant));
    consumer.children.push_back(std::move(item));

    LayoutMutationJournal journal;
    check(journal.capture(consumer, 3) == LayoutMutationJournalStatus::Ready && journal.size() == 3,
          "consumer journal captures root, item and nested descendant");
    for (LayoutBox* box : {&consumer, consumer.children[0].get(), consumer.children[0]->children[0].get()}) {
        box->rect = {31, 32, 33, 34};
        box->content_geometry = {31000, 32000, 33000, 34000, false};
        box->text_layout_cache.valid = false;
        box->text_layout_cache.source_text = "mutated";
        box->text_layout_cache.lines.clear();
        box->text_layout_cache.prepared_lines.clear();
        box->text_layout_cache.prepared_text = false;
        box->text_layout_cache.prepared_packet = "mutated packet";
        box->text_layout_cache.rect_width = 0;
    }
    journal.rollback();

    check(consumer.rect.x == 1 && consumer.rect.width == 3 &&
          consumer.content_geometry.x == 1000 && consumer.content_geometry.width == 3000 &&
          consumer.content_geometry.height_definite() && consumer.text_layout_cache.valid &&
          consumer.text_layout_cache.source_text == "consumer source" &&
          consumer.text_layout_cache.lines == std::vector<std::string>{"consumer line"} &&
          consumer.text_layout_cache.text_system_generation == 17 &&
          consumer.text_layout_cache.prepared_packet == "consumer packet" &&
          consumer.text_layout_cache.prepared_lines.size() == 1 &&
          consumer.text_layout_cache.prepared_lines[0].packet == "consumer prepared line" &&
          consumer.text_layout_cache.prepared_origin_x == 14 &&
          consumer.text_layout_cache.prepared_baseline_y == 15 &&
          consumer.text_layout_cache.rect_x == 16 && consumer.text_layout_cache.rect_y == 17 &&
          consumer.text_layout_cache.rect_width == 18 && consumer.text_layout_cache.rect_height == 19,
          "journal restores consumer container geometry and cache");
    const auto& restored_item = *consumer.children[0];
    check(restored_item.rect.x == 5 && restored_item.content_geometry.x == 5000 &&
          restored_item.text_layout_cache.valid && restored_item.text_layout_cache.source_text == "item source" &&
          restored_item.text_layout_cache.prepared_packet == "item packet" &&
          restored_item.text_layout_cache.prepared_lines.size() == 1 &&
          restored_item.text_layout_cache.prepared_lines[0].packet == "item prepared line" &&
          restored_item.text_layout_cache.rect_width == 28,
          "journal restores mixed Flex item geometry and cache");
    const auto& restored_descendant = *restored_item.children[0];
    check(restored_descendant.rect.x == 9 && restored_descendant.rect.width == 11 &&
          restored_descendant.content_geometry.x == 9000 &&
          restored_descendant.content_geometry.height() == 12000 &&
          restored_descendant.content_geometry.height_definite() &&
          restored_descendant.text_layout_cache.valid &&
          restored_descendant.text_layout_cache.source_text == "descendant source" &&
          restored_descendant.text_layout_cache.lines ==
              std::vector<std::string>{"descendant line one", "descendant line two"} &&
          restored_descendant.text_layout_cache.prepared_text &&
          restored_descendant.text_layout_cache.prepared_packet == "descendant packet" &&
          restored_descendant.text_layout_cache.prepared_lines.size() == 1 &&
          restored_descendant.text_layout_cache.prepared_lines[0].packet == "descendant prepared line" &&
          restored_descendant.text_layout_cache.prepared_origin_x == 34 &&
          restored_descendant.text_layout_cache.prepared_baseline_y == 35 &&
          restored_descendant.text_layout_cache.rect_x == 36 && restored_descendant.text_layout_cache.rect_y == 37 &&
          restored_descendant.text_layout_cache.rect_width == 38 &&
          restored_descendant.text_layout_cache.rect_height == 39,
          "journal restores nested consumer descendants and prepared cache state");
}

} // namespace

int main() {
    try {
        wrapped_row_mixed_basis_late_refusal_replays_after_journal_rollback();
        journal_restores_every_box_in_the_captured_consumer_subtree();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "C5 mixed rollback tests passed\n";
}
