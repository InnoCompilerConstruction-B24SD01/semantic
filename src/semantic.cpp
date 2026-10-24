#include "dlang/semantic.hpp"

#include <optional>
#include <ostream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace dlang {

SemanticError::SemanticError(std::size_t line, std::size_t column,
                             const std::string& message)
    : std::runtime_error("semantic error at " + std::to_string(line) + ':' +
                         std::to_string(column) + ": " + message),
      line_(line), column_(column) {}

std::string_view static_type_name(StaticType type) noexcept {
    switch (type) {
    case StaticType::NotApplicable: return "n/a";
    case StaticType::Dynamic: return "dynamic";
    case StaticType::Integer: return "int";
    case StaticType::Real: return "real";
    case StaticType::Boolean: return "bool";
    case StaticType::String: return "string";
    case StaticType::None: return "none";
    case StaticType::Array: return "array";
    case StaticType::Tuple: return "tuple";
    case StaticType::Function: return "func";
    }
    return "unknown";
}

std::string_view symbol_kind_name(SymbolKind kind) noexcept {
    switch (kind) {
    case SymbolKind::Variable: return "variable";
    case SymbolKind::Parameter: return "parameter";
    case SymbolKind::LoopVariable: return "loop-variable";
    }
    return "unknown";
}

namespace {

using Scope = std::unordered_map<std::string, std::size_t>;

bool is_numeric(StaticType type) noexcept {
    return type == StaticType::Integer || type == StaticType::Real;
}

bool is_known(StaticType type) noexcept {
    return type != StaticType::Dynamic && type != StaticType::NotApplicable;
}

SemanticNode copy_node(const AstNode& node,
                       StaticType type = StaticType::NotApplicable) {
    return {node.kind, node.value, node.line, node.column, type, 0, {}};
}

[[noreturn]] void malformed(const AstNode& node, const std::string& detail) {
    throw std::invalid_argument("malformed AST at " + std::to_string(node.line) + ':' +
                                std::to_string(node.column) + ": " + detail);
}

void require_children(const AstNode& node, std::size_t count) {
    if (node.children.size() != count) {
        malformed(node, std::string(ast_kind_name(node.kind)) + " must have " +
                        std::to_string(count) + " child node(s)");
    }
}

class AnalyzerImpl {
public:
    SemanticModel run(const AstNode& program) {
        if (program.kind != AstKind::Program) {
            throw std::invalid_argument("semantic analyzer expects a Program AST root");
        }
        scopes_.emplace_back();
        model_.root = analyze_node(program);
        scopes_.pop_back();
        return std::move(model_);
    }

private:
    SemanticNode analyze_node(const AstNode& node) {
        switch (node.kind) {
        case AstKind::Program: return analyze_program(node);
        case AstKind::Block: return analyze_block(node, true);
        case AstKind::VariableDeclaration: return analyze_declaration(node);
        case AstKind::Assignment: return analyze_assignment(node);
        case AstKind::IfStatement:
        case AstKind::ShortIfStatement: return analyze_if(node);
        case AstKind::WhileLoop: return analyze_while(node);
        case AstKind::ForLoop: return analyze_for(node);
        case AstKind::InfiniteLoop: return analyze_infinite_loop(node);
        case AstKind::ExitStatement: return analyze_exit(node);
        case AstKind::ReturnStatement: return analyze_return(node);
        case AstKind::PrintStatement: return analyze_print(node);
        case AstKind::Identifier:
        case AstKind::IntegerLiteral:
        case AstKind::RealLiteral:
        case AstKind::StringLiteral:
        case AstKind::BooleanLiteral:
        case AstKind::NoneLiteral:
        case AstKind::ArrayLiteral:
        case AstKind::TupleLiteral:
        case AstKind::FunctionLiteral:
        case AstKind::UnaryExpression:
        case AstKind::BinaryExpression:
        case AstKind::TypeCheckExpression:
        case AstKind::IndexExpression:
        case AstKind::CallExpression:
        case AstKind::MemberExpression:
        case AstKind::TupleIndexExpression:
        case AstKind::RangeExpression:
            return analyze_expression(node);
        case AstKind::Variable:
        case AstKind::ParameterList:
        case AstKind::Parameter:
        case AstKind::ExpressionBody:
        case AstKind::BlockBody:
        case AstKind::TupleElement:
        case AstKind::TypeIndicator:
            malformed(node, std::string(ast_kind_name(node.kind)) +
                            " is not valid in this AST position");
        }
        malformed(node, "unknown AST node kind");
    }

    SemanticNode analyze_program(const AstNode& node) {
        SemanticNode result = copy_node(node);
        for (const auto& child : node.children) {
            result.children.push_back(analyze_node(child));
        }
        return result;
    }

    SemanticNode analyze_block(const AstNode& node, bool create_scope) {
        if (create_scope) {
            scopes_.emplace_back();
        }
        SemanticNode result = copy_node(node);
        for (const auto& child : node.children) {
            result.children.push_back(analyze_node(child));
        }
        if (create_scope) {
            scopes_.pop_back();
        }
        return result;
    }

    std::size_t declare(const AstNode& node, const std::string& name,
                        SymbolKind kind) {
        if (name.empty()) {
            malformed(node, "symbol name must not be empty");
        }
        auto& scope = scopes_.back();
        const auto existing = scope.find(name);
        if (existing != scope.end()) {
            const SymbolInfo& original = model_.symbols[existing->second - 1];
            throw SemanticError(node.line, node.column,
                "duplicate declaration of '" + name + "' in the same scope; first " +
                std::string(symbol_kind_name(original.kind)) + " is at " +
                std::to_string(original.line) + ':' + std::to_string(original.column));
        }
        const std::size_t id = model_.symbols.size() + 1;
        scope.emplace(name, id);
        model_.symbols.push_back(
            {id, name, kind, node.line, node.column, scopes_.size() - 1});
        return id;
    }

    std::size_t resolve(const AstNode& node) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto symbol = scope->find(node.value);
            if (symbol != scope->end()) {
                return symbol->second;
            }
        }
        throw SemanticError(node.line, node.column,
                            "use of undeclared identifier '" + node.value + "'");
    }

    SemanticNode analyze_declaration(const AstNode& node) {
        SemanticNode result = copy_node(node);
        if (node.children.empty()) {
            malformed(node, "VariableDeclaration must contain at least one Variable");
        }
        for (const AstNode& variable : node.children) {
            if (variable.kind != AstKind::Variable || variable.children.size() > 1) {
                malformed(variable, "invalid variable definition");
            }
            SemanticNode decorated = copy_node(variable);
            // A name becomes active at its declaration point. Declaring it before
            // its initializer also permits recursive closures.
            decorated.symbol_id = declare(variable, variable.value, SymbolKind::Variable);
            if (!variable.children.empty()) {
                decorated.children.push_back(analyze_expression(variable.children.front()));
            }
            result.children.push_back(std::move(decorated));
        }
        return result;
    }

    SemanticNode analyze_assignment(const AstNode& node) {
        require_children(node, 2);
        SemanticNode result = copy_node(node);
        result.children.push_back(analyze_expression(node.children[0]));
        result.children.push_back(analyze_expression(node.children[1]));
        return result;
    }

    void require_boolean(const SemanticNode& expression, const AstNode& source,
                         const std::string& context) const {
        if (is_known(expression.type) && expression.type != StaticType::Boolean) {
            throw SemanticError(source.line, source.column, context +
                " must be bool, but its statically known type is " +
                std::string(static_type_name(expression.type)));
        }
    }

    SemanticNode analyze_if(const AstNode& node) {
        const bool valid_short = node.kind == AstKind::ShortIfStatement &&
                                 node.children.size() == 2;
        const bool valid_full = node.kind == AstKind::IfStatement &&
                                (node.children.size() == 2 ||
                                 node.children.size() == 3);
        if (!valid_short && !valid_full) {
            malformed(node, "conditional has an invalid number of children");
        }
        SemanticNode result = copy_node(node);
        SemanticNode condition = analyze_expression(node.children[0]);
        require_boolean(condition, node.children[0], "if condition");
        result.children.push_back(std::move(condition));
        for (std::size_t i = 1; i < node.children.size(); ++i) {
            if (node.children[i].kind != AstKind::Block) {
                malformed(node.children[i], "conditional branch must be a Block");
            }
            result.children.push_back(analyze_block(node.children[i], true));
        }
        return result;
    }

    SemanticNode analyze_while(const AstNode& node) {
        require_children(node, 2);
        SemanticNode result = copy_node(node);
        SemanticNode condition = analyze_expression(node.children[0]);
        require_boolean(condition, node.children[0], "while condition");
        result.children.push_back(std::move(condition));
        ++loop_depth_;
        result.children.push_back(analyze_block(node.children[1], true));
        --loop_depth_;
        return result;
    }

    void require_integer_or_dynamic(const SemanticNode& expression,
                                    const AstNode& source,
                                    const std::string& context) const {
        if (is_known(expression.type) && expression.type != StaticType::Integer) {
            throw SemanticError(source.line, source.column, context +
                " must be int, but its statically known type is " +
                std::string(static_type_name(expression.type)));
        }
    }

    SemanticNode analyze_for(const AstNode& node) {
        require_children(node, 2);
        SemanticNode result = copy_node(node);
        SemanticNode sequence = analyze_expression(node.children[0]);
        if (node.children[0].kind == AstKind::RangeExpression) {
            require_integer_or_dynamic(sequence.children[0], node.children[0].children[0],
                                       "range start");
            require_integer_or_dynamic(sequence.children[1], node.children[0].children[1],
                                       "range end");
        } else if (is_known(sequence.type) && sequence.type != StaticType::Array &&
                   sequence.type != StaticType::Tuple) {
            throw SemanticError(node.children[0].line, node.children[0].column,
                "for sequence must be array or tuple, but its statically known type is " +
                std::string(static_type_name(sequence.type)));
        }
        result.children.push_back(std::move(sequence));

        scopes_.emplace_back();
        if (!node.value.empty()) {
            result.symbol_id = declare(node, node.value, SymbolKind::LoopVariable);
        }
        ++loop_depth_;
        result.children.push_back(analyze_block(node.children[1], false));
        --loop_depth_;
        scopes_.pop_back();
        return result;
    }

    SemanticNode analyze_infinite_loop(const AstNode& node) {
        require_children(node, 1);
        SemanticNode result = copy_node(node);
        ++loop_depth_;
        result.children.push_back(analyze_block(node.children[0], true));
        --loop_depth_;
        return result;
    }

    SemanticNode analyze_exit(const AstNode& node) const {
        require_children(node, 0);
        if (loop_depth_ == 0) {
            throw SemanticError(node.line, node.column,
                                "'exit' is allowed only inside a loop");
        }
        return copy_node(node);
    }

    SemanticNode analyze_return(const AstNode& node) {
        if (node.children.size() > 1) {
            malformed(node, "ReturnStatement may have at most one expression");
        }
        if (function_depth_ == 0) {
            throw SemanticError(node.line, node.column,
                                "'return' is allowed only inside a function");
        }
        SemanticNode result = copy_node(node);
        if (!node.children.empty()) {
            result.children.push_back(analyze_expression(node.children.front()));
        }
        return result;
    }

    SemanticNode analyze_print(const AstNode& node) {
        if (node.children.empty()) {
            malformed(node, "PrintStatement must contain an expression");
        }
        SemanticNode result = copy_node(node);
        for (const auto& child : node.children) {
            result.children.push_back(analyze_expression(child));
        }
        return result;
    }

    SemanticNode analyze_expression(const AstNode& node) {
        switch (node.kind) {
        case AstKind::Identifier: {
            require_children(node, 0);
            SemanticNode result = copy_node(node, StaticType::Dynamic);
            result.symbol_id = resolve(node);
            return result;
        }
        case AstKind::IntegerLiteral:
            require_children(node, 0);
            return copy_node(node, StaticType::Integer);
        case AstKind::RealLiteral:
            require_children(node, 0);
            return copy_node(node, StaticType::Real);
        case AstKind::StringLiteral:
            require_children(node, 0);
            return copy_node(node, StaticType::String);
        case AstKind::BooleanLiteral:
            require_children(node, 0);
            return copy_node(node, StaticType::Boolean);
        case AstKind::NoneLiteral:
            require_children(node, 0);
            return copy_node(node, StaticType::None);
        case AstKind::ArrayLiteral: {
            SemanticNode result = copy_node(node, StaticType::Array);
            for (const auto& child : node.children) {
                result.children.push_back(analyze_expression(child));
            }
            return result;
        }
        case AstKind::TupleLiteral: return analyze_tuple(node);
        case AstKind::FunctionLiteral: return analyze_function(node);
        case AstKind::UnaryExpression: return analyze_unary(node);
        case AstKind::BinaryExpression: return analyze_binary(node);
        case AstKind::TypeCheckExpression: return analyze_type_check(node);
        case AstKind::IndexExpression: return analyze_index(node);
        case AstKind::CallExpression: return analyze_call(node);
        case AstKind::MemberExpression:
        case AstKind::TupleIndexExpression: return analyze_tuple_access(node);
        case AstKind::RangeExpression: return analyze_range(node);
        default:
            malformed(node, std::string(ast_kind_name(node.kind)) +
                            " is not an expression");
        }
    }

    SemanticNode analyze_tuple(const AstNode& node) {
        if (node.children.empty()) {
            malformed(node, "TupleLiteral must contain at least one element");
        }
        SemanticNode result = copy_node(node, StaticType::Tuple);
        std::unordered_set<std::string> names;
        for (const AstNode& element : node.children) {
            if (element.kind != AstKind::TupleElement || element.children.size() != 1) {
                malformed(element, "invalid tuple element");
            }
            if (!element.value.empty() && !names.insert(element.value).second) {
                throw SemanticError(element.line, element.column,
                                    "duplicate tuple element name '" +
                                    element.value + "'");
            }
            SemanticNode decorated = copy_node(element);
            decorated.children.push_back(analyze_expression(element.children.front()));
            result.children.push_back(std::move(decorated));
        }
        return result;
    }

    SemanticNode analyze_function(const AstNode& node) {
        require_children(node, 2);
        const AstNode& parameters = node.children[0];
        const AstNode& body = node.children[1];
        if (parameters.kind != AstKind::ParameterList ||
            (body.kind != AstKind::ExpressionBody && body.kind != AstKind::BlockBody)) {
            malformed(node, "invalid function literal structure");
        }

        SemanticNode result = copy_node(node, StaticType::Function);
        SemanticNode decorated_parameters = copy_node(parameters);
        scopes_.emplace_back();
        for (const AstNode& parameter : parameters.children) {
            if (parameter.kind != AstKind::Parameter || !parameter.children.empty()) {
                malformed(parameter, "invalid function parameter");
            }
            SemanticNode decorated = copy_node(parameter);
            decorated.symbol_id = declare(parameter, parameter.value, SymbolKind::Parameter);
            decorated_parameters.children.push_back(std::move(decorated));
        }
        result.children.push_back(std::move(decorated_parameters));

        const std::size_t saved_loop_depth = loop_depth_;
        loop_depth_ = 0; // A nested function cannot exit a surrounding loop.
        ++function_depth_;
        SemanticNode decorated_body = copy_node(body);
        if (body.kind == AstKind::ExpressionBody) {
            require_children(body, 1);
            decorated_body.children.push_back(analyze_expression(body.children.front()));
        } else {
            require_children(body, 1);
            if (body.children.front().kind != AstKind::Block) {
                malformed(body, "BlockBody must contain a Block");
            }
            // Parameters and top-level locals share the function scope.
            decorated_body.children.push_back(analyze_block(body.children.front(), false));
        }
        --function_depth_;
        loop_depth_ = saved_loop_depth;
        scopes_.pop_back();
        result.children.push_back(std::move(decorated_body));
        return result;
    }

    SemanticNode analyze_unary(const AstNode& node) {
        require_children(node, 1);
        SemanticNode result = copy_node(node);
        SemanticNode operand = analyze_expression(node.children.front());
        if (node.value == "not") {
            if (is_known(operand.type) && operand.type != StaticType::Boolean) {
                operator_error(node, operand.type, std::nullopt);
            }
            result.type = StaticType::Boolean;
        } else if (node.value == "+" || node.value == "-") {
            if (is_known(operand.type) && !is_numeric(operand.type)) {
                operator_error(node, operand.type, std::nullopt);
            }
            result.type = operand.type;
        } else {
            malformed(node, "unknown unary operator '" + node.value + "'");
        }
        result.children.push_back(std::move(operand));
        return result;
    }

    [[noreturn]] void operator_error(const AstNode& node, StaticType left,
                                     std::optional<StaticType> right) const {
        std::string message = "operator '" + node.value + "' does not accept " +
                              std::string(static_type_name(left));
        if (right) {
            message += " and " + std::string(static_type_name(*right));
        }
        message += " operand";
        if (right) {
            message += 's';
        }
        throw SemanticError(node.line, node.column, message);
    }

    SemanticNode analyze_binary(const AstNode& node) {
        require_children(node, 2);
        SemanticNode result = copy_node(node);
        SemanticNode left = analyze_expression(node.children[0]);
        SemanticNode right = analyze_expression(node.children[1]);
        const StaticType lhs = left.type;
        const StaticType rhs = right.type;

        if (node.value == "and" || node.value == "or" || node.value == "xor") {
            if ((is_known(lhs) && lhs != StaticType::Boolean) ||
                (is_known(rhs) && rhs != StaticType::Boolean)) {
                operator_error(node, lhs, rhs);
            }
            result.type = StaticType::Boolean;
        } else if (node.value == "<" || node.value == "<=" ||
                   node.value == ">" || node.value == ">=" ||
                   node.value == "=" || node.value == "/=") {
            if ((is_known(lhs) && !is_numeric(lhs)) ||
                (is_known(rhs) && !is_numeric(rhs))) {
                operator_error(node, lhs, rhs);
            }
            result.type = StaticType::Boolean;
        } else if (node.value == "+") {
            const auto can_participate_in_addition = [](StaticType type) {
                return type == StaticType::Dynamic || is_numeric(type) ||
                       type == StaticType::String || type == StaticType::Array ||
                       type == StaticType::Tuple;
            };
            if (!can_participate_in_addition(lhs) ||
                !can_participate_in_addition(rhs)) {
                operator_error(node, lhs, rhs);
            } else if (lhs == StaticType::Dynamic || rhs == StaticType::Dynamic) {
                result.type = StaticType::Dynamic;
            } else if (is_numeric(lhs) && is_numeric(rhs)) {
                result.type = lhs == StaticType::Real || rhs == StaticType::Real
                                  ? StaticType::Real : StaticType::Integer;
            } else if (lhs == rhs && (lhs == StaticType::String ||
                       lhs == StaticType::Array || lhs == StaticType::Tuple)) {
                result.type = lhs;
            } else {
                operator_error(node, lhs, rhs);
            }
        } else if (node.value == "-" || node.value == "*" || node.value == "/") {
            if ((is_known(lhs) && !is_numeric(lhs)) ||
                (is_known(rhs) && !is_numeric(rhs))) {
                operator_error(node, lhs, rhs);
            }
            if (lhs == StaticType::Dynamic || rhs == StaticType::Dynamic) {
                result.type = StaticType::Dynamic;
            } else {
                result.type = lhs == StaticType::Real || rhs == StaticType::Real
                                  ? StaticType::Real : StaticType::Integer;
            }
        } else {
            malformed(node, "unknown binary operator '" + node.value + "'");
        }
        result.children.push_back(std::move(left));
        result.children.push_back(std::move(right));
        return result;
    }

    SemanticNode analyze_type_check(const AstNode& node) {
        require_children(node, 2);
        if (node.children[1].kind != AstKind::TypeIndicator) {
            malformed(node, "type check must end with TypeIndicator");
        }
        SemanticNode result = copy_node(node, StaticType::Boolean);
        result.children.push_back(analyze_expression(node.children[0]));
        result.children.push_back(copy_node(node.children[1]));
        return result;
    }

    SemanticNode analyze_index(const AstNode& node) {
        require_children(node, 2);
        SemanticNode result = copy_node(node, StaticType::Dynamic);
        SemanticNode aggregate = analyze_expression(node.children[0]);
        SemanticNode index = analyze_expression(node.children[1]);
        if (is_known(aggregate.type) && aggregate.type != StaticType::Array) {
            throw SemanticError(node.line, node.column,
                "indexing requires array, but the receiver's statically known type is " +
                std::string(static_type_name(aggregate.type)));
        }
        require_integer_or_dynamic(index, node.children[1], "array index");
        result.children.push_back(std::move(aggregate));
        result.children.push_back(std::move(index));
        return result;
    }

    SemanticNode analyze_call(const AstNode& node) {
        if (node.children.empty()) {
            malformed(node, "CallExpression must contain a callee");
        }
        SemanticNode result = copy_node(node, StaticType::Dynamic);
        SemanticNode callee = analyze_expression(node.children.front());
        if (is_known(callee.type) && callee.type != StaticType::Function) {
            throw SemanticError(node.line, node.column,
                "call requires func, but the callee's statically known type is " +
                std::string(static_type_name(callee.type)));
        }
        result.children.push_back(std::move(callee));
        for (std::size_t i = 1; i < node.children.size(); ++i) {
            result.children.push_back(analyze_expression(node.children[i]));
        }
        return result;
    }

    SemanticNode analyze_tuple_access(const AstNode& node) {
        require_children(node, 1);
        SemanticNode result = copy_node(node, StaticType::Dynamic);
        SemanticNode tuple = analyze_expression(node.children.front());
        if (is_known(tuple.type) && tuple.type != StaticType::Tuple) {
            throw SemanticError(node.line, node.column,
                "tuple access requires tuple, but the receiver's statically known type is " +
                std::string(static_type_name(tuple.type)));
        }
        if (node.kind == AstKind::TupleIndexExpression) {
            try {
                if (node.value.empty() || std::stoull(node.value) == 0) {
                    throw SemanticError(node.line, node.column,
                                        "tuple positions are one-based");
                }
            } catch (const std::invalid_argument&) {
                malformed(node, "tuple position is not an integer");
            } catch (const std::out_of_range&) {
                throw SemanticError(node.line, node.column,
                                    "tuple position is too large");
            }
        }
        result.children.push_back(std::move(tuple));
        return result;
    }

    SemanticNode analyze_range(const AstNode& node) {
        require_children(node, 2);
        SemanticNode result = copy_node(node, StaticType::NotApplicable);
        result.children.push_back(analyze_expression(node.children[0]));
        result.children.push_back(analyze_expression(node.children[1]));
        return result;
    }

    SemanticModel model_;
    std::vector<Scope> scopes_;
    std::size_t loop_depth_ = 0;
    std::size_t function_depth_ = 0;
};

void print_escaped(std::ostream& out, const std::string& value) {
    constexpr char hex[] = "0123456789abcdef";
    out << '"';
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20 || ch == 0x7f) {
                out << "\\u00" << hex[ch >> 4] << hex[ch & 0x0f];
            } else {
                out << static_cast<char>(ch);
            }
        }
    }
    out << '"';
}

} // namespace

SemanticModel SemanticAnalyzer::analyze(const AstNode& program) const {
    return AnalyzerImpl().run(program);
}

void print_semantic_tree(std::ostream& out, const SemanticNode& node,
                         std::size_t indent) {
    for (std::size_t i = 0; i < indent; ++i) {
        out << "  ";
    }
    out << ast_kind_name(node.kind);
    if (!node.value.empty()) {
        out << ' ';
        print_escaped(out, node.value);
    }
    if (node.type != StaticType::NotApplicable) {
        out << " [type=" << static_type_name(node.type) << ']';
    }
    if (node.symbol_id != 0) {
        out << " [symbol=#" << node.symbol_id << ']';
    }
    out << '\n';
    for (const auto& child : node.children) {
        print_semantic_tree(out, child, indent + 1);
    }
}

std::ostream& operator<<(std::ostream& out, const SemanticModel& model) {
    print_semantic_tree(out, model.root);
    return out;
}

} // namespace dlang
