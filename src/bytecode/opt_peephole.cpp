#include "opt_peephole.hpp"
#include "instructions.hpp"
#include "types.hpp"
#include <cstdint>
#include <optional>
#include <vector>

namespace bytecode::opt_peephole {

// Get constant value if instruction is LoadConst
struct ConstVal {
  enum Kind { NONE, INT, BOOL, STR } kind = NONE;
  int32_t i = 0;
  bool b = false;
  std::string s;
};

static std::optional<ConstVal> get_const(const Instruction &inst,
                                         const std::vector<Constant *> &consts) {
  if (inst.operation != Operation::LoadConst)
    return std::nullopt;

  int idx = inst.operand0.value();
  if (idx < 0 || idx >= (int)consts.size())
    return std::nullopt;

  Constant *c = consts[idx];
  ConstVal cv;

  if (auto *ic = dynamic_cast<Constant::Integer *>(c)) {
    cv.kind = ConstVal::INT;
    cv.i = ic->value;
    return cv;
  }
  if (auto *bc = dynamic_cast<Constant::Boolean *>(c)) {
    cv.kind = ConstVal::BOOL;
    cv.b = bc->value;
    return cv;
  }
  if (auto *sc = dynamic_cast<Constant::String *>(c)) {
    cv.kind = ConstVal::STR;
    cv.s = sc->value;
    return cv;
  }
  if (dynamic_cast<Constant::None *>(c)) {
    cv.kind = ConstVal::NONE;
    return cv;
  }

  return std::nullopt;
}

// Find or create a constant in the pool
static int find_or_create_int(Function *fn, int32_t value) {
  for (size_t i = 0; i < fn->constants_.size(); ++i) {
    if (auto *ic = dynamic_cast<Constant::Integer *>(fn->constants_[i])) {
      if (ic->value == value)
        return i;
    }
  }
  fn->constants_.push_back(new Constant::Integer(value));
  return fn->constants_.size() - 1;
}

static int find_or_create_bool(Function *fn, bool value) {
  for (size_t i = 0; i < fn->constants_.size(); ++i) {
    if (auto *bc = dynamic_cast<Constant::Boolean *>(fn->constants_[i])) {
      if (bc->value == value)
        return i;
    }
  }
  fn->constants_.push_back(new Constant::Boolean(value));
  return fn->constants_.size() - 1;
}

static int find_or_create_string(Function *fn, const std::string &value) {
  for (size_t i = 0; i < fn->constants_.size(); ++i) {
    if (auto *sc = dynamic_cast<Constant::String *>(fn->constants_[i])) {
      if (sc->value == value)
        return i;
    }
  }
  fn->constants_.push_back(new Constant::String(value));
  return fn->constants_.size() - 1;
}

// Check if operation is a binary arithmetic/comparison op
static bool is_binary_op(Operation op) {
  switch (op) {
  case Operation::Add:
  case Operation::Sub:
  case Operation::Mul:
  case Operation::Div:
  case Operation::Gt:
  case Operation::Geq:
  case Operation::Eq:
  case Operation::And:
  case Operation::Or:
    return true;
  default:
    return false;
  }
}

// Check if operation is a unary op
static bool is_unary_op(Operation op) {
  return op == Operation::Neg || op == Operation::Not;
}

// Evaluate binary operation on constants
static std::optional<ConstVal> eval_binary(Operation op, const ConstVal &a,
                                           const ConstVal &b) {
  ConstVal result;

  switch (op) {
  case Operation::Add:
    if (a.kind == ConstVal::INT && b.kind == ConstVal::INT) {
      result.kind = ConstVal::INT;
      result.i = a.i + b.i;
      return result;
    }
    if (a.kind == ConstVal::STR && b.kind == ConstVal::STR) {
      result.kind = ConstVal::STR;
      result.s = a.s + b.s;
      return result;
    }
    break;

  case Operation::Sub:
    if (a.kind == ConstVal::INT && b.kind == ConstVal::INT) {
      result.kind = ConstVal::INT;
      result.i = a.i - b.i;
      return result;
    }
    break;

  case Operation::Mul:
    if (a.kind == ConstVal::INT && b.kind == ConstVal::INT) {
      result.kind = ConstVal::INT;
      result.i = a.i * b.i;
      return result;
    }
    break;

  case Operation::Div:
    if (a.kind == ConstVal::INT && b.kind == ConstVal::INT && b.i != 0) {
      result.kind = ConstVal::INT;
      result.i = a.i / b.i;
      return result;
    }
    break;

  case Operation::Gt:
    if (a.kind == ConstVal::INT && b.kind == ConstVal::INT) {
      result.kind = ConstVal::BOOL;
      result.b = a.i > b.i;
      return result;
    }
    break;

  case Operation::Geq:
    if (a.kind == ConstVal::INT && b.kind == ConstVal::INT) {
      result.kind = ConstVal::BOOL;
      result.b = a.i >= b.i;
      return result;
    }
    break;

  case Operation::Eq:
    if (a.kind == b.kind) {
      result.kind = ConstVal::BOOL;
      switch (a.kind) {
      case ConstVal::INT:
        result.b = a.i == b.i;
        return result;
      case ConstVal::BOOL:
        result.b = a.b == b.b;
        return result;
      case ConstVal::STR:
        result.b = a.s == b.s;
        return result;
      case ConstVal::NONE:
        result.b = true;
        return result;
      }
    }
    break;

  case Operation::And:
    if (a.kind == ConstVal::BOOL && b.kind == ConstVal::BOOL) {
      result.kind = ConstVal::BOOL;
      result.b = a.b && b.b;
      return result;
    }
    break;

  case Operation::Or:
    if (a.kind == ConstVal::BOOL && b.kind == ConstVal::BOOL) {
      result.kind = ConstVal::BOOL;
      result.b = a.b || b.b;
      return result;
    }
    break;

  default:
    break;
  }

  return std::nullopt;
}

// Evaluate unary operation on constant
static std::optional<ConstVal> eval_unary(Operation op, const ConstVal &a) {
  ConstVal result;

  switch (op) {
  case Operation::Neg:
    if (a.kind == ConstVal::INT) {
      result.kind = ConstVal::INT;
      result.i = -a.i;
      return result;
    }
    break;

  case Operation::Not:
    if (a.kind == ConstVal::BOOL) {
      result.kind = ConstVal::BOOL;
      result.b = !a.b;
      return result;
    }
    break;

  default:
    break;
  }

  return std::nullopt;
}

// Create LoadConst instruction for a constant value
static Instruction make_load_const(Function *fn, const ConstVal &cv) {
  int idx;
  switch (cv.kind) {
  case ConstVal::INT:
    idx = find_or_create_int(fn, cv.i);
    break;
  case ConstVal::BOOL:
    idx = find_or_create_bool(fn, cv.b);
    break;
  case ConstVal::STR:
    idx = find_or_create_string(fn, cv.s);
    break;
  default:
    // For NONE, find or create None constant
    for (size_t i = 0; i < fn->constants_.size(); ++i) {
      if (dynamic_cast<Constant::None *>(fn->constants_[i])) {
        idx = i;
        goto found;
      }
    }
    fn->constants_.push_back(new Constant::None());
    idx = fn->constants_.size() - 1;
  found:
    break;
  }
  return Instruction(Operation::LoadConst, idx);
}

// Find all instructions that are jump targets
static std::vector<bool> find_jump_targets(const std::vector<Instruction> &code) {
  std::vector<bool> is_target(code.size(), false);

  for (size_t i = 0; i < code.size(); ++i) {
    const auto &inst = code[i];
    if (inst.operation == Operation::Goto || inst.operation == Operation::If) {
      if (inst.operand0.has_value()) {
        int offset = inst.operand0.value();
        int target = (int)i + offset;
        if (target >= 0 && target < (int)code.size()) {
          is_target[target] = true;
        }
      }
    }
  }

  return is_target;
}

// Rebuild jump targets after instruction removal
static void rebuild_jumps(std::vector<Instruction> &code,
                          const std::vector<int> &old_to_new) {
  for (size_t i = 0; i < code.size(); ++i) {
    auto &inst = code[i];
    if (inst.operation == Operation::Goto || inst.operation == Operation::If) {
      if (inst.operand0.has_value()) {
        // Find original index of this instruction
        int old_idx = -1;
        for (size_t j = 0; j < old_to_new.size(); ++j) {
          if (old_to_new[j] == (int)i) {
            old_idx = j;
            break;
          }
        }
        if (old_idx < 0) continue;

        int old_offset = inst.operand0.value();
        int old_target = old_idx + old_offset;

        if (old_target >= 0 && old_target < (int)old_to_new.size()) {
          int new_target = old_to_new[old_target];
          if (new_target >= 0) {
            int new_offset = new_target - (int)i;
            inst.operand0 = new_offset;
          }
        }
      }
    }
  }
}

// Single pass of peephole optimization
static bool peephole_one_pass(Function *fn) {
  auto &code = fn->instructions;
  if (code.size() < 2)
    return false;

  // Find jump targets - we won't optimize sequences that include these
  std::vector<bool> is_target = find_jump_targets(code);

  std::vector<Instruction> new_code;
  new_code.reserve(code.size());
  std::vector<int> old_to_new(code.size(), -1);
  bool changed = false;

  size_t i = 0;
  while (i < code.size()) {
    // Pattern: LoadConst X; LoadConst Y; BinaryOp -> LoadConst Z
    // Only if no instruction in the sequence is a jump target
    if (i + 2 < code.size() && is_binary_op(code[i + 2].operation) &&
        !is_target[i] && !is_target[i + 1] && !is_target[i + 2]) {
      auto cv1 = get_const(code[i], fn->constants_);
      auto cv2 = get_const(code[i + 1], fn->constants_);

      if (cv1 && cv2) {
        auto result = eval_binary(code[i + 2].operation, *cv1, *cv2);
        if (result) {
          old_to_new[i] = new_code.size();
          old_to_new[i + 1] = new_code.size();
          old_to_new[i + 2] = new_code.size();
          new_code.push_back(make_load_const(fn, *result));
          i += 3;
          changed = true;
          continue;
        }
      }

      // Strength reduction: X op LoadConst -> simplified
      // Pattern: ...; LoadConst 0; Mul -> Pop; LoadConst 0 (x * 0 = 0)
      if (cv2 && cv2->kind == ConstVal::INT && cv2->i == 0 &&
          code[i + 2].operation == Operation::Mul) {
        old_to_new[i] = new_code.size();
        new_code.push_back(Instruction(Operation::Pop, std::nullopt));
        old_to_new[i + 1] = new_code.size();
        old_to_new[i + 2] = new_code.size();
        new_code.push_back(code[i + 1]); // Keep LoadConst 0
        i += 3;
        changed = true;
        continue;
      }

      // Pattern: ...; LoadConst 1; Mul -> (just keep first operand, x * 1 = x)
      if (cv2 && cv2->kind == ConstVal::INT && cv2->i == 1 &&
          code[i + 2].operation == Operation::Mul) {
        old_to_new[i] = new_code.size();
        new_code.push_back(code[i]); // Keep first operand
        old_to_new[i + 1] = new_code.size() - 1;
        old_to_new[i + 2] = new_code.size() - 1;
        i += 3;
        changed = true;
        continue;
      }

      // Pattern: ...; LoadConst 0; Add -> (just keep first operand, x + 0 = x)
      if (cv2 && cv2->kind == ConstVal::INT && cv2->i == 0 &&
          code[i + 2].operation == Operation::Add) {
        old_to_new[i] = new_code.size();
        new_code.push_back(code[i]);
        old_to_new[i + 1] = new_code.size() - 1;
        old_to_new[i + 2] = new_code.size() - 1;
        i += 3;
        changed = true;
        continue;
      }

      // Pattern: ...; LoadConst 0; Sub -> (just keep first operand, x - 0 = x)
      if (cv2 && cv2->kind == ConstVal::INT && cv2->i == 0 &&
          code[i + 2].operation == Operation::Sub) {
        old_to_new[i] = new_code.size();
        new_code.push_back(code[i]);
        old_to_new[i + 1] = new_code.size() - 1;
        old_to_new[i + 2] = new_code.size() - 1;
        i += 3;
        changed = true;
        continue;
      }

      // Pattern: ...; LoadConst 1; Div -> (just keep first operand, x / 1 = x)
      if (cv2 && cv2->kind == ConstVal::INT && cv2->i == 1 &&
          code[i + 2].operation == Operation::Div) {
        old_to_new[i] = new_code.size();
        new_code.push_back(code[i]);
        old_to_new[i + 1] = new_code.size() - 1;
        old_to_new[i + 2] = new_code.size() - 1;
        i += 3;
        changed = true;
        continue;
      }

      // Strength reduction: LoadConst op X -> simplified
      // Pattern: LoadConst 0; ...; Mul -> Pop; LoadConst 0 (0 * x = 0)
      if (cv1 && cv1->kind == ConstVal::INT && cv1->i == 0 &&
          code[i + 2].operation == Operation::Mul) {
        old_to_new[i] = new_code.size();
        new_code.push_back(code[i]); // Keep LoadConst 0
        old_to_new[i + 1] = new_code.size();
        new_code.push_back(Instruction(Operation::Pop, std::nullopt));
        old_to_new[i + 2] = new_code.size() - 1;
        i += 3;
        changed = true;
        continue;
      }

      // Pattern: LoadConst 1; ...; Mul -> keep second operand (1 * x = x)
      if (cv1 && cv1->kind == ConstVal::INT && cv1->i == 1 &&
          code[i + 2].operation == Operation::Mul) {
        old_to_new[i] = new_code.size();
        old_to_new[i + 1] = new_code.size();
        new_code.push_back(code[i + 1]);
        old_to_new[i + 2] = new_code.size() - 1;
        i += 3;
        changed = true;
        continue;
      }

      // Pattern: LoadConst 0; ...; Add -> keep second operand (0 + x = x)
      if (cv1 && cv1->kind == ConstVal::INT && cv1->i == 0 &&
          code[i + 2].operation == Operation::Add) {
        old_to_new[i] = new_code.size();
        old_to_new[i + 1] = new_code.size();
        new_code.push_back(code[i + 1]);
        old_to_new[i + 2] = new_code.size() - 1;
        i += 3;
        changed = true;
        continue;
      }
    }

    // Pattern: LoadConst X; UnaryOp -> LoadConst Y
    if (i + 1 < code.size() && is_unary_op(code[i + 1].operation) &&
        !is_target[i] && !is_target[i + 1]) {
      auto cv = get_const(code[i], fn->constants_);
      if (cv) {
        auto result = eval_unary(code[i + 1].operation, *cv);
        if (result) {
          old_to_new[i] = new_code.size();
          old_to_new[i + 1] = new_code.size();
          new_code.push_back(make_load_const(fn, *result));
          i += 2;
          changed = true;
          continue;
        }
      }
    }

    // Copy propagation: StoreLocal X; LoadLocal X -> Dup; StoreLocal X
    // This avoids redundant load after store
    if (i + 1 < code.size() &&
        code[i].operation == Operation::StoreLocal &&
        code[i + 1].operation == Operation::LoadLocal &&
        code[i].operand0.value() == code[i + 1].operand0.value() &&
        !is_target[i] && !is_target[i + 1]) {
      old_to_new[i] = new_code.size();
      new_code.push_back(Instruction(Operation::Dup, std::nullopt));
      old_to_new[i + 1] = new_code.size();
      new_code.push_back(code[i]); // StoreLocal X
      i += 2;
      changed = true;
      continue;
    }

    // No pattern matched, copy instruction
    old_to_new[i] = new_code.size();
    new_code.push_back(code[i]);
    ++i;
  }

  if (changed) {
    code = std::move(new_code);
    rebuild_jumps(code, old_to_new);
  }
  return changed;
}

// Perform peephole optimization on a single function
static void peephole_optimize_one(Function *fn) {
  // Run multiple passes until no more changes
  // (some optimizations may enable others)
  int max_passes = 10;
  for (int pass = 0; pass < max_passes; ++pass) {
    if (!peephole_one_pass(fn))
      break;
  }
}

// Public entry point
void peephole_optimize(Function *func) {
  // First recursively process nested functions
  for (Function *child : func->functions_) {
    peephole_optimize(child);
  }

  // Then process this function
  peephole_optimize_one(func);
}

} // namespace bytecode::opt_peephole
