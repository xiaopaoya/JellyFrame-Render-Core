#include <render_core/css_parser.h>
#include <render_core/html_parser.h>
#include <render_core/layout.h>
#include <stdexcept>

#if JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION != 1
#error Unsupported natural image layout contract
#endif
using namespace jellyframe;
const LayoutBox* image(const LayoutBox& box) {
    if (box.node && box.node->tag_name == "img") return &box;
    for (const auto& child : box.children) if (auto* result = image(*child)) return result;
    return nullptr;
}
ImageIntrinsicSize lookup(const Node&, void* context) {
    ++*static_cast<int*>(context);
    return {ImageIntrinsicSizeState::Known,80,40};
}
int main() {
    int queries = 0;
    auto dom = HtmlParser{}.parse("<img src='package-image'>");
    StyleResolver styles(CssParser{}.parse("img{display:block;width:25.5px}"));
    LayoutEngineOptions options;
    NaturalImageSizingStatistics stats;
    options.natural_image_sizing.provider = {lookup,&queries};
    options.natural_image_sizing.statistics = &stats;
    auto tree = LayoutEngine(styles,{},options).layout(*dom,320,240);
    const auto* box = image(*tree);
    if (!box || box->content_geometry.width != 25500 || box->content_geometry.height() != 12750 ||
        queries != 1 || stats.accepted != 1 || stats.scratch_bytes > 4096)
        throw std::runtime_error("installed natural-image sizing contract failed");
}
