#pragma once

#include "types.hpp" // for bytecode::Function
#include <optional>
#include <vector>

namespace bytecode::opt_deadcode {

// Run dead code elimination on this function and all its nested functions.
// This pass:
// - Removes unreachable code (after unconditional jumps, returns, etc.)
// - Removes dead stores (stores to local variables that are never read)
// - Removes unused constants
void eliminate_dead_code(Function *func);

} // namespace bytecode::opt_deadcode
