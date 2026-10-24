#pragma once

#include "dlang/ast.hpp"

#include <cstddef>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace dlang {

enum class StaticType {
    NotApplicable,
    Dynamic,
    Integer,
    Real,
    Boolean,
    String,
    None,
    Array,
    Tuple,
    Function,
};

enum class SymbolKind {
    Variable,
    Parameter,
    LoopVariable,
};

struct SymbolInfo {
    std::size_t id; // One-based; zero means "no symbol" in SemanticNode.
    std::string name;
    SymbolKind kind;
    std::size_t line;
    std::size_t column;
    std::size_t scope_depth;
};

struct SemanticNode {
    AstKind kind;
    std::string value;
    std::size_t line;
    std::size_t column;
    StaticType type = StaticType::NotApplicable;
    std::size_t symbol_id = 0;
    std::vector<SemanticNode> children;
};

struct SemanticModel {
    SemanticNode root;
    std::vector<SymbolInfo> symbols;
};

class SemanticError : public std::runtime_error {
public:
    SemanticError(std::size_t line, std::size_t column, const std::string& message);

    std::size_t line() const noexcept { return line_; }
    std::size_t column() const noexcept { return column_; }

private:
    std::size_t line_;
    std::size_t column_;
};

class SemanticAnalyzer {
public:
    // Validate and decorate a complete parser AST. The analyzer is stateless and
    // may be reused. Throws SemanticError on the first semantic violation.
    SemanticModel analyze(const AstNode& program) const;
};

std::string_view static_type_name(StaticType type) noexcept;
std::string_view symbol_kind_name(SymbolKind kind) noexcept;
void print_semantic_tree(std::ostream& out, const SemanticNode& node,
                         std::size_t indent = 0);
std::ostream& operator<<(std::ostream& out, const SemanticModel& model);

} // namespace dlang
