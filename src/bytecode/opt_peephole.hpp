#pragma once

#include "types.hpp"

namespace bytecode::opt_peephole {

// Perform peephole optimizations on bytecode.
// This includes:
// - Constant folding: LoadConst X; LoadConst Y; Add -> LoadConst Z
// - Redundant instruction removal: Not; Not -> (nothing)
// - Dead Pop removal after stores
void peephole_optimize(Function *func);

} // namespace bytecode::opt_peephole
