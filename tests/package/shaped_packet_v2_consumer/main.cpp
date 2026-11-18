#include <render_core/feature_config.h>

static_assert(JELLYFRAME_RENDER_CORE_SHAPED_PACKET_V2_ENABLED == EXPECT_CODEC);

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
