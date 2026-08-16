#include "render_core/text_backend.h"

#include "render_core/text_scan.h"

#include <algorithm>
#include <cstdint>
#include <cctype>
#include <cstddef>
#include <limits>
#include <vector>

namespace jellyframe {
namespace {

constexpr std::uint32_t kFontFamilyFnvOffset = 0x811c9dc5U;
constexpr std::uint32_t kFontFamilyFnvPrime = 0x01000193U;

TextMetrics sanitize_metrics(TextMetrics metrics, int font_size, int font_weight) {
    const TextMetrics fallback = fallback_text_metrics({}, font_size, font_weight);
    metrics.width = std::max(0, metrics.width);
    metrics.line_height = metrics.line_height > 0 ? metrics.line_height : fallback.line_height;
    return metrics;
}

std::string_view trim_family_view(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        value.remove_prefix(1);
        value.remove_suffix(1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
            value.remove_prefix(1);
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
            value.remove_suffix(1);
        }
    }
    return value;
}

bool family_equals_ascii(std::string_view family, std::string_view expected) {
    family = trim_family_view(family);
    if (family.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < family.size(); ++index) {
        const char normalized = static_cast<char>(std::tolower(static_cast<unsigned char>(family[index])));
        if (normalized != expected[index]) {
            return false;
        }
    }
    return true;
}

} // namespace

std::uint32_t normalized_font_family_hash(std::string_view family) {
    family = trim_family_view(family);
    if (family.empty() || is_generic_font_family(family)) {
        return 0;
    }

    std::uint32_t hash = kFontFamilyFnvOffset;
    bool pending_space = false;
    bool wrote = false;
    for (char ch : family) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (std::isspace(byte) != 0) {
            pending_space = wrote;
            continue;
        }
        if (pending_space) {
            hash ^= static_cast<std::uint8_t>(' ');
            hash *= kFontFamilyFnvPrime;
            pending_space = false;
        }
        const std::uint8_t normalized =
            byte < 0x80U ? static_cast<std::uint8_t>(std::tolower(byte)) : byte;
        hash ^= normalized;
        hash *= kFontFamilyFnvPrime;
        wrote = true;
    }
    return wrote ? hash : 0;
}

bool is_generic_font_family(std::string_view family) {
    return family_equals_ascii(family, "serif") ||
        family_equals_ascii(family, "sans-serif") ||
        family_equals_ascii(family, "monospace") ||
        family_equals_ascii(family, "cursive") ||
        family_equals_ascii(family, "fantasy") ||
        family_equals_ascii(family, "system-ui") ||
        family_equals_ascii(family, "ui-serif") ||
        family_equals_ascii(family, "ui-sans-serif") ||
        family_equals_ascii(family, "ui-monospace") ||
        family_equals_ascii(family, "emoji") ||
        family_equals_ascii(family, "math") ||
        family_equals_ascii(family, "fangsong");
}

TextMetrics fallback_text_metrics(const std::string& text, int font_size, int font_weight) {
    const int safe_font_size = std::max(1, font_size);
    const int ascii_advance = std::max(1, (safe_font_size * 2) / 3);
    const int bold_extra = font_weight >= 600 ? std::max(1, safe_font_size / 12) : 0;
    int width = 0;
    for (std::size_t index = 0; index < text.size();) {
        const std::uint32_t codepoint = consume_utf8_codepoint(text, index);
        width += codepoint < 0x80U ? ascii_advance : safe_font_size;
    }
    return TextMetrics{
        width + (text.empty() ? 0 : std::max(6, safe_font_size / 2)) + bold_extra,
        safe_font_size + std::max(6, safe_font_size / 3),
    };
}

int bounded_letter_spacing(int font_size, int letter_spacing) {
    return std::max(-std::max(1, font_size / 2), std::min(std::max(1, font_size) * 2, letter_spacing));
}

TextMetrics measure_text(const TextMeasureProvider& provider,
                         const std::string& text,
                         int font_size,
                         int font_weight) {
    return measure_text(provider, text, font_size, font_weight, 0);
}

TextMetrics measure_text(const TextMeasureProvider& provider,
                         const std::string& text,
                         int font_size,
                         int font_weight,
                         std::uint32_t font_family_hash) {
    if (font_family_hash != 0 && provider.measure_family != nullptr) {
        TextMetrics metrics;
        if (provider.measure_family(text, font_size, font_weight, font_family_hash, &metrics, provider.context)) {
            return sanitize_metrics(metrics, font_size, font_weight);
        }
    }
    if (provider.measure != nullptr) {
        TextMetrics metrics;
        if (provider.measure(text, font_size, font_weight, &metrics, provider.context)) {
            return sanitize_metrics(metrics, font_size, font_weight);
        }
    }
    return fallback_text_metrics(text, font_size, font_weight);
}

TextMetrics measure_text_with_letter_spacing(const TextMeasureProvider& provider,
                                             std::string_view text,
                                             int font_size,
                                             int font_weight,
                                             std::uint32_t font_family_hash,
                                             int letter_spacing) {
    if (letter_spacing == 0 || text.empty()) {
        return measure_text(provider, std::string(text), font_size, font_weight, font_family_hash);
    }

    const int bounded_spacing = bounded_letter_spacing(font_size, letter_spacing);
    TextMetrics metrics;
    std::size_t codepoint_count = 0;
    for (std::size_t begin = 0; begin < text.size();) {
        std::size_t end = begin;
        consume_utf8_codepoint(text, end);
        const TextMetrics scalar = measure_text(provider,
                                                std::string(text.substr(begin, end - begin)),
                                                font_size,
                                                font_weight,
                                                font_family_hash);
        if (metrics.width > std::numeric_limits<int>::max() - scalar.width) {
            metrics.width = std::numeric_limits<int>::max();
        } else {
            metrics.width += scalar.width;
        }
        metrics.line_height = std::max(metrics.line_height, scalar.line_height);
        ++codepoint_count;
        begin = end;
    }
    if (codepoint_count > 1) {
        const std::int64_t spaced_width = static_cast<std::int64_t>(metrics.width) +
            static_cast<std::int64_t>(bounded_spacing) * static_cast<std::int64_t>(codepoint_count - 1);
        metrics.width = static_cast<int>(std::max<std::int64_t>(0,
            std::min<std::int64_t>(std::numeric_limits<int>::max(), spaced_width)));
    }
    return sanitize_metrics(metrics, font_size, font_weight);
}

std::vector<std::string> wrap_text_anywhere(const TextMeasureProvider& provider,
                                            std::string_view text,
                                            int font_size,
                                            int font_weight,
                                            std::uint32_t font_family_hash,
                                            int letter_spacing,
                                            int available_width) {
    std::vector<std::string> lines;
    if (text.empty()) {
        return lines;
    }
    const int width_limit = std::max(1, available_width);
    const int bounded_spacing = bounded_letter_spacing(font_size, letter_spacing);
    std::string line;
    line.reserve(std::min<std::size_t>(text.size(), 64));
    int line_width = 0;
    for (std::size_t begin = 0; begin < text.size();) {
        std::size_t end = begin;
        consume_utf8_codepoint(text, end);
        const std::string_view scalar = text.substr(begin, end - begin);
        if (scalar == "\n") {
            lines.push_back(std::move(line));
            line.clear();
            line_width = 0;
            begin = end;
            continue;
        }
        const int scalar_width = measure_text(provider,
                                              std::string(scalar),
                                              font_size,
                                              font_weight,
                                              font_family_hash).width;
        const int candidate_width = line.empty() ? scalar_width : line_width + bounded_spacing + scalar_width;
        if (!line.empty() && candidate_width > width_limit) {
            lines.push_back(std::move(line));
            line = std::string(scalar);
            line_width = scalar_width;
        } else {
            line.append(scalar.data(), scalar.size());
            line_width = candidate_width;
        }
        begin = end;
    }
    lines.push_back(std::move(line));
    return lines;
}

std::vector<std::string> wrap_text_at_opportunities(const TextMeasureProvider& provider,
                                                    std::string_view text,
                                                    int font_size,
                                                    int font_weight,
                                                    std::uint32_t font_family_hash,
                                                    int letter_spacing,
                                                    int available_width) {
    std::vector<std::string> lines;
    if (text.empty()) {
        return lines;
    }

    const int width_limit = std::max(1, available_width);
    std::string line;
    std::string token;
    bool pending_space = false;

    const auto append_token = [&]() {
        if (token.empty()) {
            return;
        }
        std::string candidate = line;
        if (!candidate.empty() && pending_space) {
            candidate.push_back(' ');
        }
        candidate += token;
        if (!line.empty() && measure_text_with_letter_spacing(provider,
                                                              candidate,
                                                              font_size,
                                                              font_weight,
                                                              font_family_hash,
                                                              letter_spacing).width > width_limit) {
            lines.push_back(std::move(line));
            line = std::move(token);
        } else {
            line = std::move(candidate);
        }
        token.clear();
        pending_space = false;
    };

    for (std::size_t begin = 0; begin < text.size();) {
        std::size_t end = begin;
        const std::uint32_t codepoint = consume_utf8_codepoint(text, end);
        const std::string_view scalar = text.substr(begin, end - begin);
        if (codepoint == '\n') {
            append_token();
            lines.push_back(std::move(line));
            line.clear();
            pending_space = false;
            begin = end;
            continue;
        }
        if (codepoint == ' ' || codepoint == '\t' || codepoint == '\r') {
            append_token();
            pending_space = !line.empty();
            begin = end;
            continue;
        }

        token.append(scalar.data(), scalar.size());
        const bool break_after = is_cjk_codepoint(codepoint) ||
            codepoint == '-' || codepoint == '/' || codepoint == 0x3001U || codepoint == 0x3002U;
        if (break_after) {
            append_token();
        }
        begin = end;
    }
    append_token();
    if (!line.empty() || lines.empty()) {
        lines.push_back(std::move(line));
    }
    return lines;
}

std::vector<std::string> wrap_text_balanced(const TextMeasureProvider& provider,
                                            std::string_view text,
                                            int font_size,
                                            int font_weight,
                                            std::uint32_t font_family_hash,
                                            int letter_spacing,
                                            int available_width) {
    // Balance only a short headline-like run. The ordinary wrapper remains the
    // fallback for long paragraphs, explicit line breaks and malformed input.
    constexpr std::size_t kMaxBalanceTokens = 16;
    constexpr std::size_t kMaxBalanceLines = 4;
    const std::vector<std::string> ordinary = wrap_text_at_opportunities(provider,
                                                                           text,
                                                                           font_size,
                                                                           font_weight,
                                                                           font_family_hash,
                                                                           letter_spacing,
                                                                           available_width);
    if (ordinary.size() < 2 || ordinary.size() > kMaxBalanceLines ||
        text.find('\n') != std::string_view::npos) {
        return ordinary;
    }

    struct BalanceToken {
        std::string text;
        bool leading_space = false;
    };
    std::vector<BalanceToken> tokens;
    tokens.reserve(std::min<std::size_t>(text.size(), kMaxBalanceTokens));
    std::string token;
    bool pending_space = false;
    const auto append_token = [&]() {
        if (token.empty()) {
            return;
        }
        tokens.push_back(BalanceToken{std::move(token), pending_space});
        token.clear();
        pending_space = false;
    };
    for (std::size_t begin = 0; begin < text.size();) {
        std::size_t end = begin;
        const std::uint32_t codepoint = consume_utf8_codepoint(text, end);
        const std::string_view scalar = text.substr(begin, end - begin);
        if (codepoint == ' ' || codepoint == '\t' || codepoint == '\r') {
            append_token();
            pending_space = !tokens.empty();
        } else {
            token.append(scalar.data(), scalar.size());
            if (is_cjk_codepoint(codepoint) || codepoint == '-' || codepoint == '/' ||
                codepoint == 0x3001U || codepoint == 0x3002U) {
                append_token();
            }
        }
        begin = end;
    }
    append_token();
    if (tokens.size() < 2 || tokens.size() > kMaxBalanceTokens) {
        return ordinary;
    }

    const auto join_tokens = [&](std::size_t begin, std::size_t end) {
        std::string line;
        for (std::size_t index = begin; index < end; ++index) {
            if (index != begin && tokens[index].leading_space) {
                line.push_back(' ');
            }
            line += tokens[index].text;
        }
        return line;
    };

    const std::size_t token_count = tokens.size();
    std::vector<std::vector<int>> widths(token_count, std::vector<int>(token_count + 1, -1));
    for (std::size_t begin = 0; begin < token_count; ++begin) {
        for (std::size_t end = begin + 1; end <= token_count; ++end) {
            widths[begin][end] = measure_text_with_letter_spacing(provider,
                                                                    join_tokens(begin, end),
                                                                    font_size,
                                                                    font_weight,
                                                                    font_family_hash,
                                                                    letter_spacing).width;
        }
    }

    std::int64_t ordinary_width_sum = 0;
    for (const std::string& line : ordinary) {
        ordinary_width_sum += measure_text_with_letter_spacing(provider,
                                                                line,
                                                                font_size,
                                                                font_weight,
                                                                font_family_hash,
                                                                letter_spacing).width;
    }
    const int target_width = std::max(1, static_cast<int>(
        (ordinary_width_sum + static_cast<std::int64_t>(ordinary.size()) - 1) /
        static_cast<std::int64_t>(ordinary.size())));
    constexpr std::int64_t kUnreachableCost = std::numeric_limits<std::int64_t>::max() / 4;
    const std::size_t line_count = ordinary.size();
    std::vector<std::vector<std::int64_t>> costs(line_count + 1,
                                                  std::vector<std::int64_t>(token_count + 1,
                                                                            kUnreachableCost));
    std::vector<std::vector<int>> previous(line_count + 1,
                                           std::vector<int>(token_count + 1, -1));
    costs[0][0] = 0;
    const int width_limit = std::max(1, available_width);
    for (std::size_t line = 1; line <= line_count; ++line) {
        for (std::size_t begin = line - 1; begin < token_count; ++begin) {
            if (costs[line - 1][begin] == kUnreachableCost) {
                continue;
            }
            for (std::size_t end = begin + 1; end <= token_count; ++end) {
                const int width = widths[begin][end];
                if (width > width_limit && end != begin + 1) {
                    continue;
                }
                constexpr std::int64_t kMaximumPenaltyDelta = 1000000;
                const std::int64_t delta = std::max(-kMaximumPenaltyDelta,
                    std::min(kMaximumPenaltyDelta,
                             static_cast<std::int64_t>(width) - target_width));
                const std::int64_t penalty = delta * delta;
                const std::int64_t candidate = costs[line - 1][begin] >
                    kUnreachableCost - penalty
                    ? kUnreachableCost
                    : costs[line - 1][begin] + penalty;
                if (candidate < costs[line][end]) {
                    costs[line][end] = candidate;
                    previous[line][end] = static_cast<int>(begin);
                }
            }
        }
    }
    if (previous[line_count][token_count] < 0) {
        return ordinary;
    }

    std::vector<std::string> balanced(line_count);
    std::size_t end = token_count;
    for (std::size_t line = line_count; line > 0; --line) {
        const int begin = previous[line][end];
        if (begin < 0) {
            return ordinary;
        }
        balanced[line - 1] = join_tokens(static_cast<std::size_t>(begin), end);
        end = static_cast<std::size_t>(begin);
    }
    return end == 0 ? balanced : ordinary;
}

} // namespace jellyframe
