#include "opt_deadcode.hpp"
#include "instructions.hpp"
#include "types.hpp"
#include <cassert>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace bytecode::opt_deadcode {

// Mark all reachable instructions starting from instruction 0
static std::set<size_t> find_reachable_instructions(
    const std::vector<Instruction> &instructions) {
  std::set<size_t> reachable;
  std::vector<size_t> worklist;
  worklist.push_back(0);

  while (!worklist.empty()) {
    size_t idx = worklist.back();
    worklist.pop_back();

    if (reachable.count(idx))
      continue;
    reachable.insert(idx);

    if (idx >= instructions.size())
      continue;

    const Instruction &inst = instructions[idx];

    // Handle control flow
    switch (inst.operation) {
    case Operation::Goto: {
      // Unconditional jump
      if (inst.operand0.has_value()) {
        int offset = inst.operand0.value();
        int target = (int)idx + offset;
        if (target >= 0 && target < (int)instructions.size()) {
          worklist.push_back(target);
        }
      }
      // Don't add next instruction (unreachable)
      break;
    }

    case Operation::If: {
      // Conditional jump - both paths are reachable
      if (inst.operand0.has_value()) {
        int offset = inst.operand0.value();
        int target = (int)idx + offset;
        if (target >= 0 && target < (int)instructions.size()) {
          worklist.push_back(target);
        }
      }
      // Fall through to next instruction
      if (idx + 1 < instructions.size()) {
        worklist.push_back(idx + 1);
      }
      break;
    }

    case Operation::Return: {
      // Return - next instruction is unreachable
      break;
    }

    default: {
      // Normal instruction - next is reachable
      if (idx + 1 < instructions.size()) {
        worklist.push_back(idx + 1);
      }
      break;
    }
    }
  }

  return reachable;
}

// Build a simple control flow graph for liveness analysis
struct BasicBlock {
  size_t start;
  size_t end; // exclusive
  std::vector<size_t> successors;
  std::vector<size_t> predecessors;
};

static std::vector<BasicBlock>
build_basic_blocks(const std::vector<Instruction> &instructions) {
  if (instructions.empty())
    return {};

  // Find block leaders (targets of jumps and instructions after jumps)
  std::set<size_t> leaders;
  leaders.insert(0);

  for (size_t i = 0; i < instructions.size(); ++i) {
    const auto &inst = instructions[i];
    if (inst.operation == Operation::Goto || inst.operation == Operation::If) {
      if (inst.operand0.has_value()) {
        int target = (int)i + inst.operand0.value();
        if (target >= 0 && target < (int)instructions.size()) {
          leaders.insert(target);
        }
      }
      if (i + 1 < instructions.size()) {
        leaders.insert(i + 1);
      }
    } else if (inst.operation == Operation::Return) {
      if (i + 1 < instructions.size()) {
        leaders.insert(i + 1);
      }
    }
  }

  // Create basic blocks
  std::vector<BasicBlock> blocks;
  std::unordered_map<size_t, size_t> leader_to_block;

  std::vector<size_t> leader_list(leaders.begin(), leaders.end());
  for (size_t bi = 0; bi < leader_list.size(); ++bi) {
    size_t start = leader_list[bi];
    size_t end = (bi + 1 < leader_list.size()) ? leader_list[bi + 1]
                                               : instructions.size();
    blocks.push_back({start, end, {}, {}});
    leader_to_block[start] = bi;
  }

  // Add edges
  for (size_t bi = 0; bi < blocks.size(); ++bi) {
    size_t last = blocks[bi].end - 1;
    const auto &inst = instructions[last];

    if (inst.operation == Operation::Goto) {
      if (inst.operand0.has_value()) {
        int target = (int)last + inst.operand0.value();
        if (target >= 0 && target < (int)instructions.size()) {
          auto it = leader_to_block.find(target);
          if (it != leader_to_block.end()) {
            blocks[bi].successors.push_back(it->second);
            blocks[it->second].predecessors.push_back(bi);
          }
        }
      }
    } else if (inst.operation == Operation::If) {
      // Fall-through edge
      if (blocks[bi].end < instructions.size()) {
        auto it = leader_to_block.find(blocks[bi].end);
        if (it != leader_to_block.end()) {
          blocks[bi].successors.push_back(it->second);
          blocks[it->second].predecessors.push_back(bi);
        }
      }
      // Jump edge
      if (inst.operand0.has_value()) {
        int target = (int)last + inst.operand0.value();
        if (target >= 0 && target < (int)instructions.size()) {
          auto it = leader_to_block.find(target);
          if (it != leader_to_block.end()) {
            blocks[bi].successors.push_back(it->second);
            blocks[it->second].predecessors.push_back(bi);
          }
        }
      }
    } else if (inst.operation != Operation::Return) {
      // Fall-through to next block
      if (bi + 1 < blocks.size()) {
        blocks[bi].successors.push_back(bi + 1);
        blocks[bi + 1].predecessors.push_back(bi);
      }
    }
  }

  return blocks;
}

// Compute live-out sets for each instruction using backward dataflow analysis
// Returns a vector where live_out[i] contains the set of locals live after
// instruction i
static std::vector<std::unordered_set<int>>
compute_liveness(const std::vector<Instruction> &instructions,
                 const std::vector<BasicBlock> &blocks, size_t num_locals) {
  if (instructions.empty())
    return {};

  // Initialize
  std::vector<std::unordered_set<int>> live_in(blocks.size());
  std::vector<std::unordered_set<int>> live_out(blocks.size());

  // Compute gen and kill sets for each block
  std::vector<std::unordered_set<int>> gen(blocks.size());
  std::vector<std::unordered_set<int>> kill(blocks.size());

  for (size_t bi = 0; bi < blocks.size(); ++bi) {
    // Process instructions in reverse order within the block
    std::unordered_set<int> live;
    for (size_t i = blocks[bi].end; i > blocks[bi].start;) {
      --i;
      const auto &inst = instructions[i];

      // Kill: definitions (StoreLocal)
      if (inst.operation == Operation::StoreLocal && inst.operand0.has_value()) {
        int local = inst.operand0.value();
        if (local >= 0 && local < (int)num_locals) {
          live.erase(local);
          kill[bi].insert(local);
        }
      }

      // Gen: uses (LoadLocal)
      if (inst.operation == Operation::LoadLocal && inst.operand0.has_value()) {
        int local = inst.operand0.value();
        if (local >= 0 && local < (int)num_locals) {
          live.insert(local);
          gen[bi].insert(local);
        }
      }
    }
  }

  // Iterate until fixed point
  bool changed = true;
  while (changed) {
    changed = false;
    // Process blocks in reverse order (backward analysis)
    for (size_t bi = blocks.size(); bi > 0;) {
      --bi;

      // live_out = union of live_in of all successors
      std::unordered_set<int> new_live_out;
      for (size_t succ : blocks[bi].successors) {
        for (int v : live_in[succ]) {
          new_live_out.insert(v);
        }
      }

      // live_in = gen union (live_out - kill)
      std::unordered_set<int> new_live_in = gen[bi];
      for (int v : new_live_out) {
        if (!kill[bi].count(v)) {
          new_live_in.insert(v);
        }
      }

      if (new_live_in != live_in[bi] || new_live_out != live_out[bi]) {
        changed = true;
        live_in[bi] = std::move(new_live_in);
        live_out[bi] = std::move(new_live_out);
      }
    }
  }

  // Now compute per-instruction live-out sets
  std::vector<std::unordered_set<int>> inst_live_out(instructions.size());

  for (size_t bi = 0; bi < blocks.size(); ++bi) {
    std::unordered_set<int> live = live_out[bi];

    for (size_t i = blocks[bi].end; i > blocks[bi].start;) {
      --i;
      inst_live_out[i] = live;
      const auto &inst = instructions[i];

      // Update liveness backward
      if (inst.operation == Operation::StoreLocal && inst.operand0.has_value()) {
        int local = inst.operand0.value();
        if (local >= 0 && local < (int)num_locals) {
          live.erase(local);
        }
      }
      if (inst.operation == Operation::LoadLocal && inst.operand0.has_value()) {
        int local = inst.operand0.value();
        if (local >= 0 && local < (int)num_locals) {
          live.insert(local);
        }
      }
    }
  }

  return inst_live_out;
}

// Find which local variables are read (used) and whether reference operations
// exist Returns (used_set, has_references) If has_references is true, we cannot
// safely eliminate dead stores because PushReference operands are indices into
// local_ref_vars_, not local_vars_.
static std::pair<std::unordered_set<int>, bool>
find_used_locals(const std::vector<Instruction> &instructions) {
  std::unordered_set<int> used;
  bool has_references = false;

  for (const Instruction &inst : instructions) {
    switch (inst.operation) {
    case Operation::LoadLocal: {
      // Reading a local variable
      if (inst.operand0.has_value()) {
        used.insert(inst.operand0.value());
      }
      break;
    }

    case Operation::PushReference:
    case Operation::LoadReference:
    case Operation::StoreReference: {
      // Reference operations exist - operands are indices into local_ref_vars_
      // or free_vars_, NOT local_vars_. We can't safely track which locals are
      // used through references, so disable dead store elimination.
      has_references = true;
      break;
    }

    case Operation::StoreLocal: {
      // Writing to a local - we don't mark it as used here
      // (we'll handle dead stores separately)
      break;
    }

    default:
      break;
    }
  }

  return {used, has_references};
}

// Find which constants are used
static std::unordered_set<int>
find_used_constants(const std::vector<Instruction> &instructions) {
  std::unordered_set<int> used;

  for (const Instruction &inst : instructions) {
    if (inst.operation == Operation::LoadConst) {
      if (inst.operand0.has_value()) {
        used.insert(inst.operand0.value());
      }
    }
  }

  return used;
}

// Find which functions are used
static std::unordered_set<int>
find_used_functions(const std::vector<Instruction> &instructions) {
  std::unordered_set<int> used;

  for (const Instruction &inst : instructions) {
    if (inst.operation == Operation::LoadFunc) {
      if (inst.operand0.has_value()) {
        used.insert(inst.operand0.value());
      }
    }
  }

  return used;
}

// Perform dead code elimination on a single function
static void eliminate_dead_code_one(bytecode::Function *fn) {
  if (fn->instructions.empty())
    return;

  // Step 1: Find all reachable instructions
  std::set<size_t> reachable = find_reachable_instructions(fn->instructions);

  // Step 2: Remove unreachable instructions
  std::vector<Instruction> new_instructions;
  new_instructions.reserve(reachable.size());

  // Map old indices to new indices for fixing jump targets
  std::vector<int> index_map(fn->instructions.size(), -1);
  int new_idx = 0;

  for (size_t old_idx = 0; old_idx < fn->instructions.size(); ++old_idx) {
    if (reachable.count(old_idx)) {
      index_map[old_idx] = new_idx;
      new_instructions.push_back(fn->instructions[old_idx]);
      ++new_idx;
    }
  }

  // Step 3: Fix jump targets (Goto and If instructions)
  for (size_t i = 0; i < new_instructions.size(); ++i) {
    Instruction &inst = new_instructions[i];
    int old_idx = -1;

    // Find the original index of this instruction
    for (size_t j = 0; j < index_map.size(); ++j) {
      if (index_map[j] == (int)i) {
        old_idx = j;
        break;
      }
    }

    if (old_idx < 0)
      continue;

    switch (inst.operation) {
    case Operation::Goto:
    case Operation::If: {
      if (inst.operand0.has_value()) {
        int old_offset = inst.operand0.value();
        int old_target = old_idx + old_offset;

        // Find new index of target
        if (old_target >= 0 && old_target < (int)index_map.size()) {
          int new_target = index_map[old_target];
          if (new_target >= 0) {
            int new_offset = new_target - (int)i;
            inst.operand0 = new_offset;
          }
        }
      }
      break;
    }
    default:
      break;
    }
  }

  fn->instructions = std::move(new_instructions);

  // Step 4: Find used locals and remove dead stores using liveness analysis
  auto [used_locals, has_references] = find_used_locals(fn->instructions);

  // Only perform dead store elimination if the function doesn't use reference
  // variables. Reference operations (PushReference/LoadReference/StoreReference)
  // use indices into local_ref_vars_, not local_vars_, so we can't reliably
  // track which locals are accessed through references.
  if (!has_references && !fn->instructions.empty()) {
    // Build CFG and compute liveness
    auto blocks = build_basic_blocks(fn->instructions);
    auto live_out =
        compute_liveness(fn->instructions, blocks, fn->local_vars_.size());

    // Rebuild instructions, removing dead stores
    // A store is dead if the local is not live after the store
    std::vector<Instruction> final_instructions;
    final_instructions.reserve(fn->instructions.size());

    // Parameters should not have their stores removed
    std::unordered_set<int> params;
    for (size_t i = 0; i < fn->parameter_count_ && i < fn->local_vars_.size();
         ++i) {
      params.insert(i);
    }

    for (size_t i = 0; i < fn->instructions.size(); ++i) {
      const Instruction &inst = fn->instructions[i];
      bool keep = true;
      bool replace_with_pop = false;

      if (inst.operation == Operation::StoreLocal && inst.operand0.has_value()) {
        int local_idx = inst.operand0.value();

        // Don't remove stores to parameters
        if (params.count(local_idx)) {
          keep = true;
        } else if (!live_out[i].count(local_idx)) {
          // Local is not live after this store - dead store
          // Replace with Pop to maintain stack balance
          keep = false;
          replace_with_pop = true;
        }
      }

      if (keep) {
        final_instructions.push_back(inst);
      } else if (replace_with_pop) {
        // Replace dead store with pop to maintain stack balance
        final_instructions.push_back(Instruction(Operation::Pop, std::nullopt));
      }
    }

    fn->instructions = std::move(final_instructions);
  }

  // Step 5: Remove unused constants
  std::unordered_set<int> used_constants =
      find_used_constants(fn->instructions);

  if (used_constants.size() < fn->constants_.size()) {
    // Rebuild constants array, remapping indices
    std::vector<bytecode::Constant *> new_constants;
    std::vector<int> const_map(fn->constants_.size(), -1);
    int new_const_idx = 0;

    for (int old_idx = 0; old_idx < (int)fn->constants_.size(); ++old_idx) {
      if (used_constants.count(old_idx)) {
        const_map[old_idx] = new_const_idx;
        new_constants.push_back(fn->constants_[old_idx]);
        ++new_const_idx;
      } else {
        // Delete unused constant
        delete fn->constants_[old_idx];
      }
    }

    fn->constants_ = std::move(new_constants);

    // Update LoadConst instructions to use new indices
    for (Instruction &inst : fn->instructions) {
      if (inst.operation == Operation::LoadConst && inst.operand0.has_value()) {
        int old_idx = inst.operand0.value();
        if (old_idx >= 0 && old_idx < (int)const_map.size()) {
          int new_idx = const_map[old_idx];
          if (new_idx >= 0) {
            inst.operand0 = new_idx;
          }
        }
      }
    }
  }

  // Step 6: Remove unused functions (nested functions that are never loaded)
  std::unordered_set<int> used_functions =
      find_used_functions(fn->instructions);

  if (used_functions.size() < fn->functions_.size()) {
    // Rebuild functions array, remapping indices
    std::vector<bytecode::Function *> new_functions;
    std::vector<int> func_map(fn->functions_.size(), -1);
    int new_func_idx = 0;

    for (int old_idx = 0; old_idx < (int)fn->functions_.size(); ++old_idx) {
      if (used_functions.count(old_idx)) {
        func_map[old_idx] = new_func_idx;
        new_functions.push_back(fn->functions_[old_idx]);
        ++new_func_idx;
      }
      // Note: We don't delete unused functions here because they might
      // be referenced elsewhere or the user might want to keep them
    }

    fn->functions_ = std::move(new_functions);

    // Update LoadFunc instructions to use new indices
    for (Instruction &inst : fn->instructions) {
      if (inst.operation == Operation::LoadFunc && inst.operand0.has_value()) {
        int old_idx = inst.operand0.value();
        if (old_idx >= 0 && old_idx < (int)func_map.size()) {
          int new_idx = func_map[old_idx];
          if (new_idx >= 0) {
            inst.operand0 = new_idx;
          }
        }
      }
    }
  }
}

// Public entry point
void eliminate_dead_code(bytecode::Function *func) {
  // First recursively process nested functions
  for (bytecode::Function *child : func->functions_) {
    eliminate_dead_code(child);
  }

  // Then process this function
  eliminate_dead_code_one(func);
}

} // namespace bytecode::opt_deadcode
