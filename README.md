# Project D semantic analyzer

A C++17 semantic analyzer for **Project D — Dynamic Language**. It reuses the
sibling lexer and parser, validates the resulting AST, resolves every identifier
to a declaration, and prints an annotated syntax tree (AAST).

The implementation follows `../Project D.pdf` and the tree-walk approach from
`../2026 CC BS3 07 Semantic Analysis.pdf`.

Подробное русскоязычное руководство: [PDF](docs/semantic_analyzer_ru.pdf)
([редактируемый HTML](docs/semantic_analyzer_ru.html)).

## Build, run, and test

Run commands from this directory:

```sh
cd /home/gabdullinx/Inbox/IU/compilers/semantic
make
./dsemantic examples/01_types_and_bindings.d
make test
```

Analyze another Project D program with:

```sh
./dsemantic path/to/program.d
```

On success the CLI prints an AAST. For example, `var x := 1 + 2.5` contains:

```text
Variable "x" [symbol=#1]
  BinaryExpression "+" [type=real]
    IntegerLiteral "1" [type=int]
    RealLiteral "2.5" [type=real]
```

`symbol=#1` links declarations and uses. `type=dynamic` means that the exact
runtime type cannot be known soundly at this stage—not that analysis failed.

Exit status is `0` on success, `1` for a lexical, syntax, semantic, or file I/O
error, and `2` for an invalid command line. Errors go to stderr and invalid input
produces no partial AAST:

```text
program.d: semantic error at 3:7: use of undeclared identifier 'value'
```

Use `make clean` to remove build products. Use `make docs` to rebuild the PDF.

## What is checked

The analyzer performs checks that are statically sound for this dynamic language:

- every identifier is declared before use and is linked to its declaration;
- one name may be declared only once in a scope; nested scopes may shadow it;
- parameters and function-top-level locals share one function scope;
- loop variables exist only inside their loop bodies;
- closures may capture visible variables from enclosing scopes;
- `return` is accepted only within a function;
- `exit` is accepted only within a loop of the same function;
- named tuple elements and function parameters must be unique;
- tuple positions are one-based;
- literal-only/operator expressions receive exact static type annotations;
- conditions, ranges, and operators are rejected when their known types make the
  operation impossible.

Project D variables can change type after assignment. An identifier therefore has
the conservative static type `dynamic`, even if its declaration has a literal
initializer. Operations involving `dynamic` values are intentionally deferred to
the future interpreter. This avoids rejecting valid programs such as:

```text
var value := 1
value := "now a string"
print value
```

The analyzer does not execute programs, evaluate runtime values, prove that every
control-flow path returns, or validate a dynamic call's argument count.

## Scopes

The program owns the outer scope. Each `if` branch and loop body owns a nested
scope. A function creates a scope containing its parameters and top-level locals;
nested statements create further scopes. Lookup walks from the innermost scope to
the program scope, which implements shadowing and closure capture.

A declaration becomes active at its declaration point, before its initializer is
analyzed. This supports recursive function values:

```text
var factorial := func(n) => factorial(n - 1)
```

## Layout

```text
include/dlang/semantic.hpp  Public semantic model, diagnostics, analyzer, printer
src/semantic.cpp            Scope walk, checks, type deduction, AAST printer
src/main.cpp                Complete source -> tokens -> AST -> AAST CLI
tests/semantic_tests.cpp    Semantic API and rule unit tests
tests/test_integration.py   Real CLI and all 15 supplied-program tests
examples/                   Valid golden AASTs and one invalid source
docs/                       Russian PDF, HTML source, and PDF build script
Makefile                    Build, test, documentation, and clean targets
```

The Makefile compiles the existing front end directly from:

```text
../lexer/src/{lexer,token}.cpp
../syntax/src/{parser,ast}.cpp
```

Nothing is copied or forked, so the semantic stage uses exactly the lexer, parser,
tokens, and AST already present in this workspace.

## Library API

```cpp
dlang::Lexer lexer(source, dlang::NewlineMode::Emit);
dlang::Parser parser(lexer.tokenize());
dlang::AstNode ast = parser.parse();
dlang::SemanticModel model = dlang::SemanticAnalyzer().analyze(ast);
```

`SemanticModel::root` is the decorated tree. `SemanticModel::symbols` is the symbol
table with stable one-based IDs, declaration kind and position, and scope depth.
`SemanticError` exposes one-based `line()` and `column()` accessors.

## Tests and examples

`make test` runs both layers:

1. Twelve C++ unit-test groups cover bindings, declaration order, shadowing,
   closures, duplicates, control-flow context, tuple rules, type deductions,
   errors, stable output, and the public API.
2. Python integration tests launch `dsemantic`, check all 15 supplied Project D
   programs, compare golden AAST files, validate diagnostics and exit statuses,
   and confirm that lexical and syntax errors remain distinguishable.

Exact example comparison:

```sh
./dsemantic examples/02_scopes_and_closure.d |
    diff -u examples/02_scopes_and_closure.sem -
```

The tuple snippet in `Project D.pdf` uses undeclared placeholder identifiers `c`
and `d`; a semantic analyzer correctly rejects that isolated snippet unless those
variables are declared first. The 15 complete course programs are all accepted.
