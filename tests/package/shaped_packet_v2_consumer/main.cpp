#include <render_core/feature_config.h>
#include <type_traits>

static_assert(JELLYFRAME_RENDER_CORE_SHAPED_PACKET_V2_ENABLED == EXPECT_CODEC);
static_assert(JELLYFRAME_RENDER_CORE_CONTEXTUAL_TEXT_ENABLED == EXPECT_TEXT);
static_assert(JELLYFRAME_RENDER_CORE_CONTEXTUAL_LAYOUT_ENABLED == EXPECT_LAYOUT);
static_assert(!JELLYFRAME_RENDER_CORE_TEXT_SHAPING_ENABLED);

#if EXPECT_TEXT
#include <render_core/contextual_text.h>
#include <render_core/contextual_text_paragraph.h>
#include <render_core/software_renderer.h>
#if defined(_WIN64) && defined(_MSC_VER) && !defined(_DEBUG)
static_assert(sizeof(jellyframe::DisplayCommand) == 136,
              "contextual display support must preserve DisplayCommand ABI");
#endif
static_assert(std::is_copy_constructible<jellyframe::ContextualTextDisplayCommand>::value,
              "contextual display command owns a copyable Binding pin");
#elif __has_include(<render_core/contextual_text.h>) || __has_include(<render_core/contextual_text_paragraph.h>)
#error "disabled contextual text headers must not be installed"
#endif

#if EXPECT_LAYOUT
#include <render_core/contextual_text_layout.h>
#elif __has_include(<render_core/contextual_text_layout.h>)
#error "disabled contextual layout header must not be installed"
#endif

#if __has_include(<render_core/shaped_text_binding.h>)
#error "the ABI4 shaping binding header is outside the current installed contract"
#endif

#if EXPECT_CODEC
#include <render_core/shaped_packet_v2.h>

int main() {
    using namespace jellyframe::shaped_packet_v2;
    Header header{16000, 0, 1, 1};
    header.binding_identity[0] = 1;
    std::uint64_t work = 1000;
    std::string wire;
    View view;
    return encode(header, {}, nullptr, 0, nullptr, 0, {}, work, wire) == Status::ok &&
                   decode(wire, {}, work, view) == Status::ok && view.valid() &&
                   view.source().empty() && view.glyph_count() == 0
               ? 0 : 1;
}
#else
#if __has_include(<render_core/shaped_packet_v2.h>)
#error "disabled JFSP v2 header must not be installed"
#endif
int main() { return 0; }
#endif
