#pragma once

#include "cfg.hpp"
#include <vector>

namespace mitscript::analysis {

struct InlineConfig {
    int max_inline_instructions = 40;
};

// Run module-level inlining starting from the toplevel function.
void run_inlining_pass(mitscript::CFG::FunctionCFG& root, const InlineConfig& cfg = {});

} // namespace mitscript::analysis
