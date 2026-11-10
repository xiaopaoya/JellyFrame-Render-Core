#include "render_core/software_renderer.h"
#include "render_core/html_parser.h"
#include "render_core/css_parser.h"
#include "render_core/style.h"
#include "render_core/render_tree.h"
#include "render_core/layout.h"
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace jellyframe;
namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
bool same_rect(Rect a, Rect b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
struct Provider {
    ImagePaintRequest last;
    int calls = 0;
    bool available = true;
};
bool paint(FrameBuffer& target, const ImagePaintRequest& request, void* context) {
    auto& provider = *static_cast<Provider*>(context);
    provider.last = request;
    ++provider.calls;
    if (!provider.available) return false;
    // Deliberately writes the entire temporary surface: Core owns clipping.
    for (int y = 0; y < target.height; ++y) {
        for (int x = 0; x < target.width; ++x) {
            target.pixel(x, y) = {static_cast<std::uint8_t>(x - request.destination.x + 10), 80, 90, 255};
        }
    }
    return true;
}
DisplayCommand image() {
    DisplayCommand command(DisplayCommandType::Image);
    command.rect = {0, 0, 12, 8};
    command.image_paint().image_handle = 73;
    command.image_paint().background_paint_clip = {3, 2, 6, 4};
    command.image_paint().object_fit = ObjectFit::Contain;
    command.image_paint().object_position = {25, 75};
    command.image_paint().image_rendering = ImageRendering::Pixelated;
    return command;
}
void image_clipping_preserves_request() {
    Provider provider;
    SoftwareRasterizer raster({}, {paint, &provider});
    FrameBuffer target(16, 12, {1, 2, 3, 255});
    auto command = image();
    SoftwareRasterizerScratch scratch;
    raster.rasterize(command, target, {4, 0, 12, 12}, 1, 1, &scratch);
    check(provider.calls == 1, "temporary callback invoked");
    check(same_rect(provider.last.destination, {-3, -2, 12, 8}), "full sampling destination translated into temporary surface");
    check(same_rect(provider.last.background_painting_rect, {0, 0, 6, 4}), "independent painting area translated into temporary surface");
    check(provider.last.image_handle == 73 && provider.last.object_fit == ObjectFit::Contain &&
          provider.last.object_position.y_percent == 75 && provider.last.image_rendering == ImageRendering::Pixelated,
          "structured request preserves sampling controls");
    check(target.pixel(4, 3).r == 13 && target.pixel(3, 3).r == 1 && target.pixel(10, 3).r == 1,
          "Core bounds an unconstrained painter to background area and external clip");
    provider.available = false;
    target.clear({1, 2, 3, 255});
    raster.rasterize(command, target, {0, 0, 16, 12});
    check(target.pixel(4, 3).r == 1, "missing background has no placeholder");
    command.image_paint().background_paint_clip = {};
    raster.rasterize(command, target, {0, 0, 16, 12});
    check(target.pixel(4, 3).r == 226, "legacy content missing resource keeps placeholder");
    command.image_paint().background_paint_clip = {1, 0, 0, 0};
    provider.calls = 0;
    raster.rasterize(command, target, {0, 0, 16, 12});
    check(provider.calls == 0, "malformed clip never reaches provider");
}
void rounded_paint_area_and_legacy_request() {
    Provider provider;
    SoftwareRasterizer raster({}, {paint, &provider});
    auto command = image();
    command.image_paint().background_paint_clip = {2, 1, 8, 6};
    command.image_paint().border_radius = 3;
    FrameBuffer target(16, 12, {1, 2, 3, 255});
    raster.rasterize(command, target, {0, 0, 16, 12});
    check(provider.last.background_painting_radius == 3 &&
          provider.last.destination.width == 12, "rounded clip is independent from sampling tile");
    check(target.pixel(2, 1).r < target.pixel(5, 3).r && target.pixel(1, 3).r == 1,
          "rounded coverage follows painting rectangle");
    command.image_paint().background_paint_clip = {};
    raster.rasterize(command, target, {3, 2, 5, 4});
    check(same_rect(provider.last.background_painting_rect, {}),
          "absent painting metadata stays canonical on temporary target");
    check(provider.last.background_painting_radius == 0,
          "legacy rounded content does not claim a background painting radius");
}

void values_and_flatten_preserve_sampling() {
    auto original = image();
    DisplayCommand copied = original;
    DisplayCommand moved = std::move(copied);
    check(display_commands_equal(original, moved), "copy and move preserve image clip");
    moved.image_paint().background_paint_clip.dx++;
    check(!display_commands_equal(original, moved), "image equality includes independent clip");
    moved = original;
    translate_display_command(moved, 7, 9);
    check(moved.rect.x == 7 && moved.image_paint().background_paint_clip.dx == 3,
          "translation retains relative painting geometry");
    LayerNode root;
    root.bounds = {0, 0, 20, 20};
    root.has_clip = true;
    root.clip_rect = {4, 3, 3, 2};
    root.display_list.push_back(original);
    auto flattened = LayerTreeBuilder().flatten_with_clip_metadata(root);
    check(flattened.display_list.size() == 1 && same_rect(flattened.display_list[0].rect, original.rect),
          "metadata flatten keeps full image sample geometry");
    check(flattened.clips.size() == 1 && flattened.display_clip_indices[0] == 0,
          "external clip remains separately indexed");
    root.has_transform = true;
    root.transform.scale_x = 2;
    root.transform.scale_y = 2;
    flattened = LayerTreeBuilder().flatten_with_clip_metadata(root);
    check(flattened.display_list[0].transform.enabled &&
          same_rect(flattened.display_list[0].rect, original.rect), "affine metadata retains sampling rectangle");
    Provider provider;
    SoftwareCompositor compositor({}, {paint, &provider});
    auto target = compositor.render(root, 24, 24, {1, 2, 3, 255});
    check(provider.calls == 1 && provider.last.destination.width == 12 &&
          provider.last.background_painting_rect.width == 6, "affine source paint preserves complete request geometry");
    check(target.pixel(23, 23).r == 1, "affine clipping bounds painter output");
}
void gradients_keep_sampling_separate() {
    for (auto kind : {DisplayCommandType::LinearGradient, DisplayCommandType::ConicGradient,
                      DisplayCommandType::RadialGradient}) {
        DisplayCommand full(kind);
        full.rect = {1, 1, 12, 10};
        full.color = {240, 20, 30, 255};
        full.color2 = {20, 30, 240, 255};
        full.basic_paint().gradient_axis = GradientAxis::Horizontal;
        full.basic_paint().gradient_stop_percent = 50;
        auto clipped = full;
        clipped.basic_paint().background_paint_clip = {3, 2, 6, 6};
        check(!display_commands_equal(full, clipped), "gradient equality includes clip");
        FrameBuffer reference(16, 14, {1, 2, 3, 255});
        FrameBuffer actual(16, 14, {1, 2, 3, 255});
        SoftwareRasterizer raster;
        raster.rasterize(full, reference, {0, 0, 16, 14});
        raster.rasterize(clipped, actual, {0, 0, 16, 14});
        check(actual.pixel(5, 5).r == reference.pixel(5, 5).r &&
              actual.pixel(5, 5).b == reference.pixel(5, 5).b, "clip preserves gradient sample location");
        check(actual.pixel(2, 5).r == 1 && actual.pixel(10, 5).r == 1, "gradient painting area enforced");
        LayerNode root;
        root.bounds = {0, 0, 16, 14};
        root.has_clip = true;
        root.clip_rect = {5, 4, 2, 2};
        root.display_list.push_back(clipped);
        auto flattened = LayerTreeBuilder().flatten_with_clip_metadata(root);
        check(same_rect(flattened.display_list[0].rect, full.rect), "gradient metadata flatten preserves sampling geometry");
    }
}
struct Resolution {
    ResolvedImageResource resource;
    bool available = true;
    int calls = 0;
};
bool resolve(const Node&, ImageResolveKind, std::uint16_t, ResolvedImageResource& result, void* context) {
    auto& resolution = *static_cast<Resolution*>(context);
    check(result.image_handle == 0 && result.intrinsic_size.state == ImageIntrinsicSizeState::Unknown,
          "resolver output starts cleared");
    ++resolution.calls;
    result = resolution.resource;
    return resolution.available;
}
std::size_t resolved_images(Resolution& resolution, bool background, bool stretch_unknown_background = false) {
    auto document = HtmlParser().parse(background ? "<body><div></div></body>" : "<body><img src='image'></body>");
    auto sheet = CssParser().parse(background ?
        (stretch_unknown_background
            ? "div { width:12px; height:8px; background-image:url('/assets/image.bmp'); background-size:100% 100%; }"
            : "div { width:12px; height:8px; background-image:url('/assets/image.bmp'); }")
        : "img { width:12px; height:8px; }");
    StyleResolver styles(sheet);
    auto render_tree = RenderTreeBuilder(styles).build(*document);
    auto layout_tree = LayoutEngine(styles).layout(*render_tree, 40);
    LayerTreeBuilderOptions options;
    options.image_resolver = {resolve, &resolution};
    LayerTreeBuilder builder(options);
    auto layers = builder.build(*layout_tree);
    auto commands = builder.flatten_with_clip_metadata(*layers).display_list;
    std::size_t count = 0;
    for (const auto& command : commands) if (command.type() == DisplayCommandType::Image) ++count;
    return count;
}
void resolution_metadata_is_bounded_without_content_regression() {
    Resolution resolution{{73, {ImageIntrinsicSizeState::Unknown, 0, 0}}};
    check(resolved_images(resolution, true) == 0, "unknown intrinsic size refuses auto background geometry");
    check(resolved_images(resolution, true, true) == 1, "explicit stretch remains resolvable without intrinsic dimensions");
    resolution.resource.intrinsic_size = {ImageIntrinsicSizeState::Known, 8, 8};
    check(resolved_images(resolution, true) >= 1, "known bounded metadata accepted");
    for (auto size : {ImageIntrinsicSize{ImageIntrinsicSizeState::Known, 0, 1},
                      ImageIntrinsicSize{ImageIntrinsicSizeState::Known, 8192, 1},
                      ImageIntrinsicSize{ImageIntrinsicSizeState::Invalid, 1, 1}}) {
        resolution.resource.intrinsic_size = size;
        check(resolved_images(resolution, true) == 0, "invalid background metadata omitted");
        check(resolved_images(resolution, false) == 1, "content preserves existing handle semantics");
    }
    resolution.resource.image_handle = 0;
    check(resolved_images(resolution, true) == 0, "zero handle never published");
    resolution.resource.image_handle = 73;
    resolution.available = false;
    check(resolved_images(resolution, true) == 0, "failed resolver cannot publish written handle");
}
}
int main() {
    try {
        image_clipping_preserves_request();
        rounded_paint_area_and_legacy_request();
        values_and_flatten_preserve_sampling();
        gradients_keep_sampling_separate();
        resolution_metadata_is_bounded_without_content_regression();
        std::cout << "Image callback contract passed; DisplayCommand=" << sizeof(DisplayCommand)
                  << " BasicPaint=" << sizeof(BasicPaint) << " ImagePaint=" << sizeof(ImagePaint) << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
