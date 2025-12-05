#include "opt_inline.hpp"
#include "instructions.hpp"
#include <algorithm>
#include <cassert>

namespace bytecode::opt_inline {

// Helper: determine if a function is eligible for inlining

static bool is_inlinable(Function *fn) {
  // No free variables
  if (!fn->free_vars_.empty())
    return false;

  // No nested functions (simplifies locals & closures)
  if (!fn->functions_.empty())
    return false;

  // Function is small enough
  if (fn->instructions.size() > 40)
    return false;

  // Must end with a Return instruction
  if (fn->instructions.empty())
    return false;
  if (fn->instructions.back().operation != Operation::Return)
    return false;

  return true;
}

// Helper: clone and remap local variable indices
static std::vector<Instruction> clone_and_remap(Function *callee,
                                                size_t local_offset) {
  std::vector<Instruction> out;

  for (const Instruction &inst : callee->instructions) {
    Instruction cloned = inst;

    switch (inst.operation) {
    case Operation::LoadLocal:
    case Operation::StoreLocal: {
      int old = inst.operand0.value();
      cloned.operand0 = old + local_offset;
      break;
    }
    default:
      break;
    }

    out.push_back(cloned);
  }

  // Remove final Return
  if (!out.empty() && out.back().operation == Operation::Return)
    out.pop_back();

  return out;
}

// Main inlining logic for one function

static void inline_one(Function *f) {
  auto &code = f->instructions;
  std::vector<Instruction> new_code;

  for (size_t i = 0; i < code.size(); i++) {
    Instruction inst = code[i];

    // Look for CALL 0 (zero-argument calls only)
    // For calls with arguments, the callee position is complex to determine
    // because we'd need to account for the stack effects of each argument.
    // For safety, we only inline direct calls with no arguments where
    // we can verify the pattern: load_func X, alloc_closure Y, call 0
    if (inst.operation == Operation::Call) {
      int arg_count = inst.operand0.value();

      // Only consider zero-argument calls for inlining
      if (arg_count != 0) {
        new_code.push_back(inst);
        continue;
      }

      // For call 0, the pattern must be:
      //   load_func X
      //   alloc_closure Y
      //   call 0
      if (i < 2) {
        new_code.push_back(inst);
        continue;
      }

      Instruction &alloc_closure_inst = code[i - 1];
      Instruction &load_func_inst = code[i - 2];

      if (alloc_closure_inst.operation != Operation::AllocClosure ||
          load_func_inst.operation != Operation::LoadFunc) {
        new_code.push_back(inst);
        continue;
      }

      int func_index = load_func_inst.operand0.value();
      if (func_index < 0 || (size_t)func_index >= f->functions_.size()) {
        new_code.push_back(inst);
        continue;
      }
      Function *callee = f->functions_[func_index];

      if (!is_inlinable(callee)) {
        new_code.push_back(inst);
        continue;
      }

      // --- INLINING STARTS HERE ---

      // 1. Copy everything except LOAD_FUNC, ALLOC_CLOSURE
      size_t start_of_call_seq = i - 2;

      for (size_t k = new_code.size(); k < start_of_call_seq; k++)
        new_code.push_back(code[k]);

      // 2. Extend caller's locals
      size_t local_offset = f->local_vars_.size();
      f->local_vars_.insert(f->local_vars_.end(), callee->local_vars_.begin(),
                            callee->local_vars_.end());

      // 3. Insert cloned & remapped callee body
      auto cloned = clone_and_remap(callee, local_offset);
      new_code.insert(new_code.end(), cloned.begin(), cloned.end());

      // 4. Skip over LOAD_FUNC, ALLOC_CLOSURE, and CALL
      // so we do NOT re-copy them
      continue;
    }

    new_code.push_back(inst);
  }

  code = std::move(new_code);
}

// Public entry point

void inline_functions(Function *func) {
  // First recursively inline nested functions
  for (Function *child : func->functions_)
    inline_functions(child);

  // Then inline into this function
  inline_one(func);
}

} // namespace bytecode::opt_inline
