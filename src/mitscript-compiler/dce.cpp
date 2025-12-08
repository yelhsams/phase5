#include "dce.hpp"

#include <algorithm>
#include <queue>

using namespace mitscript::CFG;

namespace mitscript::analysis {

LiveSet::LiveSet(size_t vreg_count, size_t local_count)
    : vregs(vreg_count, 0), locals(local_count, 0) {}

bool LiveSet::operator==(const LiveSet& other) const {
    return vregs == other.vregs && locals == other.locals;
}

namespace {

struct DefUse {
    int def_vreg = -1;
    int def_local = -1;
    std::vector<int> use_vregs;
    std::vector<int> use_locals;
};

size_t count_vregs(const FunctionCFG& fn) {
    int max_v = -1;
    for (const auto& blk_ptr : fn.blocks) {
        if (!blk_ptr) continue;
        for (const auto& ir : blk_ptr->code) {
            if (ir.output && ir.output->kind == IROperand::VREG) {
                max_v = std::max(max_v, ir.output->i);
            }
            for (const auto& inp : ir.inputs) {
                if (inp.kind == IROperand::VREG) {
                    max_v = std::max(max_v, inp.i);
                }
            }
        }
        switch (blk_ptr->term.kind) {
            case Terminator::Kind::CondJump:
            case Terminator::Kind::Return:
                max_v = std::max(max_v, blk_ptr->term.condition);
                break;
            case Terminator::Kind::Jump:
                break;
        }
    }
    return (max_v < 0) ? 0u : static_cast<size_t>(max_v + 1);
}

size_t count_locals(const FunctionCFG& fn) {
    return fn.params.size() + fn.locals.size();
}

LiveSet unite(const LiveSet& a, const LiveSet& b) {
    LiveSet out(a.vregs.size(), a.locals.size());
    for (size_t i = 0; i < a.vregs.size(); ++i) {
        out.vregs[i] = static_cast<char>(a.vregs[i] || b.vregs[i]);
    }
    for (size_t i = 0; i < a.locals.size(); ++i) {
        out.locals[i] = static_cast<char>(a.locals[i] || b.locals[i]);
    }
    return out;
}

void add_use_vreg(LiveSet& live, size_t idx) {
    if (idx < live.vregs.size()) live.vregs[idx] = 1;
}

void add_use_local(LiveSet& live, size_t idx) {
    if (idx < live.locals.size()) live.locals[idx] = 1;
}

void kill_def_vreg(LiveSet& live, size_t idx) {
    if (idx < live.vregs.size()) live.vregs[idx] = 0;
}

void kill_def_local(LiveSet& live, size_t idx) {
    if (idx < live.locals.size()) live.locals[idx] = 0;
}

DefUse analyze_instr(const IRInstr& ir) {
    DefUse du;

    if (ir.output && ir.output->kind == IROperand::VREG) {
        du.def_vreg = ir.output->i;
    }

    switch (ir.op) {
        case IROp::StoreLocal:
            if (!ir.inputs.empty() && ir.inputs[0].kind == IROperand::LOCAL) {
                du.def_local = ir.inputs[0].i;
            }
            if (ir.inputs.size() >= 2 && ir.inputs[1].kind == IROperand::VREG) {
                du.use_vregs.push_back(ir.inputs[1].i);
            }
            break;

        case IROp::LoadLocal:
            if (!ir.inputs.empty() && ir.inputs[0].kind == IROperand::LOCAL) {
                du.use_locals.push_back(ir.inputs[0].i);
            }
            break;

        default:
            for (const auto& in : ir.inputs) {
                if (in.kind == IROperand::VREG) du.use_vregs.push_back(in.i);
                if (in.kind == IROperand::LOCAL) du.use_locals.push_back(in.i);
            }
            break;
    }

    return du;
}

bool instr_has_side_effects(const IRInstr& ins) {
    switch (ins.op) {
        case IROp::Add:
        case IROp::Sub:
        case IROp::Mul:
        case IROp::Div:
        case IROp::CmpEq:
        case IROp::CmpLt:
        case IROp::CmpGt:
        case IROp::CmpLe:
        case IROp::CmpGe:
        case IROp::Neg:
        case IROp::Not:
        case IROp::And:
        case IROp::Or:
        case IROp::LoadConst:
        case IROp::LoadLocal:
        case IROp::LoadField:
        case IROp::LoadIndex:
            return false;

        case IROp::StoreLocal:
            // Treated as removable if the local is dead.
            return false;

        default:
            return true;
    }
}

void add_terminator_uses(const BasicBlock& blk, LiveSet& live) {
    switch (blk.term.kind) {
        case Terminator::Kind::CondJump:
        case Terminator::Kind::Return:
            add_use_vreg(live, static_cast<size_t>(blk.term.condition));
            break;
        case Terminator::Kind::Jump:
            break;
    }
}

void clean_edges(FunctionCFG& fn, const std::vector<char>& reachable) {
    auto is_dead = [&](BlockId id) {
        return id < 0 || id >= (BlockId)reachable.size() || !reachable[id];
    };

    for (auto& blk_ptr : fn.blocks) {
        if (!blk_ptr) continue;
        auto& blk = *blk_ptr;

        blk.successors.erase(
            std::remove_if(blk.successors.begin(), blk.successors.end(), is_dead),
            blk.successors.end());

        blk.predecessors.erase(
            std::remove_if(blk.predecessors.begin(), blk.predecessors.end(), is_dead),
            blk.predecessors.end());
    }
}

} // namespace

void eliminate_unreachable_blocks(FunctionCFG& fn) {
    if (fn.blocks.empty()) return;

    std::vector<char> reachable(fn.blocks.size(), 0);
    std::queue<BlockId> q;

    if (fn.entry >= 0 && fn.entry < (BlockId)fn.blocks.size() && fn.blocks[fn.entry]) {
        reachable[fn.entry] = 1;
        q.push(fn.entry);
    }

    while (!q.empty()) {
        BlockId b = q.front();
        q.pop();
        const auto& blk = fn.blocks[b];
        if (!blk) continue;
        for (auto succ : blk->successors) {
            if (succ < 0 || succ >= (BlockId)fn.blocks.size()) continue;
            if (!fn.blocks[succ]) continue;
            if (!reachable[succ]) {
                reachable[succ] = 1;
                q.push(succ);
            }
        }
    }

    for (size_t i = 0; i < fn.blocks.size(); ++i) {
        if (!reachable[i] && fn.blocks[i]) {
            for (auto succ : fn.blocks[i]->successors) {
                if (succ < 0 || succ >= (BlockId)fn.blocks.size()) continue;
                if (!fn.blocks[succ]) continue;
                auto& preds = fn.blocks[succ]->predecessors;
                preds.erase(std::remove(preds.begin(), preds.end(), (BlockId)i), preds.end());
            }
            fn.blocks[i].reset();
        }
    }

    clean_edges(fn, reachable);

    if (fn.exit < 0 || fn.exit >= (BlockId)fn.blocks.size() || !reachable[fn.exit]) {
        fn.exit = -1;
    }
}

LivenessResult compute_liveness(FunctionCFG& fn) {
    LivenessResult result;
    result.num_vregs = count_vregs(fn);
    result.num_locals = count_locals(fn);

    // Track locals captured by closures; stores to these must be kept live.
    std::vector<char> captured(result.num_locals, 0);
    for (const auto& name : fn.byRefLocals) {
        for (size_t i = 0; i < fn.params.size(); ++i) {
            if (fn.params[i] == name) captured[i] = 1;
        }
        for (size_t i = 0; i < fn.locals.size(); ++i) {
            if (fn.locals[i] == name) captured[fn.params.size() + i] = 1;
        }
    }

    result.live_in.assign(fn.blocks.size(), LiveSet(result.num_vregs, result.num_locals));
    result.live_out.assign(fn.blocks.size(), LiveSet(result.num_vregs, result.num_locals));

    bool changed = true;
    while (changed) {
        changed = false;
        for (int b = static_cast<int>(fn.blocks.size()) - 1; b >= 0; --b) {
            if (b < 0 || !fn.blocks[b]) continue;
            const auto& blk = *fn.blocks[b];

            LiveSet new_out(result.num_vregs, result.num_locals);
            for (auto succ : blk.successors) {
                if (succ < 0 || succ >= (BlockId)fn.blocks.size()) continue;
                if (!fn.blocks[succ]) continue;
                new_out = unite(new_out, result.live_in[succ]);
            }

            LiveSet live = new_out;
            add_terminator_uses(blk, live);

            for (auto it = blk.code.rbegin(); it != blk.code.rend(); ++it) {
                const auto du = analyze_instr(*it);
                bool rhs_needed = true;

                if (du.def_vreg >= 0) kill_def_vreg(live, static_cast<size_t>(du.def_vreg));
                if (du.def_local >= 0) {
                    kill_def_local(live, static_cast<size_t>(du.def_local));
                    // If the local isn't live and isn't captured, the store's RHS
                    // does not need to stay live.
                    if (du.def_local < static_cast<int>(live.locals.size()) &&
                        !live.locals[du.def_local] &&
                        (du.def_local >= static_cast<int>(captured.size()) || !captured[du.def_local])) {
                        rhs_needed = false;
                    }
                }
                if (rhs_needed) {
                    for (auto v : du.use_vregs) add_use_vreg(live, static_cast<size_t>(v));
                    for (auto l : du.use_locals) add_use_local(live, static_cast<size_t>(l));
                }
            }

            if (live != result.live_in[b] || new_out != result.live_out[b]) {
                result.live_in[b] = std::move(live);
                result.live_out[b] = std::move(new_out);
                changed = true;
            }
        }
    }

    return result;
}

void eliminate_dead_instructions(FunctionCFG& fn, const LivenessResult& liveness) {
    const size_t total_slots = fn.params.size() + fn.locals.size();
    std::vector<char> captured(total_slots, 0);
    for (const auto& name : fn.byRefLocals) {
        // params first, then locals
        for (size_t i = 0; i < fn.params.size(); ++i) {
            if (fn.params[i] == name) {
                captured[i] = 1;
                goto next_name;
            }
        }
        for (size_t i = 0; i < fn.locals.size(); ++i) {
            if (fn.locals[i] == name) {
                captured[fn.params.size() + i] = 1;
                goto next_name;
            }
        }
    next_name:
        continue;
    }

    for (size_t b = 0; b < fn.blocks.size(); ++b) {
        if (!fn.blocks[b]) continue;
        auto& blk = *fn.blocks[b];

        LiveSet live = (b < liveness.live_out.size())
            ? liveness.live_out[b]
            : LiveSet(liveness.num_vregs, liveness.num_locals);

        add_terminator_uses(blk, live);

        std::vector<IRInstr> kept;
        kept.reserve(blk.code.size());

        for (auto it = blk.code.rbegin(); it != blk.code.rend(); ++it) {
            const IRInstr& ir = *it;
            const auto du = analyze_instr(ir);

            bool has_dead_def = false;
            if (du.def_vreg >= 0) {
                has_dead_def =
                    du.def_vreg < static_cast<int>(live.vregs.size()) && !live.vregs[du.def_vreg];
            }
            if (du.def_local >= 0) {
                has_dead_def =
                    has_dead_def ||
                    (du.def_local < static_cast<int>(live.locals.size()) && !live.locals[du.def_local]);

                // Never treat stores to captured locals as dead; closures may read them.
                if (du.def_local < static_cast<int>(captured.size()) && captured[du.def_local]) {
                    has_dead_def = false;
                }
            }

            if (has_dead_def && !instr_has_side_effects(ir)) {
                continue; // Remove this dead instruction.
            }

            // Keep instruction: update liveness.
            if (du.def_vreg >= 0) kill_def_vreg(live, static_cast<size_t>(du.def_vreg));
            if (du.def_local >= 0) kill_def_local(live, static_cast<size_t>(du.def_local));
            for (auto v : du.use_vregs) add_use_vreg(live, static_cast<size_t>(v));
            for (auto l : du.use_locals) add_use_local(live, static_cast<size_t>(l));

            kept.push_back(ir);
        }

        std::reverse(kept.begin(), kept.end());
        blk.code = std::move(kept);
    }
}

void run_dce_on_function(FunctionCFG& fn) {
    eliminate_unreachable_blocks(fn);
    auto liveness = compute_liveness(fn);
    eliminate_dead_instructions(fn, liveness);

    // Apply recursively to nested functions.
    for (auto& child : fn.children) {
        if (child) run_dce_on_function(*child);
    }
}

} // namespace mitscript::analysis
