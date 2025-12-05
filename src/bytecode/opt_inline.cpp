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

    // Look for CALL m
    if (inst.operation == Operation::Call) {
      int arg_count = inst.operand0.value();

      // LOAD_FUNC must appear arg_count+1 instructions before CALL
      if (i < (size_t)arg_count + 1) {
        new_code.push_back(inst);
        continue;
      }

      Instruction &load_func_inst = code[i - (arg_count + 1)];
      if (load_func_inst.operation != Operation::LoadFunc) {
        new_code.push_back(inst);
        continue;
      }

      int func_index = load_func_inst.operand0.value();
      Function *callee = f->functions_[func_index];

      if (!is_inlinable(callee)) {
        new_code.push_back(inst);
        continue;
      }

      // Verify parameter count matches
      if (callee->parameter_count_ != (uint32_t)arg_count) {
        new_code.push_back(inst);
        continue;
      }

      // --- INLINING STARTS HERE ---

      // 1. Extend caller's locals to accommodate callee's locals
      size_t local_offset = f->local_vars_.size();
      f->local_vars_.insert(f->local_vars_.end(), callee->local_vars_.begin(),
                            callee->local_vars_.end());

      // 2. The arg_count arguments are already on the stack (pushed before LOAD_FUNC)
      //    We need to store them into the callee's parameter locals
      //
      //    Stack layout before CALL:
      //      ... [arg0] [arg1] ... [arg_{n-1}] [function] <-- top
      //
      //    After inlining, we need:
      //      StoreLocal(param0, arg0)
      //      StoreLocal(param1, arg1)
      //      ...
      //      <callee body>
      //
      // Generate StoreLocal for each parameter (in reverse order since stack is LIFO)
      for (int param_idx = arg_count - 1; param_idx >= 0; param_idx--) {
        Instruction store_inst(Operation::StoreLocal, local_offset + param_idx);
        new_code.push_back(store_inst);
      }

      // 3. Insert cloned & remapped callee body (excluding Return)
      auto cloned = clone_and_remap(callee, local_offset);
      new_code.insert(new_code.end(), cloned.begin(), cloned.end());

      // 4. Skip over LOAD_FUNC and CALL (arguments already processed and on stack)
      // We continue loop which skips to i+1, effectively skipping the CALL
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
