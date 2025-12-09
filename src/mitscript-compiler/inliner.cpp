#include "inliner.hpp"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

using namespace mitscript::CFG;

namespace mitscript::analysis {

namespace {

int max_vreg_in_function(const FunctionCFG& fn) {
    int max_v = -1;
    for (const auto& blk_ptr : fn.blocks) {
        if (!blk_ptr) continue;
        const auto& blk = *blk_ptr;
        for (const auto& ir : blk.code) {
            if (ir.output && ir.output->kind == IROperand::VREG)
                max_v = std::max(max_v, ir.output->i);
            for (const auto& in : ir.inputs) {
                if (in.kind == IROperand::VREG)
                    max_v = std::max(max_v, in.i);
            }
        }
        if (blk.term.kind == Terminator::Kind::CondJump ||
            blk.term.kind == Terminator::Kind::Return) {
            max_v = std::max(max_v, blk.term.condition);
        }
    }
    return max_v;
}

int instruction_count(const FunctionCFG& fn) {
    int count = 0;
    for (const auto& blk_ptr : fn.blocks) {
        if (!blk_ptr) continue;
        count += static_cast<int>(blk_ptr->code.size());
    }
    return count;
}

struct CallSite {
    FunctionCFG* caller = nullptr;
    BlockId block = -1;
    size_t instr_index = 0;
    FunctionCFG* callee = nullptr;
};

struct CallGraph {
    std::vector<FunctionCFG*> funcs;
    std::unordered_map<FunctionCFG*, std::vector<FunctionCFG*>> edges;
    std::unordered_map<FunctionCFG*, bool> recursive;
};

void collect_functions(FunctionCFG& root, std::vector<FunctionCFG*>& out) {
    out.push_back(&root);
    for (auto& child : root.children) {
        if (child) collect_functions(*child, out);
    }
}

FunctionCFG* lookup_child(FunctionCFG& caller, int idx) {
    if (idx < 0 || idx >= static_cast<int>(caller.children.size())) return nullptr;
    return caller.children[idx].get();
}

// Resolve a callee index either as a nested child or as a module-level function
// in the flattened function list (as produced by collect_functions).
FunctionCFG* lookup_callee_global(const std::vector<FunctionCFG*>& all_funcs,
                                  FunctionCFG& caller,
                                  int idx) {
    if (idx < 0) return nullptr;
    // Prefer the existing child lookup semantics.
    if (auto* child = lookup_child(caller, idx)) return child;
    // Fallback: treat idx as a module-level function ordinal.
    if (idx < static_cast<int>(all_funcs.size())) {
        return all_funcs[idx];
    }
    return nullptr;
}

// Build a mapping from global function name -> FunctionCFG* by scanning the
// module (root) function for "StoreGlobal <name>, <vreg>" where the value comes
// from an AllocClosure of a child function.
std::unordered_map<std::string, FunctionCFG*> build_global_func_map(FunctionCFG& module,
                                                                    const std::vector<FunctionCFG*>& /*funcs*/) {
    std::unordered_map<std::string, FunctionCFG*> name_map;

    // Directly map named children.
    for (const auto& child_ptr : module.children) {
        if (child_ptr && !child_ptr->name.empty()) {
            name_map.emplace(child_ptr->name, child_ptr.get());
        }
    }

    // Track vreg -> FunctionCFG* inside the module body.
    std::unordered_map<int, FunctionCFG*> vreg_func;
    for (const auto& blk_ptr : module.blocks) {
        if (!blk_ptr) continue;
        for (const auto& ir : blk_ptr->code) {
            if (ir.op == IROp::AllocClosure &&
                ir.output && ir.output->kind == IROperand::VREG &&
                !ir.inputs.empty() &&
                ir.inputs.back().kind == IROperand::CONSTI) {
                FunctionCFG* callee = lookup_child(module, ir.inputs.back().i);
                if (callee) vreg_func[ir.output->i] = callee;
            } else if (ir.op == IROp::StoreGlobal &&
                       ir.inputs.size() >= 2 &&
                       ir.inputs[0].kind == IROperand::NAME &&
                       ir.inputs[1].kind == IROperand::VREG) {
                int v = ir.inputs[1].i;
                auto it = vreg_func.find(v);
                if (it != vreg_func.end()) {
                    name_map.emplace(ir.inputs[0].s, it->second);
                }
            }
        }
    }
    return name_map;
}

CallGraph build_call_graph(const std::vector<FunctionCFG*>& funcs) {
    CallGraph cg;
    cg.funcs = funcs;
    if (funcs.empty()) return cg;
    auto global_name_map = build_global_func_map(*funcs.front(), funcs);

    for (auto* f : funcs) {
        for (const auto& blk_ptr : f->blocks) {
            if (!blk_ptr) continue;
            std::unordered_map<int, FunctionCFG*> vreg_callee;
            for (const auto& ir : blk_ptr->code) {
                // Track simple defs to resolve subsequent calls in the same block.
                // This is minimal: only LoadGlobal of a known function and AllocClosure.
                if (ir.op == IROp::AllocClosure &&
                    ir.output && ir.output->kind == IROperand::VREG &&
                    !ir.inputs.empty() &&
                    ir.inputs.back().kind == IROperand::CONSTI) {
                    FunctionCFG* c = lookup_child(*f, ir.inputs.back().i);
                    if (c) vreg_callee[ir.output->i] = c;
                } else if (ir.op == IROp::LoadGlobal &&
                           ir.output && ir.output->kind == IROperand::VREG &&
                           !ir.inputs.empty() &&
                           ir.inputs[0].kind == IROperand::NAME) {
                    auto itn = global_name_map.find(ir.inputs[0].s);
                    if (itn != global_name_map.end()) vreg_callee[ir.output->i] = itn->second;
                } else if (ir.op == IROp::LoadLocal &&
                           ir.output && ir.output->kind == IROperand::VREG &&
                           !ir.inputs.empty() &&
                           ir.inputs[0].kind == IROperand::NAME) {
                    auto itn = global_name_map.find(ir.inputs[0].s);
                    if (itn != global_name_map.end()) vreg_callee[ir.output->i] = itn->second;
                }

                if (ir.op != IROp::Call) continue;
                if (ir.inputs.empty()) continue;

                FunctionCFG* callee = nullptr;
                const auto& callee_op = ir.inputs[0];
                if (callee_op.kind == IROperand::CONSTI) {
                    callee = lookup_callee_global(funcs, *f, callee_op.i);
                } else if (callee_op.kind == IROperand::VREG) {
                    auto it = vreg_callee.find(callee_op.i);
                    if (it != vreg_callee.end()) callee = it->second;
                }
                if (callee) cg.edges[f].push_back(callee);
            }
        }
    }
    // Detect recursion via DFS for SCC.
    std::unordered_map<FunctionCFG*, int> index, lowlink;
    std::unordered_map<FunctionCFG*, bool> on_stack;
    std::vector<FunctionCFG*> stack;
    int idx = 0;

    std::function<void(FunctionCFG*)> strongconnect = [&](FunctionCFG* v) {
        index[v] = lowlink[v] = idx++;
        stack.push_back(v);
        on_stack[v] = true;

        for (auto* w : cg.edges[v]) {
            if (!index.count(w)) {
                strongconnect(w);
                lowlink[v] = std::min(lowlink[v], lowlink[w]);
            } else if (on_stack[w]) {
                lowlink[v] = std::min(lowlink[v], index[w]);
            }
        }

        if (lowlink[v] == index[v]) {
            std::vector<FunctionCFG*> scc;
            while (true) {
                auto* w = stack.back();
                stack.pop_back();
                on_stack[w] = false;
                scc.push_back(w);
                if (w == v) break;
            }
            if (scc.size() > 1) {
                for (auto* fn : scc) cg.recursive[fn] = true;
            } else {
                // Self-loop?
                for (auto* tgt : cg.edges[v]) {
                    if (tgt == v) cg.recursive[v] = true;
                }
            }
        }
    };

    for (auto* f : funcs) {
        if (!index.count(f)) strongconnect(f);
    }

    for (auto* f : funcs) {
        if (!cg.recursive.count(f)) cg.recursive[f] = false;
    }
    return cg;
}

bool should_inline(const FunctionCFG* callee,
                   const CallGraph& cg,
                   int max_inline_instructions) {
    if (!callee) return false;
    // Do not inline closures that capture free variables; we do not rewrite
    // environments here.
    if (!callee->freeVars.empty()) return false;
    // Also skip functions that define nested functions/closures; inlining them
    // would require remapping their children and environments.
    if (!callee->children.empty()) return false;
    // Skip recursive functions
    if (cg.recursive.count(const_cast<FunctionCFG*>(callee)) &&
        cg.recursive.at(const_cast<FunctionCFG*>(callee))) return false;
    // Skip functions that have by-ref locals (closures that capture)
    if (!callee->byRefLocals.empty()) return false;
    // Check instruction count against threshold
    int count = instruction_count(*callee);
    if (count > max_inline_instructions) return false;
    // Check block count - don't inline functions with too many blocks
    int block_count = 0;
    for (const auto& b : callee->blocks) {
        if (b) block_count++;
    }
    if (block_count > 5) return false;
    return true;
}

std::vector<CallSite> find_call_sites(const std::vector<FunctionCFG*>& funcs,
                                      const CallGraph& cg,
                                      int max_inline_instructions,
                                      const std::unordered_map<std::string, FunctionCFG*>& global_name_map,
                                      FunctionCFG* root) {
    std::vector<CallSite> sites;
    for (auto* caller : funcs) {
        // Avoid inlining into the module/root function to keep top-level
        // sequencing simple.
        if (caller == root) continue;
        // Function-wide best-effort mapping from vreg to callee function.
        std::unordered_map<int, FunctionCFG*> func_vreg_callee;
        for (const auto& blk_ptr : caller->blocks) {
            if (!blk_ptr) continue;
            for (const auto& ir : blk_ptr->code) {
                if (!ir.output || ir.output->kind != IROperand::VREG) continue;
                if (ir.op == IROp::AllocClosure &&
                    !ir.inputs.empty() &&
                    ir.inputs.back().kind == IROperand::CONSTI) {
                    if (auto* c = lookup_child(*caller, ir.inputs.back().i)) {
                        func_vreg_callee[ir.output->i] = c;
                    }
                } else if (ir.op == IROp::LoadGlobal &&
                           !ir.inputs.empty() &&
                           ir.inputs[0].kind == IROperand::NAME) {
                    if (auto it = global_name_map.find(ir.inputs[0].s); it != global_name_map.end()) {
                        func_vreg_callee[ir.output->i] = it->second;
                    }
                } else if (ir.op == IROp::LoadLocal &&
                           !ir.inputs.empty() &&
                           ir.inputs[0].kind == IROperand::NAME) {
                    if (auto it = global_name_map.find(ir.inputs[0].s); it != global_name_map.end()) {
                        func_vreg_callee[ir.output->i] = it->second;
                    }
                }
            }
        }

        for (size_t bid = 0; bid < caller->blocks.size(); ++bid) {
            auto& blk_ptr = caller->blocks[bid];
            if (!blk_ptr) continue;
            auto& blk = *blk_ptr;
            std::unordered_map<int, FunctionCFG*> vreg_callee;
            for (size_t i = 0; i < blk.code.size(); ++i) {
                const auto& ir = blk.code[i];
                // Track defs to resolve callee vregs.
                if (ir.op == IROp::AllocClosure &&
                    ir.output && ir.output->kind == IROperand::VREG &&
                    !ir.inputs.empty() &&
                    ir.inputs.back().kind == IROperand::CONSTI) {
                    if (auto* callee = lookup_child(*caller, ir.inputs.back().i)) {
                        vreg_callee[ir.output->i] = callee;
                    }
                } else if (ir.op == IROp::LoadGlobal &&
                           ir.output && ir.output->kind == IROperand::VREG &&
                           !ir.inputs.empty() &&
                           ir.inputs[0].kind == IROperand::NAME) {
                    if (auto it = global_name_map.find(ir.inputs[0].s); it != global_name_map.end()) {
                        vreg_callee[ir.output->i] = it->second;
                    }
                }

                if (ir.op != IROp::Call) continue;
                if (ir.inputs.empty()) continue;
                const auto& callee_op = ir.inputs[0];
                FunctionCFG* callee = nullptr;
                if (callee_op.kind == IROperand::CONSTI) {
                    callee = lookup_callee_global(funcs, *caller, callee_op.i);
                } else if (callee_op.kind == IROperand::NAME) {
                    if (auto itn = global_name_map.find(callee_op.s); itn != global_name_map.end()) {
                        callee = itn->second;
                    }
                } else if (callee_op.kind == IROperand::VREG) {
                    if (auto it = vreg_callee.find(callee_op.i); it != vreg_callee.end()) {
                        callee = it->second;
                    } else if (auto it2 = func_vreg_callee.find(callee_op.i); it2 != func_vreg_callee.end()) {
                        callee = it2->second;
                    }
                }
                if (!callee) continue;
                if (!should_inline(callee, cg, max_inline_instructions)) continue;
                sites.push_back({caller, static_cast<BlockId>(bid), i, callee});
            }
        }
    }
    return sites;
}

int total_locals(const FunctionCFG& fn) {
    return static_cast<int>(fn.params.size() + fn.locals.size());
}

int append_locals(FunctionCFG& caller, const FunctionCFG& callee) {
    int offset = total_locals(caller);
    // Append params and locals as new locals in caller to reserve slots.
    for (const auto& p : callee.params) caller.locals.push_back("inl_param_" + p);
    for (const auto& l : callee.locals) caller.locals.push_back("inl_local_" + l);
    // Propagate by-ref markers.
    for (const auto& name : callee.byRefLocals) {
        caller.byRefLocals.push_back("inl_" + name);
    }
    return offset;
}

void remap_operands(IRInstr& ir,
                    const std::vector<int>& vmap,
                    int local_offset) {
    auto remap_op = [&](IROperand& op) {
        if (op.kind == IROperand::VREG && op.i >= 0 && op.i < static_cast<int>(vmap.size())) {
            op.i = vmap[op.i];
        } else if (op.kind == IROperand::LOCAL) {
            op.i += local_offset;
        }
    };
    for (auto& in : ir.inputs) remap_op(in);
    if (ir.output) remap_op(*ir.output);
}

} // namespace

// Core inlining of a single call site inside caller.
static void inline_call(FunctionCFG& caller, const CallSite& site) {
    BasicBlock* call_blk = caller.blocks[site.block].get();
    if (!call_blk) return;
    if (site.instr_index >= call_blk->code.size()) return;
    FunctionCFG& callee = *site.callee;

    const IRInstr& call_ir = call_blk->code[site.instr_index];
    int call_dst = (call_ir.output && call_ir.output->kind == IROperand::VREG)
                       ? call_ir.output->i
                       : -1;

    // Gather return value vregs to map to call destination when possible.
    std::unordered_set<int> returned;
    for (const auto& blk_ptr : callee.blocks) {
        if (!blk_ptr) continue;
        const auto& term = blk_ptr->term;
        if (term.kind == Terminator::Kind::Return && term.condition >= 0) {
            returned.insert(term.condition);
        }
    }

    int callee_vregs = max_vreg_in_function(callee) + 1;
    int next_vreg = max_vreg_in_function(caller) + 1;

    std::vector<int> vmap(callee_vregs, -1);
    for (int i = 0; i < callee_vregs; ++i) {
        if (call_dst >= 0 && returned.count(i)) {
            vmap[i] = call_dst;
        } else {
            vmap[i] = next_vreg++;
        }
    }

    // Create new local slots for callee locals/params.
    int local_offset = append_locals(caller, callee);

    // Split caller block at call site.
    auto cont_blk = std::make_unique<BasicBlock>();
    cont_blk->id = static_cast<int>(caller.blocks.size());
    // Copy instructions after call into continuation block.
    cont_blk->code.assign(call_blk->code.begin() + site.instr_index + 1, call_blk->code.end());
    cont_blk->term = call_blk->term;
    cont_blk->successors = call_blk->successors;
    cont_blk->predecessors.clear(); // Will rebuild.
    cont_blk->post_return = call_blk->post_return;

    // New terminator for call block: jump to inlined entry.
    call_blk->code.erase(call_blk->code.begin() + site.instr_index, call_blk->code.end());
    call_blk->term.kind = Terminator::Kind::Jump;
    call_blk->term.target = -1; // fill later
    call_blk->term.condition = -1;
    call_blk->term.trueTarget = call_blk->term.falseTarget = -1;
    call_blk->successors.clear();

    // Fix predecessors of original successors to point to cont block.
    for (auto succ : cont_blk->successors) {
        if (succ < 0 || succ >= static_cast<BlockId>(caller.blocks.size())) continue;
        auto* succ_blk = caller.blocks[succ].get();
        if (!succ_blk) continue;
        std::replace(succ_blk->predecessors.begin(), succ_blk->predecessors.end(),
                     site.block, cont_blk->id);
    }

    // Insert continuation block into caller.
    caller.blocks.push_back(std::move(cont_blk));
    BasicBlock* cont_ptr = caller.blocks.back().get();

    // Clone callee blocks.
    std::unordered_map<BlockId, BasicBlock*> block_map;
    for (const auto& blk_ptr : callee.blocks) {
        if (!blk_ptr) continue;
        auto clone = std::make_unique<BasicBlock>();
        clone->id = static_cast<int>(caller.blocks.size());
        clone->post_return = false;
        block_map[blk_ptr->id] = clone.get();
        caller.blocks.push_back(std::move(clone));
    }

    // Synthetic exit block inside caller for the inlined callee.
    auto exit_clone = std::make_unique<BasicBlock>();
    int exit_id = static_cast<int>(caller.blocks.size());
    exit_clone->id = exit_id;
    caller.blocks.push_back(std::move(exit_clone));

    // Populate cloned blocks.
    for (const auto& blk_ptr : callee.blocks) {
        if (!blk_ptr) continue;
        BasicBlock* clone = block_map[blk_ptr->id];
        // Clone instructions with operand remapping.
        for (auto ir : blk_ptr->code) {
            remap_operands(ir, vmap, local_offset);
            clone->code.push_back(std::move(ir));
        }

        // Clone terminator.
        Terminator term = blk_ptr->term;
        auto map_target = [&](BlockId t) -> BlockId {
            auto it = block_map.find(t);
            if (it != block_map.end() && it->second) return it->second->id;
            return exit_id;
        };
        switch (term.kind) {
            case Terminator::Kind::Jump:
                term.target = map_target(term.target);
                clone->successors = {term.target};
                break;
            case Terminator::Kind::CondJump:
                if (term.condition >= 0 && term.condition < static_cast<int>(vmap.size()))
                    term.condition = vmap[term.condition];
                term.trueTarget = map_target(term.trueTarget);
                term.falseTarget = map_target(term.falseTarget);
                clone->successors = {term.trueTarget, term.falseTarget};
                break;
            case Terminator::Kind::Return:
                if (term.condition >= 0 && term.condition < static_cast<int>(vmap.size()))
                    term.condition = vmap[term.condition];
                term.kind = Terminator::Kind::Jump;
                term.target = exit_id;
                term.condition = -1;
                term.trueTarget = term.falseTarget = -1;
                clone->successors = {exit_id};
                break;
        }
        clone->term = term;
    }

    // Wire predecessors for cloned blocks.
    for (const auto& blk_ptr : callee.blocks) {
        if (!blk_ptr) continue;
        BasicBlock* clone = block_map[blk_ptr->id];
        for (auto succ : clone->successors) {
            if (succ == exit_id) {
                caller.blocks[exit_id]->predecessors.push_back(clone->id);
            } else {
                auto* succ_blk = caller.blocks[succ].get();
                succ_blk->predecessors.push_back(clone->id);
            }
        }
    }

    // Fill exit block: jump to continuation, assign predecessors.
    auto* exit_blk = caller.blocks[exit_id].get();
    exit_blk->term.kind = Terminator::Kind::Jump;
    exit_blk->term.target = cont_ptr->id;
    exit_blk->successors = {cont_ptr->id};
    cont_ptr->predecessors.push_back(exit_id);

    // Set entry jump target and predecessor.
    BasicBlock* entry_clone = block_map[callee.entry];
    call_blk->term.target = entry_clone->id;
    call_blk->successors = {entry_clone->id};
    entry_clone->predecessors.push_back(call_blk->id);

    // Initialize parameters: insert stores from caller args into mapped local slots.
    int arg_offset = 1; // first input is callee id
    int param_count = static_cast<int>(callee.params.size());
    std::vector<IRInstr> prologue;
    for (int i = 0; i < param_count && arg_offset + i < static_cast<int>(call_ir.inputs.size()); ++i) {
        const auto& arg = call_ir.inputs[arg_offset + i];
        IROperand dst{IROperand::LOCAL, local_offset + i};
        IROperand src = arg;
        if (src.kind == IROperand::VREG && src.i >= 0 && src.i < static_cast<int>(vmap.size())) {
            // Argument lives in caller; leave as-is.
        }
        prologue.push_back(IRInstr{IROp::StoreLocal, {dst, src}, std::nullopt});
    }
    // Prepend prologue to entry clone.
    entry_clone->code.insert(entry_clone->code.begin(), prologue.begin(), prologue.end());
}

void run_inlining_pass(FunctionCFG& root, const InlineConfig& cfg) {
    std::vector<FunctionCFG*> funcs;
    collect_functions(root, funcs);
    auto cg = build_call_graph(funcs);
    // Build global name -> func map from module/root for resolving LoadGlobal callees.
    auto global_name_map = funcs.empty() ? std::unordered_map<std::string, FunctionCFG*>{}
                                         : build_global_func_map(*funcs.front(), funcs);

    FunctionCFG* module = funcs.empty() ? nullptr : funcs.front();

    // Process each function separately, and inline from back to front
    // to keep instruction indices valid
    constexpr int MAX_INLINES_PER_FUNC = 10;
    constexpr int MAX_BLOCKS_PER_FUNC = 100;

    for (auto* caller : funcs) {
        if (caller == module) continue; // Skip root module

        int inlines_done = 0;

        // Keep inlining until no more sites or limits reached
        while (inlines_done < MAX_INLINES_PER_FUNC) {
            // Check if caller is already too large
            int caller_blocks = 0;
            for (const auto& b : caller->blocks) {
                if (b) caller_blocks++;
            }
            if (caller_blocks >= MAX_BLOCKS_PER_FUNC) break;

            // Find call sites for this function only, process last block/instr first
            std::vector<CallSite> sites;
            std::unordered_map<int, FunctionCFG*> func_vreg_callee;
            for (const auto& blk_ptr : caller->blocks) {
                if (!blk_ptr) continue;
                for (const auto& ir : blk_ptr->code) {
                    if (!ir.output || ir.output->kind != IROperand::VREG) continue;
                    if (ir.op == IROp::AllocClosure &&
                        !ir.inputs.empty() &&
                        ir.inputs.back().kind == IROperand::CONSTI) {
                        if (auto* c = lookup_child(*caller, ir.inputs.back().i)) {
                            func_vreg_callee[ir.output->i] = c;
                        }
                    } else if (ir.op == IROp::LoadGlobal &&
                               !ir.inputs.empty() &&
                               ir.inputs[0].kind == IROperand::NAME) {
                        if (auto it = global_name_map.find(ir.inputs[0].s); it != global_name_map.end()) {
                            func_vreg_callee[ir.output->i] = it->second;
                        }
                    }
                }
            }

            for (size_t bid = 0; bid < caller->blocks.size(); ++bid) {
                auto& blk_ptr = caller->blocks[bid];
                if (!blk_ptr) continue;
                auto& blk = *blk_ptr;
                std::unordered_map<int, FunctionCFG*> vreg_callee;
                for (size_t i = 0; i < blk.code.size(); ++i) {
                    const auto& ir = blk.code[i];
                    if (ir.op == IROp::AllocClosure &&
                        ir.output && ir.output->kind == IROperand::VREG &&
                        !ir.inputs.empty() &&
                        ir.inputs.back().kind == IROperand::CONSTI) {
                        if (auto* c = lookup_child(*caller, ir.inputs.back().i)) {
                            vreg_callee[ir.output->i] = c;
                        }
                    } else if (ir.op == IROp::LoadGlobal &&
                               ir.output && ir.output->kind == IROperand::VREG &&
                               !ir.inputs.empty() &&
                               ir.inputs[0].kind == IROperand::NAME) {
                        if (auto it = global_name_map.find(ir.inputs[0].s); it != global_name_map.end()) {
                            vreg_callee[ir.output->i] = it->second;
                        }
                    }

                    if (ir.op != IROp::Call) continue;
                    if (ir.inputs.empty()) continue;
                    const auto& callee_op = ir.inputs[0];
                    FunctionCFG* callee = nullptr;
                    if (callee_op.kind == IROperand::CONSTI) {
                        callee = lookup_callee_global(funcs, *caller, callee_op.i);
                    } else if (callee_op.kind == IROperand::NAME) {
                        if (auto itn = global_name_map.find(callee_op.s); itn != global_name_map.end()) {
                            callee = itn->second;
                        }
                    } else if (callee_op.kind == IROperand::VREG) {
                        if (auto it = vreg_callee.find(callee_op.i); it != vreg_callee.end()) {
                            callee = it->second;
                        } else if (auto it2 = func_vreg_callee.find(callee_op.i); it2 != func_vreg_callee.end()) {
                            callee = it2->second;
                        }
                    }
                    if (!callee) continue;
                    if (!should_inline(callee, cg, cfg.max_inline_instructions)) continue;
                    sites.push_back({caller, static_cast<BlockId>(bid), i, callee});
                }
            }

            if (sites.empty()) break;

            // Sort sites in reverse order (last block, last instruction first)
            // This ensures indices stay valid as we inline
            std::sort(sites.begin(), sites.end(), [](const CallSite& a, const CallSite& b) {
                if (a.block != b.block) return a.block > b.block;
                return a.instr_index > b.instr_index;
            });

            // Inline just the first (last in order) site, then re-scan
            inline_call(*sites[0].caller, sites[0]);
            inlines_done++;
        }
    }
}

} // namespace mitscript::analysis
