#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/style.h"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace jellyframe;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Style resolve(const std::string& declarations) {
    auto document = HtmlParser{}.parse("<body></body>");
    Node* body = nullptr;
    const auto find = [&](const auto& self, Node& node) -> void {
        if (node.tag_name == "body") body = &node;
        for (auto& child : node.children) self(self, *child);
    };
    find(find, *document);
    check(body != nullptr, "body fixture exists");
    return StyleResolver(CssParser{}.parse("body{" + declarations + "}")).resolve(*body);
}

void cases() {
    auto style = resolve("background-color:#123456;background-image:url('/a.png'),none,url('/a.png');"
                         "background-position:left top,right bottom;"
                         "background-size:cover,contain;"
                         "background-repeat:no-repeat,repeat-x;"
                         "background-origin:content-box;background-clip:padding-box");
    check(style.background_layer_count == 3, "three computed layers");
    check(style.background_layers[0].kind == BackgroundLayerKind::Url &&
          style.background_layers[1].kind == BackgroundLayerKind::None &&
          style.background_layers[2].kind == BackgroundLayerKind::Url, "layer order and none preserved");
    check(style.background_layers[0].image.resource_id == style.background_layers[2].image.resource_id,
          "duplicate URL shares a resolver resource");
    check(style.background_layers[1].image.position_x_percent == 100 &&
          style.background_layers[2].image.position_x_percent == 0 &&
          style.background_layers[1].image.size == BackgroundImageSizeMode::Contain &&
          style.background_layers[2].image.size == BackgroundImageSizeMode::Cover,
          "longhand lists cycle from the first value over image layers");
    check(style.background_layers[0].image.origin == BackgroundBox::ContentBox &&
          style.background_layers[0].image.clip == BackgroundBox::PaddingBox,
          "origin and clip are tracked independently");

    style = resolve("background:url('/first.png') #123456;background-position:right bottom;"
                    "background:url('/second.png'),url('../invalid.png') red");
    check(style.background_layer_count == 1 && style.background_layers[0].image.resource_id == 1 &&
          style.background_color.b == 86 && style.background_layers[0].image.position_x_percent == 100,
          "invalid shorthand is rejected as a whole");
    style = resolve("");
    check(style.background_layer_count == 1 &&
          style.background_layers[0].kind == BackgroundLayerKind::None,
          "initial computed background has one none layer");

    auto document = HtmlParser{}.parse(
        "<div id=\"x\" style=\"background: url('/unused.png') #00ff00\"></div>");
    Node* target = nullptr;
    const auto find_target = [&](const auto& self, Node& node) -> void {
        if (node.attribute("id") == "x") target = &node;
        for (auto& child : node.children) self(self, *child);
    };
    find_target(find_target, *document);
    check(target != nullptr, "cascade budget fixture exists");
    StyleResolverOptions options;
    options.max_background_image_resources = 1;
    StyleResolver resolver(CssParser{}.parse(
        "#x { background-image: url('/kept.png') !important; }"), options);
    check(resolver.background_image_resource_count() == 1,
          "important stylesheet image occupies the sole resource slot");
    style = resolver.resolve(*target);
    check(style.background_color.r == 0 && style.background_color.g == 255 &&
          style.background_color.b == 0,
          "winning shorthand color applies when its image loses the cascade");
    check(style.background_layer_count == 1 &&
          resolver.background_image_resource_url(style.background_layers[0].image.resource_id) != nullptr &&
          *resolver.background_image_resource_url(style.background_layers[0].image.resource_id) == "/kept.png" &&
          resolver.background_image_resource_count() == 1,
          "losing shorthand URL consumes no resource slot and preserves the important image");
}
}

int main() {
    try { cases(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
