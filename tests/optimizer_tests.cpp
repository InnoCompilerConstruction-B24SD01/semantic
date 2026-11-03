#include "dlang/lexer.hpp"
#include "dlang/optimizer.hpp"
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

#define CHECK(condition) do { \
    if (!(condition)) { \
        throw std::runtime_error(std::string(__FILE__) + ':' + \
            std::to_string(__LINE__) + ": " #condition); \
    } \
} while (false)

dlang::AstNode parse(const std::string& source) {
    dlang::Lexer lexer(source, dlang::NewlineMode::Emit);
    dlang::Parser parser(lexer.tokenize());
    return parser.parse();
}

dlang::AstNode optimize(const std::string& source) {
    auto ast = dlang::Optimizer().optimize(parse(source));
    // Re-analysis must produce a valid model for the actual rewritten AST.
    (void)dlang::SemanticAnalyzer().analyze(ast);
    return ast;
}

std::string tree(const dlang::AstNode& node) {
    std::ostringstream out;
    out << node;
    return out.str();
}

void constant_arithmetic() {
    const auto ast = optimize("print (5 + 3) * 2, 9 - 4, -5 + +2, -(-3)\n");
    const auto& values = ast.children[0].children;
    const std::vector<std::string> expected{"16", "5", "-3", "3"};
    CHECK(values.size() == expected.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        CHECK(values[i].kind == K::IntegerLiteral);
        CHECK(values[i].value == expected[i]);
        CHECK(values[i].children.empty());
    }
}

void comparisons_and_booleans() {
    const auto ast = optimize(
        "print 3 < 5, 3 <= 3, 5 > 3, 3 >= 5, 3 = 3, 3 /= 3\n"
        "print not (3 < 5), true and false, false or true, true xor true\n");
    const std::vector<std::vector<std::string>> expected{
        {"true", "true", "true", "false", "true", "false"},
        {"false", "false", "true", "false"},
    };
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto& values = ast.children[i].children;
        CHECK(values.size() == expected[i].size());
        for (std::size_t j = 0; j < values.size(); ++j) {
            CHECK(values[j].kind == K::BooleanLiteral);
            CHECK(values[j].value == expected[i][j]);
        }
    }
}

void runtime_expressions_are_preserved() {
    const std::string source =
        "var x := 5\n"
        "var f := func() => 2\n"
        "print x + 3, f() * 0, 1 / 0, 7 / 2, 1 + 2.5, 'a' + 'b'\n";
    const auto ast = parse(source);
    CHECK(tree(dlang::Optimizer().optimize(ast)) == tree(ast));
}

void integer_boundaries() {
    const std::vector<std::string> unchanged{
        "9223372036854775807 + 1",
        "9223372036854775808 + 0",
        "9223372036854775807 * 2",
        "(-9223372036854775807 - 1) - 1",
        "(-9223372036854775807 - 1) * -1",
        "-1 * (-9223372036854775807 - 1)",
    };
    for (const auto& expression : unchanged) {
        const auto ast = optimize("print " + expression + "\n");
        CHECK(ast.children[0].children[0].kind == K::BinaryExpression);
    }
    const auto negation = optimize("print -(-9223372036854775807 - 1)\n");
    CHECK(negation.children[0].children[0].kind == K::UnaryExpression);
    const auto ast = optimize(
        "print 9223372036854775806 + 1, -9223372036854775807 - 1, "
        "0 * (-9223372036854775807 - 1), -3 * -4, -3 * 4, 3 * -4\n");
    const std::vector<std::string> expected{
        "9223372036854775807", "-9223372036854775808", "0", "12", "-12", "-12",
    };
    const auto& values = ast.children[0].children;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(values[i].kind == K::IntegerLiteral);
        CHECK(values[i].value == expected[i]);
    }
}

void remove_after_return() {
    const auto ast = optimize(
        "var f := func() is\n"
        " print 1\n return 5 + 3\n print 999\n var unused := 10\nend\n"
        "print f()\n");
    const auto& function = ast.children[0].children[0].children[0];
    const auto& block = function.children[1].children[0];
    CHECK(block.children.size() == 2);
    CHECK(block.children[0].kind == K::PrintStatement);
    CHECK(block.children[1].kind == K::ReturnStatement);
    CHECK(block.children[1].children[0].value == "8");
    CHECK(ast.children.size() == 2); // Return does not terminate the program.
    const auto model = dlang::SemanticAnalyzer().analyze(ast);
    CHECK(model.symbols.size() == 1); // No stale record for removed 'unused'.
    CHECK(model.root.children[1].children[0].children[0].symbol_id == 1);
    const auto bare = optimize("var f := func() is\n return\n print 999\nend\n");
    CHECK(bare.children[0].children[0].children[0].children[1]
              .children[0].children.size() == 1);
}

void remove_after_exit() {
    const auto ast = optimize(
        "loop\n print 1\n exit\n print 999\nend\nprint 2\n");
    const auto& block = ast.children[0].children[0];
    CHECK(block.children.size() == 2);
    CHECK(block.children.back().kind == K::ExitStatement);
    CHECK(ast.children.size() == 2); // Exit does not terminate the program.
}

void nested_control_flow_is_preserved() {
    const auto ast = optimize(
        "var f := func(flag) is\n"
        " if flag then\n return 1\n print 999\nend\n"
        " loop\n exit\n print 999\nend\n"
        " var g := func() is\n return 2\n print 999\nend\n"
        " print 3\n return g()\nend\n");
    const auto& block = ast.children[0].children[0].children[0].children[1].children[0];
    CHECK(block.children.size() == 5);
    CHECK(block.children[0].children[1].children.size() == 1);
    CHECK(block.children[1].children[0].children.size() == 1);
    CHECK(block.children[3].kind == K::PrintStatement);
    CHECK(block.children[4].kind == K::ReturnStatement);
}

void invalid_dead_code_is_still_rejected() {
    const std::vector<std::string> invalid{
        "var f := func() is\n return 1\n print missing\nend\n",
        "loop\n exit\n print 1 + 'x'\nend\n",
        "var f := func() is\n return 1\n exit\nend\n",
        "return 1\n",
    };
    for (const auto& source : invalid) {
        bool rejected = false;
        try {
            (void)dlang::Optimizer().optimize(parse(source));
        } catch (const dlang::SemanticError&) {
            rejected = true;
        }
        CHECK(rejected);
    }
}

void source_positions_input_and_idempotence() {
    const auto original = parse("print 5 + 3\n");
    const auto before = tree(original);
    const auto optimized = dlang::Optimizer().optimize(original);
    const auto& expression = original.children[0].children[0];
    const auto& literal = optimized.children[0].children[0];
    CHECK(literal.line == expression.line);
    CHECK(literal.column == expression.column);
    CHECK(tree(original) == before);
    CHECK(tree(optimized) == tree(dlang::Optimizer().optimize(optimized)));
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"nested integer constant folding", constant_arithmetic},
        {"constant comparisons and boolean operators", comparisons_and_booleans},
        {"dynamic values, calls, division and reals are preserved", runtime_expressions_are_preserved},
        {"integer boundaries and overflow", integer_boundaries},
        {"unreachable code after return", remove_after_return},
        {"unreachable code after exit", remove_after_exit},
        {"nested control flow does not terminate parent blocks", nested_control_flow_is_preserved},
        {"validation precedes optimization", invalid_dead_code_is_still_rejected},
        {"source positions, unchanged input and idempotence", source_positions_input_and_idempotence},
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
              << " optimizer unit tests passed\n";
    return failed == 0 ? 0 : 1;
}
