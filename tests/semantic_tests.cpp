#include "dlang/lexer.hpp"
#include "dlang/parser.hpp"
#include "dlang/semantic.hpp"

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using K = dlang::AstKind;
using T = dlang::StaticType;

#define CHECK(condition) do { \
    if (!(condition)) { \
        throw std::runtime_error(std::string(__FILE__) + ':' + \
            std::to_string(__LINE__) + ": " #condition); \
    } \
} while (false)

dlang::SemanticModel analyze(const std::string& source) {
    dlang::Lexer lexer(source, dlang::NewlineMode::Emit);
    dlang::Parser parser(lexer.tokenize());
    return dlang::SemanticAnalyzer().analyze(parser.parse());
}

void expect_semantic_error(const std::string& source, std::size_t line,
                           std::size_t column, const std::string& message_part) {
    try {
        (void)analyze(source);
    } catch (const dlang::SemanticError& error) {
        CHECK(error.line() == line);
        CHECK(error.column() == column);
        CHECK(std::string(error.what()).find(message_part) != std::string::npos);
        return;
    }
    throw std::runtime_error("expected SemanticError");
}

void declarations_and_bindings() {
    const auto model = analyze(
        "var x := 1\n"
        "if true then\n"
        "  var x := x + 1\n"
        "  print x\n"
        "end\n"
        "print x\n");
    CHECK(model.symbols.size() == 2);
    CHECK(model.symbols[0].name == "x");
    CHECK(model.symbols[0].scope_depth == 0);
    CHECK(model.symbols[1].name == "x");
    CHECK(model.symbols[1].scope_depth == 1);

    const auto& conditional = model.root.children[1];
    const auto& inner_variable = conditional.children[1].children[0].children[0];
    CHECK(inner_variable.symbol_id == 2);
    CHECK(inner_variable.children[0].children[0].symbol_id == 2);
    CHECK(conditional.children[1].children[1].children[0].symbol_id == 2);
    CHECK(model.root.children[2].children[0].symbol_id == 1);
}

void declaration_order_and_recursive_closures() {
    const auto model = analyze(
        "var factorial := func(n) => factorial(n - 1)\n"
        "var a := 1, b := a\n"
        "print b\n");
    CHECK(model.symbols.size() == 4);
    CHECK(model.symbols[0].name == "factorial");
    CHECK(model.symbols[1].name == "n");
    CHECK(model.symbols[2].name == "a");
    CHECK(model.symbols[3].name == "b");
    expect_semantic_error("print later\nvar later := 1\n", 1, 7, "undeclared");
}

void duplicate_declarations() {
    expect_semantic_error("var x\nvar x\n", 2, 5, "duplicate declaration");
    expect_semantic_error("var x, x\n", 1, 8, "duplicate declaration");
    expect_semantic_error("var f := func(a, a) => a\n", 1, 18, "duplicate declaration");
    expect_semantic_error(
        "var f := func(a) is\n  var a := 1\n  return a\nend\n",
        2, 7, "duplicate declaration");
    // Separate branches are separate scopes.
    (void)analyze("if true then\n var x\nelse\n var x\nend\n");
}

void undeclared_references() {
    expect_semantic_error("x := 1\n", 1, 1, "undeclared identifier 'x'");
    expect_semantic_error("var x := missing\n", 1, 10, "undeclared identifier 'missing'");
    expect_semantic_error("for item in [] loop\n print other\nend\n", 2, 8,
                          "undeclared identifier 'other'");
    expect_semantic_error("for item in [] loop\n print item\nend\nprint item\n", 4, 7,
                          "undeclared identifier 'item'");
}

void function_scopes_and_captures() {
    const auto model = analyze(
        "var outer := 10\n"
        "var make := func(step) is\n"
        "  var local := outer\n"
        "  return func(x) => x + step + local\n"
        "end\n");
    CHECK(model.symbols.size() == 5);
    CHECK(model.symbols[0].name == "outer");
    CHECK(model.symbols[1].name == "make");
    CHECK(model.symbols[2].name == "step");
    CHECK(model.symbols[3].name == "local");
    CHECK(model.symbols[4].name == "x");
}

void return_and_exit_contexts() {
    expect_semantic_error("return 1\n", 1, 1, "only inside a function");
    expect_semantic_error("exit\n", 1, 1, "only inside a loop");
    (void)analyze("loop\n if true => exit\nend\n");
    (void)analyze("var f := func() is\n return 1\nend\n");
    expect_semantic_error(
        "loop\n var f := func() is\n  exit\n end\nend\n",
        3, 3, "only inside a loop");
}

void tuple_rules() {
    (void)analyze("var t := {x := 1, 2, y := true}\nprint t.x, t.2\n");
    expect_semantic_error("var t := {x := 1, x := 2}\n", 1, 19,
                          "duplicate tuple element name 'x'");
    expect_semantic_error("var t := {1}\nprint t.0\n", 2, 8, "one-based");
}

void expression_types() {
    const auto model = analyze(
        "print 1 + 2, 1 + 2.5, 'a' + 'b', [1] + [2], {1} + {2}\n"
        "print 1 < 2, true and false, not false, -2.5\n");
    const auto& first = model.root.children[0].children;
    CHECK(first[0].type == T::Integer);
    CHECK(first[1].type == T::Real);
    CHECK(first[2].type == T::String);
    CHECK(first[3].type == T::Array);
    CHECK(first[4].type == T::Tuple);
    const auto& second = model.root.children[1].children;
    CHECK(second[0].type == T::Boolean);
    CHECK(second[1].type == T::Boolean);
    CHECK(second[2].type == T::Boolean);
    CHECK(second[3].type == T::Real);
}

void statically_known_type_errors() {
    expect_semantic_error("print 1 + 'x'\n", 1, 9, "int and string");
    expect_semantic_error("print true * 2\n", 1, 12, "bool and int");
    expect_semantic_error("var x\nprint true + x\n", 2, 12, "bool and dynamic");
    expect_semantic_error("var x\nprint none + x\n", 2, 12, "none and dynamic");
    expect_semantic_error("print not 1\n", 1, 7, "does not accept int");
    expect_semantic_error("print 'a' = 'b'\n", 1, 11, "string and string");
    expect_semantic_error("if 1 => print 1\n", 1, 4, "if condition must be bool");
    expect_semantic_error("while 'yes' loop\n print 1\nend\n", 1, 7,
                          "while condition must be bool");
    expect_semantic_error("for 3 loop\n print 1\nend\n", 1, 5,
                          "for sequence must be array or tuple");
    expect_semantic_error("for 1.5..2 loop\n print 1\nend\n", 1, 5,
                          "range start must be int");
}

void dynamic_values_defer_runtime_checks() {
    // A variable can change type at runtime, so its expression type stays
    // dynamic and operand/call/index/member checks are deferred.
    const auto model = analyze(
        "var value := 1\n"
        "value := 'text'\n"
        "var other\n"
        "print value + other, value(2), value[3], value.name\n");
    const auto& expressions = model.root.children[3].children;
    for (const auto& expression : expressions) {
        CHECK(expression.type == T::Dynamic);
    }
}

void annotated_tree_output() {
    std::ostringstream output;
    output << analyze("var result := 1 + 2.5\nprint result is real\n");
    CHECK(output.str() ==
        "Program\n"
        "  VariableDeclaration\n"
        "    Variable \"result\" [symbol=#1]\n"
        "      BinaryExpression \"+\" [type=real]\n"
        "        IntegerLiteral \"1\" [type=int]\n"
        "        RealLiteral \"2.5\" [type=real]\n"
        "  PrintStatement\n"
        "    TypeCheckExpression \"is\" [type=bool]\n"
        "      Identifier \"result\" [type=dynamic] [symbol=#1]\n"
        "      TypeIndicator \"real\"\n");
}

void analyzer_api_contract() {
    dlang::SemanticAnalyzer analyzer;
    const auto first = analyze("var x := 1\n");
    const auto second = analyze("var y := true\n");
    CHECK(first.symbols[0].id == 1);
    CHECK(second.symbols[0].id == 1);

    bool rejected = false;
    try {
        dlang::AstNode not_program{K::IntegerLiteral, "1", 1, 1, {}};
        (void)analyzer.analyze(not_program);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"declarations, scopes, shadowing, and bindings", declarations_and_bindings},
        {"declaration order and recursive closures", declaration_order_and_recursive_closures},
        {"duplicate declarations and parameters", duplicate_declarations},
        {"undeclared references", undeclared_references},
        {"function scopes and captures", function_scopes_and_captures},
        {"return and exit contexts", return_and_exit_contexts},
        {"tuple name and position rules", tuple_rules},
        {"static expression type annotations", expression_types},
        {"statically known type errors", statically_known_type_errors},
        {"dynamic values defer runtime checks", dynamic_values_defer_runtime_checks},
        {"stable annotated tree output", annotated_tree_output},
        {"semantic analyzer API contract", analyzer_api_contract},
    };

    std::size_t failed = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "PASS " << test.first << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
        }
    }
    std::cout << tests.size() - failed << '/' << tests.size()
              << " semantic unit tests passed\n";
    return failed == 0 ? 0 : 1;
}
