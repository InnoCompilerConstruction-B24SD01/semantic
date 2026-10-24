#include "dlang/lexer.hpp"
#include "dlang/parser.hpp"
#include "dlang/semantic.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <source.d>\n";
        return 2;
    }

    const char* path = argv[1];
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << path << ": cannot open source file\n";
        return 1;
    }

    try {
        input.exceptions(std::ios::badbit);
        std::string source{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
        dlang::Lexer lexer(std::move(source), dlang::NewlineMode::Emit);
        dlang::Parser parser(lexer.tokenize());
        const dlang::AstNode ast = parser.parse();
        const dlang::SemanticModel model = dlang::SemanticAnalyzer().analyze(ast);
        std::cout << model;
        if (!std::cout) {
            std::cerr << "cannot write annotated syntax tree\n";
            return 1;
        }
    } catch (const dlang::LexicalError& error) {
        std::cerr << path << ": " << error.what() << '\n';
        return 1;
    } catch (const dlang::SyntaxError& error) {
        std::cerr << path << ": " << error.what() << '\n';
        return 1;
    } catch (const dlang::SemanticError& error) {
        std::cerr << path << ": " << error.what() << '\n';
        return 1;
    } catch (const std::ios_base::failure& error) {
        std::cerr << path << ": cannot read source file: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
