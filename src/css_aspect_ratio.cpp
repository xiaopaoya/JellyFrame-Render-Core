#include "css_aspect_ratio.h"
#include "css_ratio_tokens.h"
#include "render_core/style.h"
#include <cstdint>
#include <limits>
#include <numeric>

namespace jellyframe::detail {
namespace {
struct Decimal { std::uint64_t coefficient = 0; int exponent = 0; };
bool decimal(std::string_view text, std::size_t& position, Decimal& output) noexcept {
    const auto begin = position;
    const auto end = ratio_tokens::number_end(text, begin);
    if (end == begin || end - begin > 63) return false;
    // A unit/percentage changes this token's type; it cannot become the next
    // auto keyword simply because its numeric prefix was readable.
    auto unit_end = end;
    CssIdentifierToken unit;
    if ((end < text.size() && text[end] == '%') || css_read_identifier(text, unit_end, unit)) return false;
    auto p = begin;
    const bool negative = text[p] == '-';
    if (text[p] == '+' || text[p] == '-') ++p;
    unsigned fractional = 0, trailing_zeros = 0;
    std::uint64_t coefficient = 0;
    bool point = false;
    for (; p < end && text[p] != 'e' && text[p] != 'E'; ++p) {
        if (text[p] == '.') { point = true; continue; }
        if (point) ++fractional;
        const auto digit = static_cast<unsigned>(text[p] - '0');
        if (!digit) {
            if (coefficient) ++trailing_zeros;
            continue;
        }
        // Delay zeroes until a later nonzero digit proves they belong to the
        // significant coefficient. No mantissa scratch or second scan needed.
        while (trailing_zeros) {
            if (coefficient > std::numeric_limits<std::uint64_t>::max() / 10) return false;
            coefficient *= 10;
            --trailing_zeros;
        }
        if (coefficient > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
        coefficient = coefficient * 10 + digit;
    }
    int exponent = 0;
    if (p < end) {
        ++p;
        const bool minus = text[p] == '-';
        if (text[p] == '+' || text[p] == '-') ++p;
        for (; p < end; ++p) {
            exponent = exponent * 10 + (text[p] - '0');
            if (exponent > 10000) return false;
        }
        if (minus) exponent = -exponent;
    }
    if (negative && coefficient) return false;
    output = {coefficient, exponent - static_cast<int>(fractional) + static_cast<int>(trailing_zeros)};
    position = end;
    return true;
}
bool keyword(std::string_view text, std::size_t& position, const char* expected) noexcept {
    auto p = position;
    CssIdentifierToken token;
    if (!css_read_identifier(text, p, token) || !css_identifier_equals(token, expected, true)) return false;
    position = p;
    return true;
}
bool scaled_ratio(Decimal a, Decimal b, CssAspectRatio& output) noexcept {
    if (!a.coefficient || !b.coefficient) {
        output.width = a.coefficient ? 1 : 0;
        output.height = b.coefficient ? 1 : 0;
        return true;
    }
    const auto divisor = std::gcd(a.coefficient, b.coefficient);
    auto numerator = a.coefficient / divisor, denominator = b.coefficient / divisor;
    int exponent = a.exponent - b.exponent;
    while (exponent != 0) {
        auto& grow = exponent > 0 ? numerator : denominator;
        auto& shrink = exponent > 0 ? denominator : numerator;
        const auto factor = std::gcd<std::uint64_t>(shrink, 10);
        shrink /= factor;
        const auto multiplier = 10 / factor;
        if (grow > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) / multiplier) return false;
        grow *= multiplier;
        exponent += exponent > 0 ? -1 : 1;
    }
    if (numerator > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ||
        denominator > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) return false;
    output.width = static_cast<int>(numerator);
    output.height = static_cast<int>(denominator);
    return true;
}
} // namespace
bool parse_css_aspect_ratio(std::string_view text, CssAspectRatio& output) noexcept {
    if (text.size() > 4096) return false;

    std::size_t pos = 0;
    ratio_tokens::trivia(text, pos);
    CssAspectRatio result;
    auto end = pos;
    CssIdentifierToken first;
    const bool leading_identifier = css_read_identifier(text, end, first);
    const bool leading_auto = leading_identifier && css_identifier_equals(first, "auto", true);
    if (leading_identifier) {
        pos = end;
        if (!leading_auto) {
            const bool inherit = css_identifier_equals(first, "inherit", true);
            if (!inherit && !css_identifier_equals(first, "initial", true) &&
                !css_identifier_equals(first, "unset", true)) return false;
            ratio_tokens::trivia(text, pos);
            if (pos != text.size()) return false;
            if (inherit) result.width = result.height = -2;
            output = result;
            return true;
        }
    }
    ratio_tokens::trivia(text, pos);
    if (leading_auto && pos == text.size()) { output = result; return true; }
    Decimal a, b{1, 0};
    if (!decimal(text, pos, a)) return false;
    ratio_tokens::trivia(text, pos);
    if (pos < text.size() && text[pos] == '/') {
        ++pos;
        ratio_tokens::trivia(text, pos);
        if (!decimal(text, pos, b)) return false;
        ratio_tokens::trivia(text, pos);
    }
    bool trailing_auto = false;
    if (!leading_auto) trailing_auto = keyword(text, pos, "auto");
    ratio_tokens::trivia(text, pos);
    if (pos != text.size() || !scaled_ratio(a, b, result)) return false;
    result.automatic = leading_auto || trailing_auto;
    output = result;
    return true;
}
void finalize_inherited_aspect_ratio(Style& style, const Style* parent) noexcept {
    if (style.aspect_ratio_width != -2 || style.aspect_ratio_height != -2) return;
    style.aspect_ratio_width = parent ? parent->aspect_ratio_width : 0;
    style.aspect_ratio_height = parent ? parent->aspect_ratio_height : 0;
    style.aspect_ratio_auto = parent ? parent->aspect_ratio_auto : true;
}
} // namespace jellyframe::detail
