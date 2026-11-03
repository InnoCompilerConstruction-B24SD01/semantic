#pragma once

#include "dlang/ast.hpp"

namespace dlang {

class Optimizer {
public:
    // Validate the entire program first, then return a rewritten AST copy.
    // Fold literal integer/boolean expressions and remove statements after a
    // direct return/exit in the same block. The input AST stays unchanged.
    AstNode optimize(const AstNode& program) const;
};

} // namespace dlang
