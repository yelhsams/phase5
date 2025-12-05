// bytecode/opt_inline.hpp
#pragma once

#include "types.hpp"    // for bytecode::Function
#include <vector>

namespace bytecode::opt_inline {

// Run function inlining on this function and all its nested functions.
void inline_functions(Function* func);

} // namespace bytecode::opt_inline