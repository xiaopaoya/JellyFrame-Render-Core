#include "css_counter_program.h"

#include "css_selector_tokens.h"
#include "render_core/diagnostics.h"
#include "render_core/render_tree.h"

#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace jellyframe::detail {
namespace {

bool css_space(unsigned char value) noexcept {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\f';
}

std::string_view trim_css(std::string_view value) noexcept {
    while (!value.empty() && css_space(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
    while (!value.empty() && css_space(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
    return value;
}

bool keyword(std::string_view value, std::string_view expected) noexcept {
    value = trim_css(value);
    return css_ascii_case_equal(value, expected);
}

CounterDeclaration* declaration(CounterProgram& program, std::string_view property) noexcept {
    if (property == "counter-reset") return &program.reset;
    if (property == "counter-set") return &program.set;
    if (property == "counter-increment") return &program.increment;
    return nullptr;
}

const CounterDeclaration* declaration(const CounterProgram* program, std::string_view property) noexcept {
    if (program == nullptr) return nullptr;
    if (property == "counter-reset") return &program->reset;
    if (property == "counter-set") return &program->set;
    if (property == "counter-increment") return &program->increment;
    return nullptr;
}

bool decoded_name(const CssIdentifierToken& token, std::string& output) {
    if (token.decoded_bytes == 0 || token.decoded_bytes > 63) return false;
    output.assign(token.decoded_bytes, '\0');
    if (!css_identifier_copy(token, output.data(), output.size())) return false;
    for (const std::string_view reserved : {"none", "initial", "inherit", "unset", "revert", "revert-layer", "default"}) {
        if (css_ascii_case_equal(output, reserved)) return false;
    }
    return true;
}

bool parse_integer(std::string_view source, std::size_t& position, int& output) noexcept {
    const std::size_t begin = position;
    bool positive_sign = false;
    if (position < source.size() && source[position] == '+') {
        positive_sign = true;
        ++position;
    }
    const std::size_t integer_begin = position;
    std::int64_t value = 0;
    const auto result = std::from_chars(source.data() + integer_begin,
                                        source.data() + source.size(), value, 10);
    if (result.ec != std::errc{} || result.ptr == source.data() + integer_begin ||
        (positive_sign && value < 0) || value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) {
        position = begin;
        return false;
    }
    position = static_cast<std::size_t>(result.ptr - source.data());
    if (position < source.size() && !css_space(static_cast<unsigned char>(source[position]))) {
        position = begin;
        return false;
    }
    output = static_cast<int>(value);
    return true;
}

void inherit_one(CounterDeclaration& target, const CounterDeclaration* parent) {
    if (!target.inherits) return;
    target.operations = parent == nullptr ? std::vector<CounterOperation>{} : parent->operations;
    target.inherits = false;
}

constexpr std::size_t max_tree_visits = 4096;
constexpr std::size_t max_counter_depth = 64;
constexpr std::size_t max_active_counters = 64;
constexpr std::size_t max_generated_text_bytes = 8192;
constexpr std::size_t max_pseudo_text_bytes = 256;

struct ActiveCounter {
    std::string name;
    std::int32_t value = 0;
};

struct ResolvedCounterText {
    RenderObject* owner = nullptr;
    bool after = false;
    std::string text;
};

struct EvaluationState {
    const CounterPrograms& programs;
    std::vector<ActiveCounter> counters;
    std::vector<ResolvedCounterText> generated_text;
    std::size_t visits = 0;
    std::size_t generated_bytes = 0;

    std::size_t find(std::string_view name) const noexcept {
        for (std::size_t index = counters.size(); index != 0; --index) {
            if (counters[index - 1].name == name) return index - 1;
        }
        return counters.size();
    }

    std::size_t ensure(std::string_view name) {
        const auto existing = find(name);
        if (existing != counters.size()) return existing;
        if (counters.size() == max_active_counters) throw std::length_error("counter scope limit");
        counters.push_back({std::string(name), 0});
        return counters.size() - 1;
    }

    void append_counter_text(RenderObject& owner, bool after,
                             std::string_view name, std::string_view suffix) {
        const auto value = ensure(name);
        const std::string digits = std::to_string(counters[value].value);
        if (digits.size() > max_pseudo_text_bytes - std::min(max_pseudo_text_bytes, suffix.size()) ||
            digits.size() + suffix.size() > max_generated_text_bytes - generated_bytes) {
            throw std::length_error("generated counter text budget");
        }
        std::string output;
        output.reserve(digits.size() + suffix.size());
        output.append(digits);
        output.append(suffix);
        generated_text.push_back({&owner, after, std::move(output)});
        generated_bytes += digits.size() + suffix.size();
    }
};

void apply_operations(const CounterProgram& program, EvaluationState& state) {
    for (const auto& operation : program.reset.operations) {
        if (state.counters.size() == max_active_counters)
            throw std::length_error("counter scope limit");
        state.counters.push_back({operation.name, operation.value});
    }
    for (const auto& operation : program.increment.operations) {
        auto& counter = state.counters[state.ensure(operation.name)];
        const std::int64_t result = static_cast<std::int64_t>(counter.value) + operation.value;
        if (result < std::numeric_limits<std::int32_t>::min() ||
            result > std::numeric_limits<std::int32_t>::max())
            throw std::length_error("counter value overflow");
        counter.value = static_cast<std::int32_t>(result);
    }
    for (const auto& operation : program.set.operations)
        state.counters[state.ensure(operation.name)].value = operation.value;
}

void evaluate_pseudo(RenderObject& object, EvaluationState& state,
                     const CounterProgram& program, bool after) {
    const std::size_t scope_size = state.counters.size();
    apply_operations(program, state);
    const auto kind = after ? object.style.after_content_kind : object.style.before_content_kind;
    const auto& name = after ? object.style.after_counter_name : object.style.before_counter_name;
    const auto& suffix = after ? object.style.after_counter_suffix : object.style.before_counter_suffix;
    if (is_generated_counter(kind) && name != "list-num")
        state.append_counter_text(object, after, name, suffix);
    state.counters.resize(scope_size);
}

void evaluate_counter_tree(RenderObject& object, EvaluationState& state, std::size_t depth) {
    if (++state.visits > max_tree_visits || depth > max_counter_depth)
        throw std::length_error("counter tree traversal budget");

    const std::size_t inherited_size = state.counters.size();
    const auto found = state.programs.find(&object);
    if (found != state.programs.end()) apply_operations(found->second.element, state);

    Style& style = object.style;
    if (found != state.programs.end()) {
        evaluate_pseudo(object, state, found->second.before, false);
    } else if (is_generated_counter(style.before_content_kind) &&
               style.before_counter_name != "list-num") {
        const std::size_t pseudo_scope_size = state.counters.size();
        state.append_counter_text(object, false, style.before_counter_name,
                                  style.before_counter_suffix);
        state.counters.resize(pseudo_scope_size);
    }

    for (auto& child : object.children)
        evaluate_counter_tree(*child, state, depth + 1);

    if (found != state.programs.end()) {
        evaluate_pseudo(object, state, found->second.after, true);
    } else if (is_generated_counter(style.after_content_kind) &&
               style.after_counter_name != "list-num") {
        const std::size_t pseudo_scope_size = state.counters.size();
        state.append_counter_text(object, true, style.after_counter_name,
                                  style.after_counter_suffix);
        state.counters.resize(pseudo_scope_size);
    }
    state.counters.resize(inherited_size);
}

} // namespace

bool parse_counter_operations(std::string_view source,
                              CounterOperationKind kind,
                              std::vector<CounterOperation>& output) {
    source = trim_css(source);
    if (source.empty() || source.size() > 4096) return false;

    std::vector<CounterOperation> candidate;
    if (keyword(source, "none")) {
        output.swap(candidate);
        return true;
    }

    std::size_t position = 0;
    while (position < source.size()) {
        while (position < source.size() && css_space(static_cast<unsigned char>(source[position]))) ++position;
        if (position == source.size()) break;
        if (candidate.size() == 16) return false;

        CssIdentifierToken token;
        if (!css_read_identifier(source, position, token, 63)) return false;
        CounterOperation operation;
        if (!decoded_name(token, operation.name)) return false;
        operation.value = kind == CounterOperationKind::Increment ? 1 : 0;

        const std::size_t after_name = position;
        while (position < source.size() && css_space(static_cast<unsigned char>(source[position]))) ++position;
        if (position < source.size() &&
            (source[position] == '+' ||
             (source[position] == '-' && position + 1 < source.size() &&
              source[position + 1] >= '0' && source[position + 1] <= '9') ||
             (source[position] >= '0' && source[position] <= '9'))) {
            if (position == after_name || !parse_integer(source, position, operation.value)) return false;
        }
        candidate.push_back(std::move(operation));
    }

    if (candidate.empty()) return false;
    output.swap(candidate);
    return true;
}

bool counter_declaration_syntax_supported(std::string_view property, std::string_view value) {
    if (property != "counter-reset" && property != "counter-set" && property != "counter-increment")
        return false;
    if (keyword(value, "initial") || keyword(value, "inherit") || keyword(value, "unset") ||
        keyword(value, "revert")) return true;
    std::vector<CounterOperation> parsed;
    const auto kind = property == "counter-reset" ? CounterOperationKind::Reset :
        property == "counter-set" ? CounterOperationKind::Set : CounterOperationKind::Increment;
    return parse_counter_operations(value, kind, parsed);
}

bool CounterProgram::empty() const noexcept {
    return !reset.inherits && reset.operations.empty() &&
           !increment.inherits && increment.operations.empty() &&
           !set.inherits && set.operations.empty();
}

bool RenderCounterPrograms::empty() const noexcept {
    return element.empty() && before.empty() && after.empty();
}

bool apply_counter_declaration(CounterProgram& program,
                               std::string_view property,
                               std::string_view value) {
    CounterDeclaration* target = declaration(program, property);
    if (target == nullptr) return false;

    CounterDeclaration candidate;
    if (keyword(value, "inherit")) {
        candidate.inherits = true;
    } else if (!keyword(value, "initial") && !keyword(value, "unset") &&
               !keyword(value, "revert")) {
        const auto kind = property == "counter-reset" ? CounterOperationKind::Reset :
            property == "counter-set" ? CounterOperationKind::Set : CounterOperationKind::Increment;
        if (!parse_counter_operations(value, kind, candidate.operations)) return false;
    }

    *target = std::move(candidate);
    return true;
}

void inherit_counter_declarations(CounterProgram& program, const CounterProgram* parent) {
    inherit_one(program.reset, declaration(parent, "counter-reset"));
    inherit_one(program.set, declaration(parent, "counter-set"));
    inherit_one(program.increment, declaration(parent, "counter-increment"));
}

bool parse_decimal_counter_content(std::string_view value, std::string& name, std::string& suffix) {
    constexpr std::size_t prefix_size = 8;
    if (value.size() < prefix_size || !css_ascii_case_equal(value.substr(0, prefix_size), "counter("))
        return false;
    value = trim_css(value.substr(prefix_size));
    std::size_t position = 0;
    CssIdentifierToken token;
    std::string parsed_name;
    if (!css_read_identifier(value, position, token, 63) || !decoded_name(token, parsed_name))
        return false;
    value = trim_css(value.substr(position));
    if (!value.empty() && value.front() == ',') {
        value = trim_css(value.substr(1));
        position = 0;
        if (!css_read_identifier(value, position, token, 63) ||
            !css_identifier_equals(token, "decimal", true)) return false;
        value = trim_css(value.substr(position));
    }
    if (value.empty() || value.front() != ')') return false;
    value = trim_css(value.substr(1));

    std::string parsed_suffix;
    if (!value.empty()) {
        std::string_view encoded;
        position = 0;
        std::size_t bytes = 0;
        if (!css_read_string(value, position, encoded) || !trim_css(value.substr(position)).empty() ||
            !css_token_decoded_length(encoded, true, bytes, 240)) return false;
        CssTokenDecoder decoder{encoded, 0, 0, 0, true};
        parsed_suffix.reserve(bytes);
        unsigned char byte;
        while (decoder.next(byte)) parsed_suffix.push_back(static_cast<char>(byte));
    }
    name = std::move(parsed_name);
    suffix = std::move(parsed_suffix);
    return true;
}

bool finalize_counter_tree(RenderObject& root, const CounterPrograms& programs,
                           DiagnosticSink* diagnostics) {
    try {
        EvaluationState state{programs, {}, {}, 0, 0};
        evaluate_counter_tree(root, state, 0);
        for (auto& resolved : state.generated_text) {
            if (resolved.after) resolved.owner->style.after_content_text = std::move(resolved.text);
            else resolved.owner->style.before_content_text = std::move(resolved.text);
        }
        return true;
    } catch (const std::bad_alloc&) {
    } catch (const std::length_error&) {
    }
    report_diagnostic(diagnostics, DiagnosticStage::RenderTree, DiagnosticSeverity::Warning,
                      "css-counter-evaluation-refused",
                      "Counter evaluation exceeded a bounded resource limit",
                      "Counter-generated text was omitted for this render tree.");
    return false;
}

} // namespace jellyframe::detail
