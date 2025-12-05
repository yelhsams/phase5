#include "opt_deadcode.hpp"
#include "instructions.hpp"
#include "types.hpp"
#include <cassert>
#include <set>
#include <unordered_map>
#include <unordered_set>

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

// Find which local variables are read (used)
static std::unordered_set<int>
find_used_locals(const std::vector<Instruction> &instructions) {
  std::unordered_set<int> used;

  for (const Instruction &inst : instructions) {
    switch (inst.operation) {
    case Operation::LoadLocal:
    case Operation::PushReference: {
      // Reading a local variable
      if (inst.operand0.has_value()) {
        used.insert(inst.operand0.value());
      }
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

  return used;
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

  // Step 4: Find used locals and remove dead stores
  std::unordered_set<int> used_locals = find_used_locals(fn->instructions);

  // Rebuild instructions, removing dead stores
  // A store is dead if:
  // 1. The local is never read after the store (before being overwritten)
  // 2. The local is not a parameter (parameters might be used externally)
  std::vector<Instruction> final_instructions;
  final_instructions.reserve(fn->instructions.size());

  // Track last write to each local
  std::unordered_map<int, size_t> last_write;
  std::unordered_set<int> params;
  for (size_t i = 0; i < fn->parameter_count_ && i < fn->local_vars_.size(); ++i) {
    params.insert(i);
  }

  for (size_t i = 0; i < fn->instructions.size(); ++i) {
    const Instruction &inst = fn->instructions[i];
    bool keep = true;

    if (inst.operation == Operation::StoreLocal && inst.operand0.has_value()) {
      int local_idx = inst.operand0.value();

      // Don't remove stores to parameters
      if (params.count(local_idx)) {
        keep = true;
      } else if (!used_locals.count(local_idx)) {
        // Local is never read - this store is dead
        keep = false;
      } else {
        // Check if there's a later write before any read
        // (simple approach: if this is the last write and local is used, keep it)
        // For simplicity, we'll keep stores if the local is used anywhere
        keep = true;
      }

      if (keep) {
        last_write[local_idx] = i;
      }
    }

    if (keep) {
      final_instructions.push_back(inst);
    }
  }

  fn->instructions = std::move(final_instructions);

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
