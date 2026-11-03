#include "dlang/lexer.hpp"
#include "dlang/optimizer.hpp"
#include "dlang/parser.hpp"
#include "dlang/semantic.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

int main(int argc, char* argv[]) {
    const bool optimize = argc == 3 && std::string_view(argv[1]) == "--optimize";
    if ((argc != 2 && !optimize) ||
        (argc == 2 && std::string_view(argv[1]) == "--optimize")) {
        std::cerr << "Usage: " << argv[0] << " [--optimize] <source.d>\n";
        return 2;
    }

    const char* path = argv[optimize ? 2 : 1];
    std::error_code file_error;
    if (std::filesystem::is_directory(path, file_error)) {
        std::cerr << path << ": cannot read source file: path is a directory\n";
        return 1;
    }
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
        dlang::AstNode ast = parser.parse();
        if (optimize) {
            ast = dlang::Optimizer().optimize(ast);
        }
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
