#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace jellyframe::detail {

// Borrowed CSS token bytes. Decode only when a token contains an escape or NUL;
// no token owns storage and no operation below allocates.
struct CssTokenDecoder {
    std::string_view source;
    std::size_t position = 0;
    std::uint32_t pending = 0;
    unsigned remaining = 0;
    bool string = false;
    bool next(unsigned char& byte) noexcept;
};
struct CssIdentifierToken {
    std::string_view encoded;
    std::size_t decoded_bytes = 0;
    bool raw = true;
};
bool css_token_raw_bytes(std::string_view source) noexcept;
bool css_token_decoded_length(std::string_view source, bool string,
                              std::size_t& length, std::size_t limit) noexcept;
bool css_token_decoded_equals(std::string_view source, bool string,
                              std::string_view actual, bool insensitive) noexcept;
// ASCII-only case folding used by HTML selector/type semantics. Non-ASCII
// bytes compare literally; this is deliberately not Unicode case folding.
bool css_ascii_case_equal(std::string_view left, std::string_view right) noexcept;
// position identifies a backslash. Rejects identifier newline escapes; EOF
// after the backslash is a valid U+FFFD escape. Nonhex UTF-8 consumes one scalar.
std::size_t css_token_skip_escape(std::string_view source, std::size_t position) noexcept;
// Failed token reads preserve both the position and output. The identifier cap
// is decoded UTF-8 bytes; attribute selectors supply their smaller own cap.
// Raw non-ASCII scalars follow CSS Syntax ident ranges. Malformed UTF-8 in an
// identifier is rejected; escaped excluded scalars remain valid ident content.
bool css_read_identifier(std::string_view source, std::size_t& position,
                         CssIdentifierToken& token, std::size_t limit = 4096) noexcept;
bool css_read_string(std::string_view source, std::size_t& position,
                     std::string_view& token) noexcept;
bool css_identifier_equals(const CssIdentifierToken& token,
                           std::string_view actual, bool insensitive = false) noexcept;
// Writes exactly decoded_bytes, without a NUL terminator. A short buffer is
// rejected before any write. ASCII folding leaves all non-ASCII bytes intact.
bool css_identifier_copy(const CssIdentifierToken& token, char* buffer,
                         std::size_t capacity, bool fold_ascii = false) noexcept;

} // namespace jellyframe::detail
