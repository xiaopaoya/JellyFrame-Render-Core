#include "render_core/contextual_text.h"
#include "render_core/shaped_packet_v2.h"
#include "render_core/software_renderer.h"
#include "contextual_text_fixture.h"
#include <climits>
#include <iostream>
using namespace jellyframe;
using namespace jellyframe::contextual_text;
using namespace shaped_binding_fixture;
namespace packet = jellyframe::shaped_packet_v2;
namespace {
constexpr std::uint64_t budget = 32U * 1024U * 1024U;
bool equal(const FrameBuffer& a, const FrameBuffer& b) {
  return a.width == b.width && a.height == b.height && a.pixels.size() == b.pixels.size() &&
    std::equal(a.pixels.begin(), a.pixels.end(), b.pixels.begin(), [](Color x, Color y) {
      return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a;
    });
}
std::int64_t rounded(std::int64_t n, std::int64_t d) {
  const auto q = n / d, r = n % d;
  return q + (r * 2 >= d ? 1 : r * 2 <= -d ? -1 : 0);
}
shaping_catalog::View catalog_for(const Fixture& f) {
  shaping_catalog::Identity id;
  auto in = f.as_inputs();
  std::copy(in.source_identity.begin(), in.source_identity.end(), id.shaping.source);
  std::copy(in.profile_identity.begin(), in.profile_identity.end(), id.shaping.profile);
  id.shaping.glyph_count = in.shaping_glyph_count;
  id.trusted_companion = f.companion; id.companion_sha256 = in.companion_identity;
  id.catalog_sha256 = in.catalog_identity; id.raster_size = in.raster_size; id.weight = in.weight;
  shaping_catalog::View result; auto work = budget;
  check(shaping_catalog::bind(f.catalog, id, {}, work, result) == shaping_catalog::Status::ok, "reference catalog");
  return result;
}
// Destination-wide independent reference: no production painter or placement
// helper. Decode the authenticated packet, then use rational quotient/remainder
// geometry and direct packed-raster reads against each framebuffer coordinate.
void reference(const shaping_catalog::View& catalog, std::string_view bytes,
               FrameBuffer& target, int bx, int by, Rect clip, Color color) {
  packet::View view; auto work = budget;
  check(packet::decode(bytes, {}, work, view) == packet::Status::ok, "reference packet");
  const auto size = view.header().size_q1000;
  const auto divisor = std::int64_t(catalog.metadata().raster_pixel_size) * 1000;
  for (std::size_t i = 0; i < view.glyph_count(); ++i) {
    packet::Glyph p; FontGlyphView g;
    check(view.glyph(i, p) && catalog.find_gid(static_cast<std::uint16_t>(p.gid), g), "reference raster");
    const auto left = rounded(std::int64_t(bx) + p.x + p.offset_x, 1000) + rounded(std::int64_t(g.bearing_x) * size, divisor);
    const auto top = rounded(std::int64_t(by) + p.y + p.offset_y, 1000) + rounded(std::int64_t(g.bearing_y) * size, divisor);
    const auto width = g.width ? std::max<std::int64_t>(1, rounded(std::int64_t(g.width) * size, divisor)) : 0;
    const auto height = g.height ? std::max<std::int64_t>(1, rounded(std::int64_t(g.height) * size, divisor)) : 0;
    for (int y = 0; y < target.height; ++y) for (int x = 0; x < target.width; ++x) {
      if (x < clip.x || y < clip.y || std::int64_t(x) >= std::int64_t(clip.x) + clip.width ||
          std::int64_t(y) >= std::int64_t(clip.y) + clip.height || x < left || y < top ||
          x >= left + width || y >= top + height) continue;
      const auto sx = std::size_t((x - left) * g.width / width);
      const auto sy = std::size_t((y - top) * g.height / height);
      check(sx < g.width && sy < g.height, "reference coverage bounds");
      const unsigned packed = g.coverage[sy * g.row_stride + sx / 2];
      const unsigned coverage = sx % 2 ? packed % 16 : packed / 16;
      const unsigned alpha = (unsigned(color.a) * coverage + 7) / 15;
      auto& pixel = target.pixel(x, y);
      pixel.r = std::uint8_t((color.r * alpha + pixel.r * (255 - alpha) + 127) / 255);
      pixel.g = std::uint8_t((color.g * alpha + pixel.g * (255 - alpha) + 127) / 255);
      pixel.b = std::uint8_t((color.b * alpha + pixel.b * (255 - alpha) + 127) / 255);
      pixel.a = std::uint8_t(alpha + (pixel.a * (255 - alpha) + 127) / 255);
    }
  }
}

void display_list_owns_bindings_and_preflights_all_commands(const std::string& root) {
  const Color clear{11, 23, 31, 255}, ink{203, 41, 137, 219};
  Fixture fixture(root, "regular", true);
  Binding binding;
  auto work = budget;
  check(create_binding(fixture.as_inputs(), {}, work, binding) == Status::ok,
        "display-list binding");
  std::string packet_bytes;
  Metrics metrics;
  work = budget;
  check(prepare(binding, 16000, "AV", {}, work, packet_bytes, metrics) == Status::ok,
        "display-list shaped packet");

  ContextualTextDisplayCommand first;
  first.rect = {0, 0, 64, 48};
  first.color = ink;
  first.baseline_x_q1000 = 9500;
  first.baseline_y_q1000 = 26000;
  first.packet = packet_bytes;
  first.binding = binding;
  ContextualTextDisplayList commands{first};
  SoftwareRasterizer rasterizer;
  FrameBuffer actual(64, 48, clear), expected = actual;
  work = budget;
  check(paint(binding, packet_bytes, expected, 9500, 26000, {0, 0, 64, 48},
              ink, {}, work) == Status::ok,
        "display-list reference paint");
  rasterizer.rasterize(commands, actual, {0, 0, 64, 48});
  check(equal(actual, expected), "typed display-list paints JFSP2 packet");

  SoftwareRasterizerScratch scratch;
  FrameBuffer reused(64, 48, clear), reused_expected = reused;
  rasterizer.rasterize(commands, reused, {0, 0, 64, 48}, 0, 0, &scratch);
  check(equal(reused, expected) && equal(scratch.transformed_surface, reused_expected),
        "scratch-backed contextual display paint is atomic");
  const auto retained_scratch_capacity = scratch.transformed_surface.pixels.capacity();
  for (int frame = 0; frame < 3; ++frame) {
    reused.clear(clear);
    reused_expected = reused;
    rasterizer.rasterize(commands, reused, {0, 0, 64, 48}, 0, 0, &scratch);
    check(equal(reused, expected) &&
              scratch.transformed_surface.pixels.capacity() == retained_scratch_capacity,
          "repeated contextual paint reuses scratch capacity");
  }
  const auto scratch_before_invalid = scratch.transformed_surface;
  FrameBuffer invalid_frame(64, 48, clear);
  invalid_frame.pixels.pop_back();
  const auto invalid_frame_before = invalid_frame;
  rasterizer.rasterize(commands, invalid_frame, {0, 0, 64, 48}, 0, 0, &scratch);
  check(equal(invalid_frame, invalid_frame_before) &&
            equal(scratch.transformed_surface, scratch_before_invalid),
        "invalid framebuffer preserves target and reusable scratch");

  // The retained command keeps the immutable resource snapshot alive after
  // the producer's binding and source buffers have gone away.
  binding = {};
  fixture.font.clear(); fixture.catalog.clear(); fixture.companion.clear();
  fixture.tables.clear(); fixture.eligibility.clear();
  FrameBuffer retained(64, 48, clear);
  rasterizer.rasterize(commands, retained, {0, 0, 64, 48});
  check(equal(retained, expected), "display command retains binding resources");

  // A malformed later packet is found before the valid first command can
  // change the caller framebuffer.
  commands.push_back(first);
  commands.back().packet.back() ^= 1;
  FrameBuffer refused(64, 48, clear), before = refused;
  rasterizer.rasterize(commands, refused, {0, 0, 64, 48});
  check(equal(refused, before), "all-command packet preflight is atomic");

  commands.pop_back();
  commands.front().baseline_x_q1000 = INT32_MAX;
  refused = before;
  rasterizer.rasterize(commands, refused, {0, 0, 64, 48}, 1, 0);
  check(equal(refused, before), "translated baseline refusal is atomic");
}
}
int main(int argc, char** argv) {
  try {
    check(argc == 2, "fixture root");
    const Color clear{13, 17, 19, 147}, ink{217, 31, 151, 193};
    unsigned cases = 0;
    for (const auto face : {"regular", "medium", "bold"}) {
      Fixture fixture(argv[1], face, true); auto catalog = catalog_for(fixture);
      Binding binding; auto work = budget;
      check(create_binding(fixture.as_inputs(), {}, work, binding) == Status::ok, "complete-font owner");
      for (int size : {10000, 16000, 17750, 24000}) for (int bx : {-12750, 4250})
      for (int by : {17500, 39750}) for (Rect clip : {Rect{0,0,96,64}, Rect{9,7,33,29}, Rect{100,100,5,5}}) {
        std::string bytes; Metrics metrics; work = budget;
        check(prepare(binding, size, u8"office AV 中 e\u0301", {}, work, bytes, metrics) == Status::ok, "actual run");
        FrameBuffer actual(96,64,clear), expected = actual, tiled = actual;
        reference(catalog, bytes, expected, bx, by, clip, ink);
        work = budget;
        check(paint(binding, bytes, actual, bx, by, clip, ink, {}, work) == Status::ok && equal(actual,expected), "independent coverage/geometry/alpha");
        for (int y=0; y<64; y+=13) for (int x=0; x<96; x+=17) {
          const int l=std::max(x,clip.x), t=std::max(y,clip.y);
          const int r=std::min(x+17,clip.x+clip.width), b=std::min(y+13,clip.y+clip.height);
          work=budget;
          check(paint(binding,bytes,tiled,bx,by,{l,t,std::max(0,r-l),std::max(0,b-t)},ink,{},work)==Status::ok,"tile paint");
        }
        check(equal(actual,tiled),"full/tiled equality"); ++cases;
      }
      std::string bytes; Metrics metrics; work=budget;
      check(prepare(binding,16000,"A",{},work,bytes,metrics)==Status::ok,"single glyph");
      FrameBuffer target(32,32,clear), before=target;
      const auto refuse=[&](std::string_view packet_bytes, Limits limits, std::uint64_t allowance, Rect clip) {
        work=allowance;
        check(paint(binding,packet_bytes,target,0,16000,clip,ink,limits,work)!=Status::ok &&
              work==allowance && equal(target,before),"transactional paint refusal");
      };
      refuse(bytes,{},0,{0,0,32,32}); refuse(bytes,{},budget,{0,0,-1,32});
      for (auto offset : {28,32,40,48}) { auto bad=bytes; bad[offset]^=1; refuse(bad,{},budget,{0,0,32,32}); }
      auto bad=bytes; bad.pop_back(); refuse(bad,{},budget,{0,0,32,32});
      Limits tiny; tiny.packet_bytes=1; refuse(bytes,tiny,budget,{0,0,32,32});
      // The successful exact work boundary must succeed; one unit less rejects
      // even when insufficient capacity occurs only at raster preflight.
      work=budget; check(paint(binding,bytes,target,0,16000,{0,0,32,32},ink,{},work)==Status::ok,"work baseline");
      const auto cost=budget-work; target=before;
      refuse(bytes,{},cost-1,{0,0,32,32});
      work=cost; check(paint(binding,bytes,target,0,16000,{0,0,32,32},ink,{},work)==Status::ok && work==0,"exact work");
      target=before;
      // Structurally valid extreme scaling stays defined and clipped; it is not
      // authenticated source geometry, as documented by validate_packet.
      packet::View view; work=budget;
      check(packet::decode(bytes,{},work,view)==packet::Status::ok,"extreme input");
      auto header=view.header(); header.size_q1000=INT_MAX;
      packet::Cluster cluster; packet::Glyph glyph; check(view.cluster(0,cluster)&&view.glyph(0,glyph),"extreme records");
      std::string huge; work=budget;
      check(packet::encode(header,view.source(),&cluster,1,&glyph,1,{},work,huge)==packet::Status::ok,"extreme packet");
      reference(catalog,huge,before,INT_MIN,INT_MAX,{0,0,32,32},ink);
      work=budget; check(paint(binding,huge,target,INT_MIN,INT_MAX,{0,0,32,32},ink,{},work)==Status::ok&&equal(target,before),"extreme scale coordinates");
      // A valid packet placed in the target cannot be reread after writes.
      target=FrameBuffer(32,32,clear); std::memcpy(target.pixels.data(),bytes.data(),bytes.size()); before=target;
      refuse(std::string_view(reinterpret_cast<const char*>(target.pixels.data()),bytes.size()),{},budget,{0,0,32,32});
      // Invalid framebuffer storage is rejected before writing.
      target=FrameBuffer(32,32,clear); target.pixels.pop_back(); before=target;
      refuse(bytes,{},budget,{0,0,32,32});
      auto retained=binding; binding={};
      fixture.font.clear(); fixture.catalog.clear(); fixture.companion.clear(); fixture.tables.clear(); fixture.eligibility.clear();
      target=FrameBuffer(32,32,clear); before=target; work=budget;
      check(paint(retained,bytes,target,0,16000,{0,0,32,32},ink,{},work)==Status::ok&&!equal(target,before),"retained raster owner");
    }
    display_list_owns_bindings_and_preflights_all_commands(argv[1]);
    std::cout<<cases<<" actual-font independent paint/full/tiled cases; refusal/extremes/lifetime passed\n";
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
