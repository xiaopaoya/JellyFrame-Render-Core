#include "render_core/layer_tree.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct ResolverState {
    std::vector<std::uint16_t> requested;
    bool known_intrinsic_size = true;
};

bool resolve_background(const Node&, ImageResolveKind kind, std::uint16_t id,
                        ResolvedImageResource& result, void* context) {
    if (kind != ImageResolveKind::Background) return false;
    static_cast<ResolverState*>(context)->requested.push_back(id);
    result.image_handle = id;
    if (static_cast<ResolverState*>(context)->known_intrinsic_size) {
        result.intrinsic_size = {ImageIntrinsicSizeState::Known, 8, 8};
    }
    return id != 99;
}

BackgroundLayer url_layer(std::uint16_t id) {
    BackgroundLayer layer;
    layer.kind = BackgroundLayerKind::Url;
    layer.image.resource_id = id;
    layer.image.repeat_x = BackgroundImageRepeat::NoRepeat;
    layer.image.repeat_y = BackgroundImageRepeat::NoRepeat;
    return layer;
}

struct Fixture {
    Node node{NodeType::Element};
    LayoutBox box;
    ResolverState resolver;
    VectorDiagnosticSink diagnostics;

    Fixture() {
        node.tag_name = "section";
        box.node = &node;
        box.rect = {8, 8, 32, 24};
        box.style.background_color = {255, 255, 0, 255};
        box.style.image_rendering = ImageRendering::CrispEdges;
    }

    DisplayList build() {
        LayerTreeBuilderOptions options;
        options.image_resolver = {resolve_background, &resolver};
        options.diagnostics = &diagnostics;
        LayerTreeBuilder builder(options);
        const auto tree = builder.build(box);
        return builder.flatten(*tree);
    }
};

void canonical_order_and_fallback() {
    Fixture fixture;
    fixture.box.style.background_layer_count = 3;
    fixture.box.style.background_layers[0] = url_layer(1);
    fixture.box.style.background_layers[1] = {};
    fixture.box.style.background_layers[2] = url_layer(2);

    const auto list = fixture.build();
    check(list.size() == 3, "base color and both URL layers are emitted");
    check(list[0].type() == DisplayCommandType::FillRect &&
              list[0].color.r == 255 && list[0].color.g == 255 && list[0].color.b == 0,
          "base color is painted beneath image layers");
    check(list[1].type() == DisplayCommandType::Image &&
              list[1].image_paint().image_handle == 2 &&
              list[2].type() == DisplayCommandType::Image &&
              list[2].image_paint().image_handle == 1,
          "image slots paint in reverse CSS order with none retained");
    check(fixture.resolver.requested == std::vector<std::uint16_t>({2, 1}),
          "only URL slots resolve, in paint order");
    check(list[1].image_paint().image_rendering == ImageRendering::CrispEdges &&
              list[2].image_paint().image_rendering == ImageRendering::CrispEdges,
          "background URL layers use Style::image_rendering");
}

void malformed_count_refuses_all_layers() {
    Fixture fixture;
    fixture.box.style.background_color = {255, 255, 0, 255};
    fixture.box.style.background_layer_count = 5;
    fixture.box.style.background_layers[0] = url_layer(1);
    fixture.box.style.background_layers[1] = url_layer(2);

    const auto list = fixture.build();
    check(list.size() == 1 && list[0].type() == DisplayCommandType::FillRect,
          "invalid public layer count emits only the fallback color");
    check(fixture.resolver.requested.empty(), "invalid count does not resolve a valid prefix");
    check(fixture.diagnostics.size() == 1, "invalid count emits one bounded diagnostic");
}

void canonical_style_keeps_background_color() {
    Fixture fixture;
    fixture.box.style.background_color = {20, 40, 60, 255};
    const auto list = fixture.build();
    check(list.size() == 1 && list[0].type() == DisplayCommandType::FillRect &&
              list[0].color.r == 20 && list[0].color.g == 40 && list[0].color.b == 60,
          "canonical Style uses its background color beneath empty layer slots");
}

void auto_sized_url_requires_known_intrinsic_size() {
    Fixture fixture;
    fixture.resolver.known_intrinsic_size = false;
    fixture.box.style.background_layer_count = 1;
    fixture.box.style.background_layers[0] = url_layer(1);

    const auto list = fixture.build();
    check(list.size() == 1 && list[0].type() == DisplayCommandType::FillRect,
          "auto-sized URL background with unknown intrinsic size is rejected");
    check(fixture.resolver.requested == std::vector<std::uint16_t>({1}),
          "unknown intrinsic size is checked after resolving the URL");
}

void color_clip_uses_per_edge_insets_without_origin() {
    Fixture fixture;
    fixture.box.rect = {10, 20, 40, 30};
    fixture.box.style.background_layer_count = 1;
    fixture.box.style.background_layers[0].kind = BackgroundLayerKind::None;
    fixture.box.style.background_layers[0].image.clip = BackgroundBox::ContentBox;
    fixture.box.style.background_layers[0].image.origin = static_cast<BackgroundBox>(99);
    fixture.box.style.border_width = {1, 2, 3, 4};
    fixture.box.style.padding = {5, 6, 7, 8};

    const auto list = fixture.build();
    check(!list.empty() && list[0].type() == DisplayCommandType::FillRect,
          "shared color paints when positioning origin is invalid");
    check(list[0].rect.x == 22 && list[0].rect.y == 26 &&
              list[0].rect.width == 20 && list[0].rect.height == 14,
          "content clip uses exact asymmetric border and padding insets");
    check(fixture.diagnostics.size() == 0,
          "invalid origin does not affect shared color clip geometry");
}

void empty_inner_color_clip_is_omitted() {
    Fixture fixture;
    fixture.box.rect = {0, 0, 12, 12};
    fixture.box.style.background_layer_count = 1;
    fixture.box.style.background_layers[0].kind = BackgroundLayerKind::None;
    fixture.box.style.background_layers[0].image.clip = BackgroundBox::ContentBox;
    fixture.box.style.border_width = {1, 1, 1, 1};
    fixture.box.style.padding = {5, 5, 5, 5};

    const auto list = fixture.build();
    check(std::none_of(list.begin(), list.end(), [](const DisplayCommand& command) {
              return command.type() == DisplayCommandType::FillRect && command.color.r == 255 &&
                  command.color.g == 255 && command.color.b == 0 && command.color.a == 255;
          }), "empty content clip does not expand to the border box");
    check(fixture.diagnostics.size() == 1,
          "empty color clip reports one bounded diagnostic");
}

}

int main() {
    try {
        canonical_order_and_fallback();
        malformed_count_refuses_all_layers();
        canonical_style_keeps_background_color();
        auto_sized_url_requires_known_intrinsic_size();
        color_clip_uses_per_edge_insets_without_origin();
        empty_inner_color_clip_is_omitted();
        std::cout << "canonical background layer paint contract passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
