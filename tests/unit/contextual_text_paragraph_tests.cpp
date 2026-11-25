#include "render_core/contextual_text_paragraph.h"
#include "render_core/software_renderer.h"
#include "shaped_text_packet.h"
#include "contextual_text_fixture.h"
#include <iostream>
using namespace jellyframe;
using namespace jellyframe::contextual_text;
using namespace shaped_binding_fixture;
namespace {
constexpr std::uint64_t budget = 32U * 1024U * 1024U;
std::vector<std::string> line_text(const Paragraph& paragraph) {
  std::vector<std::string> result;
  for (const auto& line : paragraph.lines())
    result.emplace_back(paragraph.text().substr(line.source_begin, line.source_end - line.source_begin));
  return result;
}
bool same_pixels(const FrameBuffer& a, const FrameBuffer& b) {
  return a.pixels.size() == b.pixels.size() && std::equal(a.pixels.begin(), a.pixels.end(), b.pixels.begin(),
    [](Color x, Color y) { return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a; });
}
void validate_lines(const Paragraph& p) {
  std::size_t consumed = 0;
  for (const auto& line : p.lines()) {
    check(line.source_begin == consumed && line.source_begin < line.source_end &&
          line.source_end <= line.consumed_end && line.consumed_end <= p.text().size(), "coverage");
    std::uint64_t work = budget;
    check(validate_packet(p.binding(), line.packet, {}, work) == Status::ok, "line packet binding");
    shaped_packet::View view;
    check(shaped_packet::decode(line.packet, {}, work, view) == shaped_packet::Status::ok &&
          view.source() == p.text().substr(line.source_begin, line.source_end - line.source_begin), "exact final source");
    consumed = line.consumed_end;
  }
  check(consumed == p.text().size(), "complete coverage");
}
}
int main(int argc, char** argv) {
  try {
    check(argc == 2, "fixture root");
    Fixture fixture(argv[1], "regular", true);
    auto input = fixture.as_inputs();
    Binding binding;
    std::uint64_t work = budget;
    check(create_binding(input, {}, work, binding) == Status::ok, "binding");
    std::string packet; Metrics a;
    work = budget;
    check(prepare(binding, 16500, "A", {}, work, packet, a) == Status::ok, "fractional A");
    ParagraphOptions options;
    options.size_q1000 = 16500;
    options.width_q1000 = a.advance_x;
    options.line_height_q1000 = 23001;
    Paragraph paragraph;
    ParagraphUsage usage;
    work = budget;
    check(layout_paragraph(binding, " \tA \n A\rA  ", options, {}, work, paragraph, &usage) == Status::ok, "normal wrap");
    check(paragraph.source() == " \tA \n A\rA  " && paragraph.text() == "A A A", "normal whitespace");
    check(line_text(paragraph) == std::vector<std::string>({"A", "A", "A"}), "three lines");
    check(paragraph.width_q1000() == a.advance_x && paragraph.height_q1000() == 69003, "Q1000 geometry");
    check(paragraph.lines()[0].baseline_y_q1000 == 18695 &&
          paragraph.lines()[1].baseline_y_q1000 - paragraph.lines()[0].baseline_y_q1000 == 23001,
          "explicit line height applies negative half-leading to the Noto font extent");
    check(usage.shape_calls > paragraph.lines().size() + 1 && usage.work == budget - work && usage.result_bytes > 0, "charged candidate work");
    validate_lines(paragraph);
    auto retained = paragraph;
    const auto prior_text = paragraph.text().data();
    auto refuses = [&](ParagraphLimits limits, std::uint64_t allowance) {
      ParagraphUsage reported;
      work = allowance;
      const auto status = layout_paragraph(binding, "A A A", options, limits, work, paragraph, &reported);
      check(status != Status::ok && paragraph.text().data() == prior_text &&
            line_text(paragraph) == line_text(retained) && reported.work == allowance - work, "refusal preserves result and charges");
      return reported;
    };
    ParagraphLimits tiny;
    tiny.lines = 1;
    auto line_usage = refuses(tiny, budget);
    check(line_usage.shape_calls > 1 && line_usage.work > 0, "line-limit attempts charged");
    tiny = {}; tiny.shape_calls = 1;
    auto call_usage = refuses(tiny, budget);
    check(call_usage.shape_calls == 1 && call_usage.work > 0, "call-limit charged");
    tiny = {}; tiny.result_bytes = 1; refuses(tiny, budget);
    tiny = {}; tiny.shaping.packet_bytes = 80;
    check(refuses(tiny, budget).shape_calls == 1 && work == 0, "failed primitive conservatively charged");
    refuses({}, 1);
    work = budget;
    check(layout_paragraph(binding, std::string_view("A\x01", 2), options, {}, work, paragraph) == Status::invalid &&
          paragraph.text().data() == prior_text, "unsupported control");
    work = budget;
    check(layout_paragraph(binding, std::string_view("\xc0\x80", 2), options, {}, work, paragraph) != Status::ok &&
          paragraph.text().data() == prior_text, "invalid UTF8");
    options.emergency_wrap = true;
    work = budget;
    check(layout_paragraph(binding, "AAAA", options, {}, work, paragraph) == Status::ok &&
          line_text(paragraph) == std::vector<std::string>({"A", "A", "A", "A"}), "emergency clusters");
    validate_lines(paragraph);
    options.width_q1000 = 0;
    work = budget;
    check(layout_paragraph(binding, "A A", options, {}, work, paragraph) == Status::ok &&
          line_text(paragraph) == std::vector<std::string>({"A", "A"}), "oversized emergency consumes following space");
    validate_lines(paragraph);
    options.width_q1000 = a.advance_x;
    options.emergency_wrap = false;
    work = budget;
    check(layout_paragraph(binding, "AAAA", options, {}, work, paragraph) == Status::ok &&
          paragraph.lines().size() == 1 && paragraph.width_q1000() > options.width_q1000, "unbreakable overflow");
    // Explicit hyphen breaks are supported, but a hyphen before slash
    // keeps the punctuation sequence unbreakable.
    std::string hyphen_packet; Metrics hyphen_metrics;
    work = budget;
    check(prepare(binding, options.size_q1000, "A-", {}, work, hyphen_packet, hyphen_metrics) == Status::ok,
          "hyphen metrics");
    options.width_q1000 = hyphen_metrics.advance_x;
    work = budget;
    check(layout_paragraph(binding, "A-A", options, {}, work, paragraph) == Status::ok &&
          line_text(paragraph) == std::vector<std::string>({"A-", "A"}), "hyphen soft break");
    work = budget;
    check(layout_paragraph(binding, "A-/A", options, {}, work, paragraph) == Status::ok &&
          paragraph.lines().size() == 1, "hyphen slash unbreakable");
    options.width_q1000 = a.advance_x;
    options.nowrap = true;
    work = budget;
    check(layout_paragraph(binding, "A A A", options, {}, work, paragraph) == Status::ok && paragraph.lines().size() == 1, "nowrap");
    options.nowrap = false;
    options.width_q1000 = 1000000;
    options.line_height_q1000 = 0;
    work = budget;
    check(layout_paragraph(binding, "A\xCC\x81", options, {}, work, paragraph) == Status::ok, "normalizing source cluster");
    validate_lines(paragraph);
    work = budget;
    Metrics final_metrics;
    std::string final_packet;
    check(prepare(binding, options.size_q1000, "A\xCC\x81", {}, work, final_packet, final_metrics) == Status::ok &&
          paragraph.lines()[0].packet == final_packet &&
          paragraph.lines()[0].metrics.advance_x == final_metrics.advance_x, "final line is direct shaping output");
    // Noto Sans SC at 16.5 px has 19.140 px ascent and 4.752 px descent.
    check(final_metrics.ascent == 19140 && final_metrics.descent == 4752 &&
          paragraph.height_q1000() == 23892 && paragraph.lines()[0].baseline_y_q1000 == 19140,
          "natural line height includes the positive descent");
    // A combining sequence is a single source cluster: emergency wrapping
    // must not split between A and its combining acute accent, even at width0.
    options.width_q1000 = 0;
    options.emergency_wrap = true;
    work = budget;
    check(layout_paragraph(binding, "A\xCC\x81" "A", options, {}, work, paragraph) == Status::ok &&
          paragraph.lines().size() == 2 && paragraph.lines()[0].source_end == 3,
          "combining cluster cannot split");
    validate_lines(paragraph);
    work = budget;
    check(layout_paragraph(binding, "fifi", options, {}, work, paragraph) == Status::ok &&
          paragraph.lines().size() == 2 && paragraph.lines()[0].source_end == 2 &&
          paragraph.lines()[1].source_begin == 2, "ligature cluster cannot split");
    validate_lines(paragraph);
    // Chinese source intervals are UTF8 byte ranges, not scalar indices.
    options.width_q1000 = 0;
    work = budget;
    check(layout_paragraph(binding, "\xE4\xB8\xAD\xE4\xB8\xAD", options, {}, work, paragraph) == Status::ok &&
          paragraph.lines().size() == 2 && paragraph.lines()[0].source_end == 3 &&
          paragraph.lines()[1].source_begin == 3, "multibyte cluster wrapping");
    validate_lines(paragraph);
    // Retained packets own font state after original wrapper and source bytes disappear.
    auto owned = paragraph;
    binding = {};
    fixture = Fixture(argv[1], "bold");
    validate_lines(owned);
    FrameBuffer target(64, 64, Color{255,255,255,255}), before = target;
    for (const auto& line : owned.lines()) {
      work = budget;
      check(paint(owned.binding(), line.packet, target, 4000, line.baseline_y_q1000,
            {0,0,64,64}, {0,0,0,255}, {}, work) == Status::ok, "paragraph final paint");
    }
    check(!same_pixels(target, before), "final-line paint visible");
    auto before_refusal = paragraph.text().data();
    for (const std::string source : {std::string("\xD7\x90"), std::string("A\xE2\x80\xA8" "A")}) {
      work = budget;
      check(layout_paragraph(owned.binding(), source, options, {}, work, paragraph) == Status::invalid &&
            paragraph.text().data() == before_refusal, "unsupported bidi/separator");
    }
    options.size_q1000 = 0;
    work = budget;
    check(layout_paragraph(owned.binding(), "A", options, {}, work, paragraph) == Status::invalid && work == budget, "invalid options");
    options.size_q1000 = 16000;
    work = budget;
    check(layout_paragraph(owned.binding(), " \t\n", options, {}, work, paragraph) == Status::ok &&
          paragraph.valid() && paragraph.lines().empty() && paragraph.height_q1000() == 0, "empty paragraph");
    // Source can borrow the prior immutable result and publication keeps it safe.
    auto source = retained.source();
    options.width_q1000 = 1000000;
    work = budget;
    check(layout_paragraph(retained.binding(), source, options, {}, work, retained) == Status::ok && retained.text() == "A A A", "alias publication");
    std::cout << "shaped paragraph tests passed\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
