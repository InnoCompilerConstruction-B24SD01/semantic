#include "dlang/optimizer.hpp"
#include "dlang/semantic.hpp"

#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace dlang {
namespace {

using Integer = std::int64_t;
constexpr Integer kMin = std::numeric_limits<Integer>::min();
constexpr Integer kMax = std::numeric_limits<Integer>::max();

std::optional<Integer> integer_value(const AstNode& node) {
    if (node.kind != AstKind::IntegerLiteral) {
        return std::nullopt;
    }
    Integer value;
    const char* end = node.value.data() + node.value.size();
    const auto parsed = std::from_chars(node.value.data(), end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return std::nullopt;
    }
    return value;
}

void replace_literal(AstNode& node, AstKind kind, std::string value) {
    // Keep the original operator's source position for diagnostics.
    node.kind = kind;
    node.value = std::move(value);
    node.children.clear();
}

void replace_boolean(AstNode& node, bool value) {
    replace_literal(node, AstKind::BooleanLiteral, value ? "true" : "false");
}

std::optional<Integer> arithmetic(const std::string& op, Integer lhs, Integer rhs) {
    // Guard before calculating: signed overflow is undefined in C++.
    if (op == "+") {
        if ((rhs > 0 && lhs > kMax - rhs) || (rhs < 0 && lhs < kMin - rhs)) {
            return std::nullopt;
        }
        return lhs + rhs;
    }
    if (op == "-") {
        if ((rhs > 0 && lhs < kMin + rhs) || (rhs < 0 && lhs > kMax + rhs)) {
            return std::nullopt;
        }
        return lhs - rhs;
    }
    if (op == "*") {
        if (lhs > 0) {
            if ((rhs > 0 && lhs > kMax / rhs) || (rhs < 0 && rhs < kMin / lhs)) {
                return std::nullopt;
            }
        } else if (lhs < 0) {
            if ((rhs > 0 && lhs < kMin / rhs) || (rhs < 0 && lhs < kMax / rhs)) {
                return std::nullopt;
            }
        }
        return lhs * rhs;
    }
    // Division and floating-point operations are deliberately left to runtime.
    return std::nullopt;
}

void fold_constants(AstNode& node) {
    if (node.kind == AstKind::UnaryExpression) {
        const auto& operand = node.children[0];
        if (node.value == "not" && operand.kind == AstKind::BooleanLiteral) {
            replace_boolean(node, operand.value == "false");
        } else if (const auto value = integer_value(operand)) {
            if (node.value == "+" || (node.value == "-" && *value != kMin)) {
                const Integer result = node.value == "-" ? -*value : *value;
                replace_literal(node, AstKind::IntegerLiteral, std::to_string(result));
            }
        }
        return;
    }
    if (node.kind != AstKind::BinaryExpression) {
        return;
    }

    const auto& left = node.children[0];
    const auto& right = node.children[1];
    const auto lhs = integer_value(left);
    const auto rhs = integer_value(right);
    if (lhs && rhs) {
        if (const auto value = arithmetic(node.value, *lhs, *rhs)) {
            replace_literal(node, AstKind::IntegerLiteral, std::to_string(*value));
        } else if (node.value == "<") {
            replace_boolean(node, *lhs < *rhs);
        } else if (node.value == "<=") {
            replace_boolean(node, *lhs <= *rhs);
        } else if (node.value == ">") {
            replace_boolean(node, *lhs > *rhs);
        } else if (node.value == ">=") {
            replace_boolean(node, *lhs >= *rhs);
        } else if (node.value == "=") {
            replace_boolean(node, *lhs == *rhs);
        } else if (node.value == "/=") {
            replace_boolean(node, *lhs != *rhs);
        }
    } else if (left.kind == AstKind::BooleanLiteral &&
               right.kind == AstKind::BooleanLiteral) {
        const bool a = left.value == "true";
        const bool b = right.value == "true";
        if (node.value == "and") {
            replace_boolean(node, a && b);
        } else if (node.value == "or") {
            replace_boolean(node, a || b);
        } else if (node.value == "xor") {
            replace_boolean(node, a != b);
        }
    }
}

void optimize_node(AstNode& node) {
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        optimize_node(node.children[i]);
        const AstKind kind = node.children[i].kind;
        if (node.kind == AstKind::Block &&
            (kind == AstKind::ReturnStatement || kind == AstKind::ExitStatement)) {
            // Only a direct terminator ends this block. A return in a nested
            // function or an exit inside a nested loop does not end its parent.
            node.children.resize(i + 1);
            break;
        }
    }
    // Bottom-up traversal also folds expressions such as (2 + 3) * 4.
    fold_constants(node);
}

} // namespace

AstNode Optimizer::optimize(const AstNode& program) const {
    // Invalid code must not become valid merely because it is unreachable.
    (void)SemanticAnalyzer().analyze(program);
    AstNode result = program;
    optimize_node(result);
    return result;
}

} // namespace dlang
