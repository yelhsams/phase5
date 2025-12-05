#pragma once

#include "types.hpp"

namespace bytecode::opt_licm {

// Convert stack code to register form (if not already present) and perform a
// lightweight LICM pass to hoist loop-invariant, side-effect-free
// computations.
void run(Function *func);

} // namespace bytecode::opt_licm

