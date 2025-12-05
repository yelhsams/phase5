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

  // Function is small enough - increased threshold for more aggressive inlining
  if (fn->instructions.size() > 100)
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
  new_code.reserve(code.size());

  for (size_t i = 0; i < code.size(); i++) {
    const Instruction &inst = code[i];

    // Look for CALL m
    if (inst.operation == Operation::Call) {
      int arg_count = inst.operand0.value();

      // Check if there's a LoadFunc instruction at the expected position
      // The call sequence is: LoadFunc, [arg instructions...], Call
      // So LoadFunc should be at position (i - arg_count - 1) in original code
      // But we've been pushing instructions to new_code, so we need to check
      // the original code array
      if (i < (size_t)arg_count + 1) {
        new_code.push_back(inst);
        continue;
      }

      size_t load_func_pos = i - arg_count - 1;
      const Instruction &load_func_inst = code[load_func_pos];
      if (load_func_inst.operation != Operation::LoadFunc) {
        new_code.push_back(inst);
        continue;
      }

      int func_index = load_func_inst.operand0.value();
      if (func_index < 0 || func_index >= (int)f->functions_.size()) {
        new_code.push_back(inst);
        continue;
      }

      Function *callee = f->functions_[func_index];

      if (!is_inlinable(callee)) {
        new_code.push_back(inst);
        continue;
      }

      // Verify parameter count matches argument count
      if ((int)callee->parameter_count_ != arg_count) {
        new_code.push_back(inst);
        continue;
      }

      // --- INLINING STARTS HERE ---

      // At this point, new_code has already accumulated:
      // - Instructions before LoadFunc
      // - LoadFunc instruction
      // - arg_count argument-producing instructions
      //
      // We need to:
      // 1. Remove the LoadFunc instruction (it's not needed for inlining)
      // 2. Keep the argument instructions (they produce values on stack)
      // 3. Add StoreLocal instructions to move args from stack to local slots
      // 4. Insert the cloned callee body

      // Calculate where the call sequence starts in new_code
      // new_code currently has load_func_pos + 1 + arg_count instructions
      // (everything through the arg instructions, not including Call)
      size_t new_code_call_seq_start = new_code.size() - arg_count - 1;

      // Remove LoadFunc instruction (it's at new_code_call_seq_start)
      // The argument instructions after it remain in place
      new_code.erase(new_code.begin() + new_code_call_seq_start);

      // Extend caller's locals with callee's locals
      size_t local_offset = f->local_vars_.size();
      f->local_vars_.insert(f->local_vars_.end(), callee->local_vars_.begin(),
                            callee->local_vars_.end());

      // Store arguments from stack to the remapped parameter locals
      // Arguments are on stack in order [arg0, arg1, ..., argN-1] (arg0 at bottom)
      // We need to store them in reverse order (pop argN-1 first)
      for (int j = arg_count - 1; j >= 0; --j) {
        new_code.push_back(
            Instruction(Operation::StoreLocal, (int32_t)(local_offset + j)));
      }

      // Insert cloned & remapped callee body (without final Return)
      auto cloned = clone_and_remap(callee, local_offset);
      new_code.insert(new_code.end(), cloned.begin(), cloned.end());

      // The inlined body's last instruction pushed the return value to stack,
      // which is what we want (Call would have done the same)
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

  // Run multiple passes of inlining until no more changes or limit reached
  // This handles cases where inlined code contains more inlinable calls
  const int MAX_PASSES = 3;
  const size_t MAX_CODE_SIZE = 10000; // Prevent code explosion

  for (int pass = 0; pass < MAX_PASSES; ++pass) {
    size_t old_size = func->instructions.size();
    inline_one(func);

    // Stop if no change or code is getting too large
    if (func->instructions.size() == old_size ||
        func->instructions.size() > MAX_CODE_SIZE) {
      break;
    }
  }
}

} // namespace bytecode::opt_inline
