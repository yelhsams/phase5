#pragma once

#include "cfg.hpp"
#include <vector>

namespace mitscript::analysis {

// Simple liveness set that tracks virtual registers and locals separately.
struct LiveSet {
    std::vector<char> vregs;
    std::vector<char> locals;

    LiveSet() = default;
    LiveSet(size_t vreg_count, size_t local_count);

    bool operator==(const LiveSet& other) const;
    bool operator!=(const LiveSet& other) const { return !(*this == other); }
};

struct LivenessResult {
    std::vector<LiveSet> live_in;
    std::vector<LiveSet> live_out;
    size_t num_vregs = 0;
    size_t num_locals = 0;
};

// Removes CFG blocks unreachable from the entry.
void eliminate_unreachable_blocks(mitscript::CFG::FunctionCFG& fn);

// Fixed-point liveness computation across the CFG.
LivenessResult compute_liveness(mitscript::CFG::FunctionCFG& fn);

// Deletes dead, side-effect-free instructions using the provided liveness info.
void eliminate_dead_instructions(mitscript::CFG::FunctionCFG& fn,
                                 const LivenessResult& liveness);

// Runs the full DCE pipeline (reachability + liveness + per-instruction DCE).
void run_dce_on_function(mitscript::CFG::FunctionCFG& fn);

} // namespace mitscript::analysis
