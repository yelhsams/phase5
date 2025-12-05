#include "opt_constprop.hpp"
#include "instructions.hpp"
#include "types.hpp"
#include <cassert>

namespace bytecode::opt_constprop {

// Represents a constant value that can be tracked
struct ConstantValue {
  enum Kind { UNKNOWN, INTEGER, BOOLEAN, STRING, NONE } kind = UNKNOWN;
  int32_t int_val = 0;
  bool bool_val = false;
  std::string str_val;

  static ConstantValue unknown() {
    ConstantValue cv;
    cv.kind = UNKNOWN;
    cv.int_val = 0;
    cv.bool_val = false;
    cv.str_val = "";
    return cv;
  }
  static ConstantValue integer(int32_t v) {
    ConstantValue cv;
    cv.kind = INTEGER;
    cv.int_val = v;
    cv.bool_val = false;
    cv.str_val = "";
    return cv;
  }
  static ConstantValue boolean(bool v) {
    ConstantValue cv;
    cv.kind = BOOLEAN;
    cv.bool_val = v;
    cv.int_val = 0;
    cv.str_val = "";
    return cv;
  }
  static ConstantValue string(const std::string &v) {
    ConstantValue cv;
    cv.kind = STRING;
    cv.str_val = v;
    cv.int_val = 0; // Initialize unused fields
    cv.bool_val = false;
    return cv;
  }
  static ConstantValue none() {
    ConstantValue cv;
    cv.kind = NONE;
    cv.int_val = 0;
    cv.bool_val = false;
    cv.str_val = "";
    return cv;
  }

  bool is_known() const { return kind != UNKNOWN; }
  bool is_integer() const { return kind == INTEGER; }
  bool is_boolean() const { return kind == BOOLEAN; }
  bool is_string() const { return kind == STRING; }
  bool is_none() const { return kind == NONE; }
};

// Helper to get constant value from a Constant*
static ConstantValue get_constant_value(bytecode::Constant *c) {
  if (auto *i = dynamic_cast<bytecode::Constant::Integer *>(c)) {
    return ConstantValue::integer(i->value);
  } else if (auto *b = dynamic_cast<bytecode::Constant::Boolean *>(c)) {
    return ConstantValue::boolean(b->value);
  } else if (auto *s = dynamic_cast<bytecode::Constant::String *>(c)) {
    return ConstantValue::string(s->value);
  } else if (dynamic_cast<bytecode::Constant::None *>(c)) {
    return ConstantValue::none();
  }
  return ConstantValue::unknown();
}

// Helper to find or create a constant in the function's constant pool
static int find_or_create_constant(bytecode::Function *fn,
                                   const ConstantValue &cv) {
  if (!cv.is_known())
    return -1;

  // Search for existing constant
  for (size_t i = 0; i < fn->constants_.size(); ++i) {
    ConstantValue existing = get_constant_value(fn->constants_[i]);
    if (existing.kind == cv.kind) {
      switch (cv.kind) {
      case ConstantValue::INTEGER:
        if (existing.int_val == cv.int_val)
          return i;
        break;
      case ConstantValue::BOOLEAN:
        if (existing.bool_val == cv.bool_val)
          return i;
        break;
      case ConstantValue::STRING:
        if (existing.str_val == cv.str_val)
          return i;
        break;
      case ConstantValue::NONE:
        return i;
      case ConstantValue::UNKNOWN:
        break;
      }
    }
  }

  // Create new constant
  bytecode::Constant *new_const = nullptr;
  switch (cv.kind) {
  case ConstantValue::INTEGER:
    new_const = new bytecode::Constant::Integer(cv.int_val);
    break;
  case ConstantValue::BOOLEAN:
    new_const = new bytecode::Constant::Boolean(cv.bool_val);
    break;
  case ConstantValue::STRING:
    new_const = new bytecode::Constant::String(cv.str_val);
    break;
  case ConstantValue::NONE:
    new_const = new bytecode::Constant::None();
    break;
  case ConstantValue::UNKNOWN:
    return -1;
  }

  fn->constants_.push_back(new_const);
  return fn->constants_.size() - 1;
}

// Evaluate binary operations with constants
static ConstantValue eval_binary(Operation op, const ConstantValue &a,
                                 const ConstantValue &b) {
  if (!a.is_known() || !b.is_known())
    return ConstantValue::unknown();

  switch (op) {
  case Operation::Add:
    if (a.is_integer() && b.is_integer()) {
      return ConstantValue::integer(a.int_val + b.int_val);
    } else if (a.is_string() && b.is_string()) {
      return ConstantValue::string(a.str_val + b.str_val);
    }
    break;
  case Operation::Sub:
    if (a.is_integer() && b.is_integer()) {
      return ConstantValue::integer(a.int_val - b.int_val);
    }
    break;
  case Operation::Mul:
    if (a.is_integer() && b.is_integer()) {
      return ConstantValue::integer(a.int_val * b.int_val);
    }
    break;
  case Operation::Div:
    if (a.is_integer() && b.is_integer() && b.int_val != 0) {
      return ConstantValue::integer(a.int_val / b.int_val);
    }
    break;
  case Operation::Gt:
    if (a.is_integer() && b.is_integer()) {
      return ConstantValue::boolean(a.int_val > b.int_val);
    }
    break;
  case Operation::Geq:
    if (a.is_integer() && b.is_integer()) {
      return ConstantValue::boolean(a.int_val >= b.int_val);
    }
    break;
  case Operation::Eq:
    if (a.kind == b.kind) {
      switch (a.kind) {
      case ConstantValue::INTEGER:
        return ConstantValue::boolean(a.int_val == b.int_val);
      case ConstantValue::BOOLEAN:
        return ConstantValue::boolean(a.bool_val == b.bool_val);
      case ConstantValue::STRING:
        return ConstantValue::boolean(a.str_val == b.str_val);
      case ConstantValue::NONE:
        return ConstantValue::boolean(true);
      case ConstantValue::UNKNOWN:
        break;
      }
    }
    break;
  case Operation::And:
    if (a.is_boolean() && b.is_boolean()) {
      return ConstantValue::boolean(a.bool_val && b.bool_val);
    }
    break;
  case Operation::Or:
    if (a.is_boolean() && b.is_boolean()) {
      return ConstantValue::boolean(a.bool_val || b.bool_val);
    }
    break;
  default:
    break;
  }

  return ConstantValue::unknown();
}

// Evaluate unary operations with constants
static ConstantValue eval_unary(Operation op, const ConstantValue &a) {
  if (!a.is_known())
    return ConstantValue::unknown();

  switch (op) {
  case Operation::Neg:
    if (a.is_integer()) {
      return ConstantValue::integer(-a.int_val);
    }
    break;
  case Operation::Not:
    if (a.is_boolean()) {
      return ConstantValue::boolean(!a.bool_val);
    }
    break;
  default:
    break;
  }

  return ConstantValue::unknown();
}

// Perform constant propagation on a single function
static void constant_propagate_one(bytecode::Function *fn) {
  // Track constant values in local variables
  std::vector<ConstantValue> locals(fn->local_vars_.size(),
                                    ConstantValue::unknown());

  // Track stack state (for operations that use stack)
  // We'll use a simple approach: track the top few stack elements
  // For simplicity, we'll track up to 4 stack elements
  std::vector<ConstantValue> stack;

  std::vector<Instruction> new_instructions;
  new_instructions.reserve(fn->instructions.size());

  for (size_t i = 0; i < fn->instructions.size(); ++i) {
    const Instruction &inst = fn->instructions[i];
    bool replaced = false;

    switch (inst.operation) {
    case Operation::LoadConst: {
      int const_idx = inst.operand0.value();
      if (const_idx >= 0 && const_idx < (int)fn->constants_.size()) {
        ConstantValue cv = get_constant_value(fn->constants_[const_idx]);
        stack.push_back(cv);
        new_instructions.push_back(inst);
      } else {
        stack.push_back(ConstantValue::unknown());
        new_instructions.push_back(inst);
      }
      break;
    }

    case Operation::LoadLocal: {
      int local_idx = inst.operand0.value();
      if (local_idx >= 0 && local_idx < (int)locals.size()) {
        ConstantValue cv = locals[local_idx];
        stack.push_back(cv);
        // NOTE: We do NOT replace LoadLocal with LoadConst here because
        // this simple linear pass doesn't do proper dataflow analysis.
        // In a loop, locals may be modified and we'd incorrectly use
        // the pre-loop value. We still track the value for folding later ops.
      } else {
        stack.push_back(ConstantValue::unknown());
      }
      new_instructions.push_back(inst);
      break;
    }

    case Operation::StoreLocal: {
      int local_idx = inst.operand0.value();
      if (local_idx >= 0 && local_idx < (int)locals.size()) {
        if (!stack.empty()) {
          locals[local_idx] = stack.back();
          stack.pop_back();
        } else {
          locals[local_idx] = ConstantValue::unknown();
        }
      }
      new_instructions.push_back(inst);
      break;
    }

    case Operation::Add:
    case Operation::Sub:
    case Operation::Mul:
    case Operation::Div:
    case Operation::Gt:
    case Operation::Geq:
    case Operation::Eq:
    case Operation::And:
    case Operation::Or: {
      // Binary operations: pop two values, compute result, push result
      // NOTE: We track the result for downstream operations, but we do NOT
      // replace the operation with LoadConst. In stack-based bytecode, replacing
      // "load_const X; load_const Y; add" with "load_const X; load_const Y; load_const Z"
      // corrupts the stack (leaves X and Y on stack). Proper constant folding
      // would need peephole optimization to remove operand loads too.
      if (stack.size() >= 2) {
        ConstantValue b = stack.back();
        stack.pop_back();
        ConstantValue a = stack.back();
        stack.pop_back();

        ConstantValue result = eval_binary(inst.operation, a, b);
        stack.push_back(result);
      } else {
        // Can't track stack, mark as unknown
        if (!stack.empty())
          stack.pop_back();
        if (!stack.empty())
          stack.pop_back();
        stack.push_back(ConstantValue::unknown());
      }
      new_instructions.push_back(inst);
      break;
    }

    case Operation::Neg:
    case Operation::Not: {
      // Unary operations: pop one value, compute result, push result
      // Same issue as binary ops - we can't safely replace without removing
      // the operand load instruction, so just track for downstream use.
      if (!stack.empty()) {
        ConstantValue a = stack.back();
        stack.pop_back();

        ConstantValue result = eval_unary(inst.operation, a);
        stack.push_back(result);
      } else {
        stack.push_back(ConstantValue::unknown());
      }
      new_instructions.push_back(inst);
      break;
    }

    case Operation::Dup: {
      if (!stack.empty()) {
        ConstantValue top = stack.back();
        stack.push_back(top);
      } else {
        stack.push_back(ConstantValue::unknown());
        stack.push_back(ConstantValue::unknown());
      }
      new_instructions.push_back(inst);
      break;
    }

    case Operation::Pop: {
      if (!stack.empty()) {
        stack.pop_back();
      }
      new_instructions.push_back(inst);
      break;
    }

    case Operation::Swap: {
      if (stack.size() >= 2) {
        ConstantValue a = stack.back();
        stack.pop_back();
        ConstantValue b = stack.back();
        stack.pop_back();
        stack.push_back(a);
        stack.push_back(b);
      } else {
        // Can't track properly
        while (stack.size() < 2)
          stack.push_back(ConstantValue::unknown());
        ConstantValue a = stack.back();
        stack.pop_back();
        ConstantValue b = stack.back();
        stack.pop_back();
        stack.push_back(a);
        stack.push_back(b);
      }
      new_instructions.push_back(inst);
      break;
    }

    case Operation::LoadGlobal:
    case Operation::StoreGlobal:
    case Operation::PushReference:
    case Operation::LoadReference:
    case Operation::StoreReference:
    case Operation::AllocRecord:
    case Operation::FieldLoad:
    case Operation::FieldStore:
    case Operation::IndexLoad:
    case Operation::IndexStore:
    case Operation::AllocClosure:
    case Operation::Call:
    case Operation::LoadFunc:
    case Operation::Return:
    case Operation::Goto:
    case Operation::If: {
      // For operations that modify stack in complex ways or have side effects,
      // we conservatively mark affected values as unknown
      // This is a simplified approach - a full implementation would track
      // stack more precisely

      // For If, we might be able to optimize if the condition is constant
      // NOTE: This optimization is DISABLED because it's unsafe without also
      // removing the instruction that produces the condition value. When we
      // remove an If or convert it to Goto, the condition value is still on
      // the stack if it was produced by a preceding instruction (like LoadConst).
      // A proper fix would require peephole optimization to also remove the
      // condition-producing instruction.
      //
      // For now, we just keep all If instructions as-is:
      new_instructions.push_back(inst);

      // Reset stack tracking for control flow
      if (inst.operation == Operation::Goto ||
          inst.operation == Operation::If ||
          inst.operation == Operation::Return) {
        // Control flow - can't track stack precisely across jumps
        // In a more sophisticated implementation, we'd do dataflow analysis
        stack.clear();
      }
      break;
    }
    }
  }

  fn->instructions = std::move(new_instructions);
}

// Public entry point
void constant_propagate(bytecode::Function *func) {
  // First recursively process nested functions
  for (bytecode::Function *child : func->functions_) {
    constant_propagate(child);
  }

  // Then process this function
  constant_propagate_one(func);
}

} // namespace bytecode::opt_constprop
