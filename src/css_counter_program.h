#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace jellyframe {
struct RenderObject;
class DiagnosticSink;

namespace detail {

enum class CounterOperationKind { Reset, Set, Increment };

struct CounterOperation {
    std::string name;
    int value = 0;
};

struct CounterDeclaration {
    std::vector<CounterOperation> operations;
    bool inherits = false;
};

struct CounterProgram {
    CounterDeclaration reset;
    CounterDeclaration set;
    CounterDeclaration increment;
    bool empty() const noexcept;
};

struct RenderCounterPrograms {
    CounterProgram element;
    CounterProgram before;
    CounterProgram after;
    bool empty() const noexcept;
};

bool parse_counter_operations(std::string_view source,
                              CounterOperationKind kind,
                              std::vector<CounterOperation>& output);
bool counter_declaration_syntax_supported(std::string_view property,
                                          std::string_view value);
bool apply_counter_declaration(CounterProgram& program,
                               std::string_view property,
                               std::string_view value);
void inherit_counter_declarations(CounterProgram& program, const CounterProgram* parent);
bool parse_decimal_counter_content(std::string_view value, std::string& name, std::string& suffix);
using CounterPrograms = std::unordered_map<const RenderObject*, RenderCounterPrograms>;
bool finalize_counter_tree(RenderObject& root, const CounterPrograms& programs,
                           DiagnosticSink* diagnostics);

} // namespace detail
} // namespace jellyframe
