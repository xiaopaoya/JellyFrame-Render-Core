#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/software_renderer.h"
#include "../../src/natural_image_sizing.h"
#include "../../src/computed_size_values.h"
#include "../../src/intrinsic_source_query.h"
#include <iostream>
#include <stdexcept>

using namespace jellyframe;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
const LayoutBox& find(const LayoutBox& box, const char* id) {
    if (box.node && box.node->attribute("id") == id) return box;
    for (const auto& child : box.children) {
        try { return find(*child, id); } catch (const std::out_of_range&) {}
    }
    throw std::out_of_range(id);
}
struct Snapshot {
    ImageIntrinsicSize size{ImageIntrinsicSizeState::Known, 120, 60};
    int metadata_calls = 0, resolve_calls = 0, paint_calls = 0;
    std::uint32_t handle = 73;
};
ImageIntrinsicSize metadata(const Node& node, void* context) {
    check(node.tag_name == "img" && node.attribute("src") == "image", "selected content source passed unchanged");
    auto& snapshot = *static_cast<Snapshot*>(context);
    ++snapshot.metadata_calls;
    return snapshot.size;
}
LayoutEngineOptions options(Snapshot& snapshot, NaturalImageSizingStatistics& stats) {
    LayoutEngineOptions value;
    value.natural_image_sizing.provider = {metadata, &snapshot};
    value.natural_image_sizing.statistics = &stats;
    return value;
}
void dimensions(const char* css, std::int64_t width, std::int64_t height,
    const char* attributes = "", std::size_t calls = 1) {
    Snapshot snapshot;
    NaturalImageSizingStatistics stats;
    auto dom = HtmlParser{}.parse(std::string("<div><img id='i' src='image' ")+attributes+"></div>");
    StyleResolver styles(CssParser{}.parse(std::string("*{margin:0;padding:0;border:0}img{display:block;")+css+"}"));
    auto tree = LayoutEngine(styles, {}, options(snapshot, stats)).layout(*dom, 320, 240);
    const auto& image = find(*tree, "i");
    if (image.content_geometry.width != width || image.content_geometry.height() != height) {
        std::cerr << css << " expected " << width << ',' << height << " got "
                  << image.content_geometry.width << ',' << image.content_geometry.height() << '\n';
        throw std::runtime_error("natural sizing differs from expected Q1000 geometry");
    }
    check(stats.queries == calls && snapshot.metadata_calls == static_cast<int>(calls), "one bounded query when metadata needed");
    check(stats.scratch_bytes <= 4096, "memo remains within configured scratch budget");
}
void sizing() {
    dimensions("", 120000, 60000);
    dimensions("width:30.5px", 30500, 15250);
    dimensions("height:20.5px", 41000, 20500);
    dimensions("width:30px;height:10px", 30000, 10000, "", 0);
    dimensions("", 44000, 22000, "width='44'");
    dimensions("", 48000, 24000, "height='24'");
    dimensions("", 44000, 24000, "width='44' height='24'", 0);
    dimensions("width:auto;height:auto", 120000, 60000, "width='44' height='24'");
    dimensions("max-width:80px", 80000, 40000);
    dimensions("max-height:20px", 40000, 20000);
    dimensions("min-width:150px", 150000, 75000);
    dimensions("min-height:80px", 160000, 80000);
    dimensions("min-width:150px;max-height:20px", 150000, 20000);
    dimensions("max-width:20px;min-height:70px", 20000, 70000);
    dimensions("min-width:100px;max-width:20px", 100000, 50000);
    dimensions("width:30px;min-width:50px;max-height:10px", 50000, 10000);
    dimensions("height:30px;min-height:50px;max-width:10px", 10000, 50000);
    dimensions("aspect-ratio:1 / 1", 120000, 120000);
    dimensions("aspect-ratio:auto 1 / 1", 120000, 60000);
    dimensions("width:50px;aspect-ratio:2 / 1;padding:5px;border:1px solid black;box-sizing:border-box", 38000, 13000);
    dimensions("width:50px;padding:5px;border:1px solid black;box-sizing:border-box", 38000, 19000);
    dimensions("height:50px;padding:5px;border:1px solid black;box-sizing:border-box", 76000, 38000);
    dimensions("padding:5px;border:1px solid black;box-sizing:border-box;max-width:72px", 60000, 30000);
    dimensions("padding:5px;border:1px solid black;box-sizing:border-box;aspect-ratio:1 / 1;max-width:72px", 60000, 60000);
    dimensions("width:calc(25% + 1px)", 81000, 40500);
}
void refusal_and_budgets() {
    auto dom = HtmlParser{}.parse("<div><img id='i' src='image'><img id='j' src='image'></div>");
    StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}img{display:block}"));
    for (auto invalid : {ImageIntrinsicSize{}, ImageIntrinsicSize{ImageIntrinsicSizeState::Invalid,1,1},
        ImageIntrinsicSize{ImageIntrinsicSizeState::Known,0,60}, ImageIntrinsicSize{ImageIntrinsicSizeState::Known,8192,60},
        ImageIntrinsicSize{static_cast<ImageIntrinsicSizeState>(255),120,60}}) {
        Snapshot snapshot; snapshot.size = invalid;
        NaturalImageSizingStatistics stats;
        auto with = LayoutEngine(styles, {}, options(snapshot,stats)).layout(*dom,320,240);
        auto legacy = LayoutEngine(styles).layout(*dom,320,240);
        check(find(*with,"i").rect.width == find(*legacy,"i").rect.width &&
              find(*with,"i").rect.height == find(*legacy,"i").rect.height, "missing/invalid metadata retains legacy fallback");
        check(stats.queries == 2 && stats.accepted == 0, "declined results are bounded per source");
    }
    Snapshot snapshot;
    NaturalImageSizingStatistics stats;
    auto opts = options(snapshot,stats);
    opts.natural_image_sizing.max_queries = 1;
    auto tree = LayoutEngine(styles,{},opts).layout(*dom,320,240);
    check(find(*tree,"i").rect.width == 120 && find(*tree,"j").rect.width == 320,
        "query exhaustion preserves previously admitted results and falls back deterministically");
    check(stats.queries == 1 && stats.limit_refusals == 1, "query limit reported");
    for (auto budget : {std::size_t{0}, sizeof(detail::NaturalImageSizingFrame::Entry)-1}) {
        snapshot.metadata_calls = 0;
        opts.natural_image_sizing.max_scratch_bytes = budget;
        tree = LayoutEngine(styles,{},opts).layout(*dom,320,240);
        check(stats.queries == 0 && snapshot.metadata_calls == 0 && stats.limit_refusals == 2,
            "scratch exhaustion never queries host");
    }
    // A failed or changing host result cannot alter an earlier probe in the pass.
    opts = options(snapshot,stats);
    {
        detail::NaturalImageSizingFrame frame(opts.natural_image_sizing);
        const auto& source = *find(*tree,"i").node;
        snapshot.size = {};
        check(frame.metadata(source).state == ImageIntrinsicSizeState::Unknown, "unknown cached");
        snapshot.size = {ImageIntrinsicSizeState::Known,120,60};
        check(frame.metadata(source).state == ImageIntrinsicSizeState::Unknown && frame.statistics.queries == 1,
            "unknown memo cannot be reloaded during replay");
    }
    check(stats.cache_hits == 1, "memo hit statistics published");
    opts.natural_image_sizing.max_memo_entries = 0;
    tree = LayoutEngine(styles,{},opts).layout(*dom,320,240);
    check(stats.queries == 0 && stats.limit_refusals == 2, "zero memo budget cannot invoke provider");
    opts = options(snapshot,stats);
    opts.natural_image_sizing.provider.intrinsic_size = [](const Node&, void*) -> ImageIntrinsicSize {
        throw std::bad_alloc{};
    };
    tree = LayoutEngine(styles,{},opts).layout(*dom,320,240);
    check(stats.queries == 2 && stats.allocation_failures == 2 && stats.unknown == 2,
        "provider allocation failure is retained as unknown fallback");
}
void arithmetic_bounds() {
    using detail::scale_natural_ratio;
    for (std::int64_t value : {0,1,27,8191000})
        for (std::int64_t numerator : {0,1,3,8191})
            for (std::int64_t denominator : {1,2,7,8191})
                check(scale_natural_ratio(value,numerator,denominator) ==
                    (value*numerator+denominator/2)/denominator, "bounded ratio matches exact rational oracle");
    const auto maximum = detail::kDimensionPrecisionMaximum;
    check(scale_natural_ratio(maximum,maximum,maximum) == maximum &&
        scale_natural_ratio(maximum-1,maximum,maximum) == maximum-1 &&
        scale_natural_ratio(maximum,maximum,1) == maximum,
        "large coordinate ratios do not overflow and saturate deterministically");
    for (auto property : {detail::SizeProperty::MinWidth, detail::SizeProperty::MinHeight}) {
        Style style;
        check(detail::apply_computed_size(style,property,detail::SizeKind::Pixels,
            std::numeric_limits<int>::max()), "extreme typed minimum retained");
        check(!detail::natural_image_content_size(style,{ImageIntrinsicSizeState::Known,120,60},
            320000,240000,1000,1000).valid,
            "saturated minimum plus decoration refuses before constructing an invalid clamp interval");
    }
    check(!detail::natural_image_content_size(Style{}, {ImageIntrinsicSizeState::Known,120,60},
        320000,240000,-1,1000).valid, "negative decoration refuses without hiding invalid input");
}
void unsupported_contexts() {
    for (auto css : {"img{display:inline}", "img{display:inline-block}", "img{display:block;position:absolute}",
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        "div{display:flex}img{display:block}", "div{display:grid}img{display:block}",
#endif
        "div{display:inline-block}img{display:block}"}) {
        auto dom = HtmlParser{}.parse("<div><img id='i' src='image'></div>");
        StyleResolver styles(CssParser{}.parse(css));
        Snapshot snapshot; NaturalImageSizingStatistics stats;
        auto tree = LayoutEngine(styles,{},options(snapshot,stats)).layout(*dom,320,240);
        check(stats.queries == 0, "unsupported formatting context does not query natural metadata");
    }
    auto dom = HtmlParser{}.parse("<div><img id='i'></div>");
    StyleResolver styles(CssParser{}.parse("img{display:block}"));
    Snapshot snapshot; NaturalImageSizingStatistics stats;
    auto tree = LayoutEngine(styles,{},options(snapshot,stats)).layout(*dom,320,240);
    check(stats.queries == 0, "empty src is not an image metadata source");
    auto picture = HtmlParser{}.parse("<picture><img src='image'></picture>");
    StyleResolver picture_styles(CssParser{}.parse("picture,img{display:block}"));
    tree = LayoutEngine(picture_styles,{},options(snapshot,stats)).layout(*picture,320,240);
    check(stats.queries == 0, "picture source selection remains outside the first metadata consumer");
}
void typed_mixed_geometry() {
    auto dom = HtmlParser{}.parse("<div id='p'><img id='i' src='image'></div>");
    StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{width:100.25px}img{display:block}"));
    auto render = RenderTreeBuilder(styles).build(*dom);
    const auto apply = [&](auto&& self, RenderObject& object) -> void {
        if (object.node && object.node->attribute("id") == "i") {
            check(detail::apply_computed_length_percentage(object.style, detail::SizeProperty::Width, 0.5, 37.5),
                "typed mixed input admitted");
        }
        for (auto& child : object.children) self(self,*child);
    };
    apply(apply,*render);
    Snapshot snapshot; NaturalImageSizingStatistics stats;
    auto tree = LayoutEngine(styles,{},options(snapshot,stats)).layout(*render,320,240);
    check(find(*tree,"i").content_geometry.width == 38094 && find(*tree,"i").content_geometry.height() == 19047,
        "natural sizing consumes exact mixed typed geometry without changing parser admission");
    TextSystem text;
    check(detail::query_intrinsic_source_width(find(*tree,"i"),text,
        {detail::IntrinsicPercentageBasisKind::CyclicSameAxis,-1}).status == detail::IntrinsicSourceStatus::Unsupported,
        "source-query replaced refusal remains atomic");
}
bool resolve(const Node&, ImageResolveKind kind, std::uint16_t, ResolvedImageResource& output, void* context) {
    if (kind != ImageResolveKind::Content) return false;
    auto& snapshot = *static_cast<Snapshot*>(context);
    ++snapshot.resolve_calls;
    output = {snapshot.handle, snapshot.size};
    return true;
}
bool paint(FrameBuffer& target, const ImagePaintRequest& request, void* context) {
    auto& snapshot = *static_cast<Snapshot*>(context);
    ++snapshot.paint_calls;
    check(request.image_handle == snapshot.handle, "retained provider handle remains bound to original snapshot");
    for (int y = 0; y < target.height; ++y)
        for (int x = 0; x < target.width; ++x)
            if (x >= request.destination.x && x < request.destination.x+request.destination.width &&
                y >= request.destination.y && y < request.destination.y+request.destination.height)
                target.pixel(x,y) = {220,30,40,255};
    return true;
}
void lifetime_and_fractional_paint() {
    auto dom = HtmlParser{}.parse("<div id='p'><img id='i' src='image'><img id='j' src='image'></div>");
    StyleResolver styles(CssParser{}.parse("*{margin:0;padding:0;border:0}#p{width:100.25px}img{display:block;width:37.5%}"));
    Snapshot old;
    NaturalImageSizingStatistics stats;
    auto opts = options(old,stats);
    auto tree = LayoutEngine(styles,{},opts).layout(*dom,320,240);
    const auto old_width = find(*tree,"i").content_geometry.width;
    check(old_width == 37594 && find(*tree,"i").content_geometry.height() == 18797,
        "mixed dimension retains Q1000 natural geometry");
    check(find(*tree,"j").content_geometry.y == 18797 && find(*tree,"j").rect.y == 19 &&
        find(*tree,"j").rect.height == 19, "adjacent natural images accumulate before edge projection");
    LayerTreeBuilderOptions layer_options;
    layer_options.image_resolver = {resolve,&old};
    LayerTreeBuilder builder(layer_options);
    auto layers = builder.build(*tree);
    auto display = builder.flatten_with_clip_metadata(*layers);
    check(old.metadata_calls == 2 && old.resolve_calls == 2, "layout metadata and paint handle resolution are separate once-only stages");
    Snapshot next; next.size = {ImageIntrinsicSizeState::Known,80,80}; next.handle = 74;
    opts = options(next,stats);
    auto next_tree = LayoutEngine(styles,{},opts).layout(*dom,320,240);
    check(find(*next_tree,"i").content_geometry.height() == 37594 &&
        find(*tree,"i").content_geometry.height() == 18797, "old and new layout snapshots coexist");
    SoftwareCompositor compositor({}, {paint,&old});
    for (int replay = 0; replay < 2; ++replay) {
        auto frame = compositor.render(*layers,320,240,Color{255,255,255,255});
        check(frame.pixel(2,20).r == 220 && frame.pixel(2,39).r == 255, "paint follows retained natural-image geometry");
    }
    check(old.metadata_calls == 2 && old.resolve_calls == 2 && old.paint_calls == 4,
        "replay does not query metadata or reload handles");
    MonotonicArena arena(256);
    auto arena_tree = LayoutEngine(styles,{},opts).layout(*dom,320,240,arena);
    check(find(*arena_tree,"i").content_geometry.width == old_width, "arena layout uses same metadata path");
}
}
int main() {
    try {
        sizing(); refusal_and_budgets(); arithmetic_bounds(); unsupported_contexts(); typed_mixed_geometry(); lifetime_and_fractional_paint();
        std::cout << "Natural image block foundation passed; LayoutBox=" << sizeof(LayoutBox)
                  << " MemoEntry=" << sizeof(detail::NaturalImageSizingFrame::Entry) << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
