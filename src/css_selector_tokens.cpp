#include "css_selector_tokens.h"

namespace jellyframe::detail {
namespace {

constexpr bool css_space(unsigned char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}
constexpr unsigned char fold(unsigned char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
}
constexpr int hex(unsigned char c) noexcept {
    return c >= '0' && c <= '9' ? c - '0' :
        c >= 'a' && c <= 'f' ? c - 'a' + 10 :
        c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
constexpr bool name_start(unsigned char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == 0;
}
constexpr bool name_char(unsigned char c) noexcept {
    return name_start(c) || (c >= '0' && c <= '9') || c == '-';
}
// CSS Syntax's non-ASCII ident code point ranges. Escapes intentionally bypass
// this classification; an escaped excluded scalar still belongs to an ident.
constexpr bool non_ascii_name_start(std::uint32_t c) noexcept {
    return c == 0xB7 || (c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xF6) ||
        (c >= 0xF8 && c <= 0x37D) || (c >= 0x37F && c <= 0x1FFF) ||
        c == 0x200C || c == 0x200D || c == 0x203F || c == 0x2040 ||
        (c >= 0x2070 && c <= 0x218F) || (c >= 0x2C00 && c <= 0x2FEF) ||
        (c >= 0x3001 && c <= 0xD7FF) || (c >= 0xF900 && c <= 0xFDCF) ||
        (c >= 0xFDF0 && c <= 0xFFFD) || (c >= 0x10000 && c <= 0x10FFFF);
}

bool read_utf8_scalar(std::string_view source, std::size_t position,
                      std::uint32_t& scalar, std::size_t& end) noexcept {
    if (position >= source.size()) return false;
    const auto lead = static_cast<unsigned char>(source[position]);
    const unsigned size = lead >= 0xC2 && lead <= 0xDF ? 2 :
        lead >= 0xE0 && lead <= 0xEF ? 3 : lead >= 0xF0 && lead <= 0xF4 ? 4 : 0;
    if (size == 0 || source.size() - position < size) return false;
    std::uint32_t value = lead & (size == 2 ? 0x1F : size == 3 ? 0x0F : 0x07);
    for (unsigned i = 1; i < size; ++i) {
        const auto byte = static_cast<unsigned char>(source[position + i]);
        if ((byte & 0xC0) != 0x80) return false;
        value = (value << 6) | (byte & 0x3F);
    }
    if ((size == 3 && value < 0x800) || (size == 4 && value < 0x10000) ||
        (value >= 0xD800 && value <= 0xDFFF) || value > 0x10FFFF) return false;
    scalar = value;
    end = position + size;
    return true;
}

bool name_start_at(std::string_view source, std::size_t position) noexcept {
    const auto byte = static_cast<unsigned char>(source[position]);
    if (byte < 0x80) return name_start(byte);
    std::uint32_t scalar;
    std::size_t end;
    return read_utf8_scalar(source, position, scalar, end) && non_ascii_name_start(scalar);
}

bool valid_escape(std::string_view source, std::size_t position) noexcept {
    if (position >= source.size() || source[position] != '\\') return false;
    // CSS would-start-an-identifier considers backslash + EOF a valid escape.
    if (source.size() - position == 1) return true;
    const char c = source[position + 1];
    return c != '\n' && c != '\r' && c != '\f';
}

// Position is the slash on entry. CSS hex escapes consume one terminating
// whitespace code point (CRLF is one after CSS input preprocessing).
std::uint32_t consume_escape(std::string_view source, std::size_t& position) noexcept {
    ++position;
    if (position == source.size()) return 0xFFFD;
    int digit = hex(static_cast<unsigned char>(source[position]));
    if (digit < 0) {
        const unsigned char c = static_cast<unsigned char>(source[position]);
        if (c >= 0x80) {
            std::uint32_t scalar;
            std::size_t end;
            if (read_utf8_scalar(source, position, scalar, end)) {
                position = end;
                return scalar;
            }
        }
        ++position;
        return c == 0 ? 0xFFFD : c;
    }
    std::uint32_t codepoint = 0;
    unsigned count = 0;
    do {
        codepoint = (codepoint << 4) | static_cast<unsigned>(digit);
        ++position;
        ++count;
        digit = position < source.size() ? hex(static_cast<unsigned char>(source[position])) : -1;
    } while (count < 6 && digit >= 0);
    if (position < source.size() && css_space(static_cast<unsigned char>(source[position]))) {
        const char terminator = source[position++];
        if (terminator == '\r' && position < source.size() && source[position] == '\n') ++position;
    }
    return codepoint == 0 || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)
        ? 0xFFFD : codepoint;
}

bool raw_equals(std::string_view expected, std::string_view actual, bool insensitive) noexcept {
    if (expected.size() != actual.size()) return false;
    if (expected == actual) return true;
    if (!insensitive) return false;
    for (std::size_t i = 0; i < expected.size(); ++i)
        if (fold(static_cast<unsigned char>(expected[i])) != fold(static_cast<unsigned char>(actual[i]))) return false;
    return true;
}

// Raw non-ASCII bytes are borrowed by the marker decoder. Validate every raw
// scalar before the byte-count fast path, including a non-ASCII scalar that
// follows a simple backslash escape. Hex escapes remain ASCII source bytes.
bool valid_raw_utf8(std::string_view source) noexcept {
    std::size_t position = 0;
    while (position < source.size()) {
        const auto byte = static_cast<unsigned char>(source[position]);
        if (byte == '\\') {
            ++position;
            if (position < source.size() && static_cast<unsigned char>(source[position]) >= 0x80) {
                std::uint32_t scalar;
                std::size_t end;
                if (!read_utf8_scalar(source, position, scalar, end)) return false;
                position = end;
            }
            continue;
        }
        if (byte < 0x80) {
            ++position;
            continue;
        }
        std::uint32_t scalar;
        std::size_t end;
        if (!read_utf8_scalar(source, position, scalar, end)) return false;
        position = end;
    }
    return true;
}

} // namespace

bool CssTokenDecoder::next(unsigned char& byte) noexcept {
    if (remaining != 0) {
        --remaining;
        byte = static_cast<unsigned char>(0x80 | ((pending >> (remaining * 6)) & 0x3F));
        return true;
    }
    while (position < source.size()) {
        unsigned char c = static_cast<unsigned char>(source[position]);
        if (c == '\\' && string && position + 1 < source.size() &&
            (source[position + 1] == '\n' || source[position + 1] == '\r' || source[position + 1] == '\f')) {
            ++position;
            const char newline = source[position++];
            if (newline == '\r' && position < source.size() && source[position] == '\n') ++position;
            continue;
        }
        std::uint32_t codepoint;
        if (c == '\\') {
            // A non-hex escaped non-ASCII character already consists of
            // UTF-8 source bytes. Preserve those bytes rather than encoding
            // its leading byte a second time.
            if (position + 1 < source.size() && static_cast<unsigned char>(source[position + 1]) >= 0x80) {
                position += 2;
                byte = static_cast<unsigned char>(source[position - 1]);
                return true;
            }
            codepoint = consume_escape(source, position);
        } else {
            ++position;
            if (c != 0) { byte = c; return true; }
            codepoint = 0xFFFD;
        }
        if (codepoint <= 0x7F) { byte = static_cast<unsigned char>(codepoint); return true; }
        pending = codepoint;
        if (codepoint <= 0x7FF) { remaining = 1; byte = static_cast<unsigned char>(0xC0 | (codepoint >> 6)); }
        else if (codepoint <= 0xFFFF) { remaining = 2; byte = static_cast<unsigned char>(0xE0 | (codepoint >> 12)); }
        else { remaining = 3; byte = static_cast<unsigned char>(0xF0 | (codepoint >> 18)); }
        return true;
    }
    return false;
}

bool css_token_raw_bytes(std::string_view source) noexcept {
    for (const char byte : source) if (byte == '\\' || byte == '\0') return false;
    return true;
}

bool css_token_decoded_length(std::string_view source, bool string, std::size_t& length, std::size_t limit) noexcept {
    if (!valid_raw_utf8(source)) return false;
    if (css_token_raw_bytes(source)) {
        if (source.size() > limit) return false;
        length = source.size();
        return true;
    }
    CssTokenDecoder decoder{source, 0, 0, 0, string};
    unsigned char byte;
    std::size_t count = 0;
    while (decoder.next(byte)) if (++count > limit) return false;
    length = count;
    return true;
}

std::size_t css_token_skip_escape(std::string_view source, std::size_t position) noexcept {
    if (!valid_escape(source, position)) return std::string_view::npos;
    if (source.size() - position > 1 && static_cast<unsigned char>(source[position + 1]) >= 0x80) {
        std::uint32_t scalar;
        std::size_t end;
        if (!read_utf8_scalar(source, position + 1, scalar, end)) return std::string_view::npos;
    }
    consume_escape(source, position);
    return position;
}

bool css_read_identifier(std::string_view source, std::size_t& position, CssIdentifierToken& token, std::size_t limit) noexcept {
    auto cursor = position;
    const auto begin = cursor;
    if (cursor >= source.size()) return false;
    unsigned char c = static_cast<unsigned char>(source[cursor]);
    if (c == '-') {
        if (cursor + 1 == source.size()) return false;
        c = static_cast<unsigned char>(source[cursor + 1]);
        if (c != '-' && !name_start_at(source, cursor + 1) && !valid_escape(source, cursor + 1)) return false;
    } else if (!name_start_at(source, cursor) && !valid_escape(source, cursor)) return false;
    bool raw = true;
    while (cursor < source.size()) {
        c = static_cast<unsigned char>(source[cursor]);
        if (name_char(c)) { raw = raw && c != 0; ++cursor; continue; }
        if (c >= 0x80) {
            std::uint32_t scalar;
            std::size_t end;
            // Malformed UTF-8 cannot supply a borrowed decoded token. Reject
            // transactionally rather than treating continuation bytes as names.
            if (!read_utf8_scalar(source, cursor, scalar, end)) return false;
            if (!non_ascii_name_start(scalar)) break;
            cursor = end;
            continue;
        }
        if (!valid_escape(source, cursor)) break;
        const auto end = css_token_skip_escape(source, cursor);
        if (end == std::string_view::npos) return false;
        raw = false;
        cursor = end;
    }
    CssIdentifierToken parsed{source.substr(begin, cursor - begin), 0, raw};
    if (raw) {
        if (parsed.encoded.size() > limit) return false;
        parsed.decoded_bytes = parsed.encoded.size();
    } else if (!css_token_decoded_length(parsed.encoded, false, parsed.decoded_bytes, limit)) return false;
    token = parsed;
    position = cursor;
    return true;
}

bool css_read_string(std::string_view source, std::size_t& position, std::string_view& token) noexcept {
    if (position >= source.size() || (source[position] != '\'' && source[position] != '"')) return false;
    auto cursor = position;
    const char quote = source[cursor++];
    const auto begin = cursor;
    while (cursor < source.size()) {
        const char c = source[cursor];
        if (c == quote) { token = source.substr(begin, cursor - begin); ++cursor; position = cursor; return true; }
        if (c == '\n' || c == '\r' || c == '\f') return false;
        if (c != '\\') { ++cursor; continue; }
        if (cursor + 1 == source.size()) return false;
        const char following = source[cursor + 1];
        if (following == '\n' || following == '\r' || following == '\f') {
            cursor += 2;
            if (following == '\r' && cursor < source.size() && source[cursor] == '\n') ++cursor;
        } else consume_escape(source, cursor);
    }
    return false;
}

bool css_token_decoded_equals(std::string_view encoded, bool string, std::string_view actual, bool insensitive) noexcept {
    CssTokenDecoder decoder{encoded, 0, 0, 0, string};
    unsigned char expected;
    std::size_t position = 0;
    while (decoder.next(expected)) {
        if (position == actual.size()) return false;
        unsigned char value = static_cast<unsigned char>(actual[position++]);
        if (insensitive) { expected = fold(expected); value = fold(value); }
        if (expected != value) return false;
    }
    return position == actual.size();
}

bool css_ascii_case_equal(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (fold(static_cast<unsigned char>(left[i])) !=
            fold(static_cast<unsigned char>(right[i]))) return false;
    }
    return true;
}

bool css_identifier_equals(const CssIdentifierToken& token, std::string_view actual, bool insensitive) noexcept {
    if (token.decoded_bytes != actual.size()) return false;
    return token.raw ? raw_equals(token.encoded, actual, insensitive) :
        css_token_decoded_equals(token.encoded, false, actual, insensitive);
}

bool css_identifier_copy(const CssIdentifierToken& token, char* buffer, std::size_t capacity, bool fold_ascii) noexcept {
    if (token.decoded_bytes > capacity || (token.decoded_bytes != 0 && buffer == nullptr)) return false;
    if (token.raw) {
        for (std::size_t i = 0; i < token.encoded.size(); ++i) {
            const auto byte = static_cast<unsigned char>(token.encoded[i]);
            buffer[i] = static_cast<char>(fold_ascii ? fold(byte) : byte);
        }
    } else {
        CssTokenDecoder decoder{token.encoded};
        unsigned char byte;
        std::size_t position = 0;
        while (decoder.next(byte)) buffer[position++] = static_cast<char>(fold_ascii ? fold(byte) : byte);
    }
    return true;
}

} // namespace jellyframe::detail
