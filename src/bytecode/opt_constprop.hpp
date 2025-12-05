#pragma once

#include "types.hpp" // for bytecode::Function
#include <optional>
#include <vector>

namespace bytecode::opt_constprop {

// Run constant propagation and folding on this function and all its nested
// functions. This pass:
// - Tracks constant values in local variables and on the stack
// - Folds operations with known constant operands (e.g., Add with two
// constants)
// - Replaces operations with LoadConst when the result is known
void constant_propagate(Function *func);

} // namespace bytecode::opt_constprop
