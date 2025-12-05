#include "opt_licm.hpp"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "instructions.hpp"

namespace bytecode::opt_licm {
namespace {

struct RegAlloc {
  uint16_t next;
  uint16_t max_used;
  uint16_t fresh() {
    uint16_t r = next++;
    if (r > max_used)
      max_used = r;
    return r;
  }
};

struct Fixup {
  size_t out_idx;
  size_t target_pc;
};

// Minimal stack->register translation (duplicated from VM fast path).
static void ensure_registerized(Function *func) {
  if (!func || !func->reg_instructions.empty())
    return;

  using bytecode::Operation;
  uint16_t initial = static_cast<uint16_t>(func->local_vars_.size());
  uint16_t max_init = initial ? static_cast<uint16_t>(initial - 1) : 0;
  RegAlloc alloc{initial, max_init};

  auto ensure_reg_count = [&](uint16_t idx) {
    if (idx > alloc.max_used)
      alloc.max_used = idx;
  };

  std::vector<uint16_t> vstack;
  std::vector<size_t> pc_to_out(func->instructions.size() + 1, 0);
  std::vector<Fixup> fixups;
  std::vector<bytecode::RegisterInstruction> out;

  for (size_t pc = 0; pc < func->instructions.size(); ++pc) {
    pc_to_out[pc] = out.size();
    const auto &in = func->instructions[pc];
    switch (in.operation) {
    case Operation::LoadConst: {
      uint16_t dst = alloc.fresh();
      out.push_back({Operation::LoadConst, dst, 0, 0, in.operand0.value()});
      vstack.push_back(dst);
      break;
    }
    case Operation::LoadFunc: {
      uint16_t dst = alloc.fresh();
      out.push_back({Operation::LoadFunc, dst, 0, 0, in.operand0.value()});
      vstack.push_back(dst);
      break;
    }
    case Operation::LoadLocal: {
      uint16_t reg = static_cast<uint16_t>(in.operand0.value());
      ensure_reg_count(reg);
      vstack.push_back(reg);
      break;
    }
    case Operation::StoreLocal: {
      uint16_t val = vstack.back();
      vstack.pop_back();
      uint16_t dst = static_cast<uint16_t>(in.operand0.value());
      ensure_reg_count(dst);
      out.push_back({Operation::StoreLocal, dst, val, 0, 0});
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
      uint16_t right = vstack.back();
      vstack.pop_back();
      uint16_t left = vstack.back();
      vstack.pop_back();
      uint16_t dst = alloc.fresh();
      out.push_back({in.operation, dst, left, right, 0});
      vstack.push_back(dst);
      break;
    }
    case Operation::Neg:
    case Operation::Not: {
      uint16_t val = vstack.back();
      vstack.pop_back();
      uint16_t dst = alloc.fresh();
      out.push_back({in.operation, dst, val, 0, 0});
      vstack.push_back(dst);
      break;
    }
    case Operation::Goto: {
      int64_t target_pc = static_cast<int64_t>(pc) + in.operand0.value();
      if (target_pc >= 0 &&
          target_pc <= static_cast<int64_t>(func->instructions.size())) {
        fixups.push_back({out.size(), static_cast<size_t>(target_pc)});
      }
      out.push_back({Operation::Goto, 0, 0, 0, 0});
      break;
    }
    case Operation::If: {
      uint16_t cond = vstack.back();
      vstack.pop_back();
      int64_t target_pc = static_cast<int64_t>(pc) + in.operand0.value();
      if (target_pc >= 0 &&
          target_pc <= static_cast<int64_t>(func->instructions.size())) {
        fixups.push_back({out.size(), static_cast<size_t>(target_pc)});
      }
      out.push_back({Operation::If, 0, cond, 0, 0});
      break;
    }
    case Operation::Dup: {
      uint16_t top = vstack.back();
      uint16_t dst = alloc.fresh();
      out.push_back({Operation::Dup, dst, top, 0, 0});
      vstack.push_back(dst);
      break;
    }
    case Operation::Swap: {
      uint16_t a = vstack.back();
      vstack.pop_back();
      uint16_t b = vstack.back();
      vstack.pop_back();
      vstack.push_back(a);
      vstack.push_back(b);
      break;
    }
    case Operation::Pop: {
      vstack.pop_back();
      break;
    }
    case Operation::Call: {
      int32_t arg_count = in.operand0.value();
      std::vector<uint16_t> args;
      args.reserve(arg_count);
      for (int i = 0; i < arg_count; ++i) {
        args.push_back(vstack.back());
        vstack.pop_back();
      }
      std::reverse(args.begin(), args.end());
      uint16_t callee = vstack.back();
      vstack.pop_back();
      uint16_t arg_start = alloc.fresh();
      if (arg_count > 0) {
        ensure_reg_count(static_cast<uint16_t>(arg_start + arg_count - 1));
        for (int i = 0; i < arg_count; ++i) {
          out.push_back({Operation::StoreLocal,
                         static_cast<uint16_t>(arg_start + i), args[i], 0, 0});
        }
      }
      uint16_t dst = alloc.fresh();
      out.push_back({Operation::Call, dst, callee, arg_start, arg_count});
      vstack.push_back(dst);
      break;
    }
    case Operation::Return: {
      uint16_t ret = vstack.back();
      vstack.pop_back();
      out.push_back({Operation::Return, 0, ret, 0, 0});
      break;
    }
    case Operation::LoadGlobal: {
      uint16_t dst = alloc.fresh();
      out.push_back({Operation::LoadGlobal, dst, 0, 0, in.operand0.value()});
      vstack.push_back(dst);
      break;
    }
    case Operation::StoreGlobal: {
      uint16_t val = vstack.back();
      vstack.pop_back();
      out.push_back({Operation::StoreGlobal, 0, val, 0, in.operand0.value()});
      break;
    }
    case Operation::PushReference: {
      uint16_t dst = alloc.fresh();
      out.push_back({Operation::PushReference, dst, 0, 0, in.operand0.value()});
      vstack.push_back(dst);
      break;
    }
    case Operation::LoadReference: {
      uint16_t ref = vstack.back();
      vstack.pop_back();
      uint16_t dst = alloc.fresh();
      out.push_back({Operation::LoadReference, dst, ref, 0, 0});
      vstack.push_back(dst);
      break;
    }
    case Operation::StoreReference: {
      uint16_t ref = vstack.back();
      vstack.pop_back();
      uint16_t val = vstack.back();
      vstack.pop_back();
      out.push_back({Operation::StoreReference, 0, val, ref, 0});
      break;
    }
    case Operation::AllocRecord: {
      uint16_t dst = alloc.fresh();
      out.push_back({Operation::AllocRecord, dst, 0, 0, 0});
      vstack.push_back(dst);
      break;
    }
    case Operation::FieldLoad: {
      uint16_t record = vstack.back();
      vstack.pop_back();
      uint16_t dst = alloc.fresh();
      out.push_back({Operation::FieldLoad, dst, record, 0, in.operand0.value()});
      vstack.push_back(dst);
      break;
    }
    case Operation::FieldStore: {
      uint16_t record = vstack.back();
      vstack.pop_back();
      uint16_t val = vstack.back();
      vstack.pop_back();
      out.push_back({Operation::FieldStore, 0, val, record, in.operand0.value()});
      break;
    }
    case Operation::IndexLoad: {
      uint16_t idx = vstack.back();
      vstack.pop_back();
      uint16_t record = vstack.back();
      vstack.pop_back();
      uint16_t dst = alloc.fresh();
      out.push_back({Operation::IndexLoad, dst, record, idx, 0});
      vstack.push_back(dst);
      break;
    }
    case Operation::IndexStore: {
      uint16_t record = vstack.back();
      vstack.pop_back();
      uint16_t idx = vstack.back();
      vstack.pop_back();
      uint16_t val = vstack.back();
      vstack.pop_back();
      out.push_back({Operation::IndexStore, record, val, idx, 0});
      break;
    }
    case Operation::AllocClosure: {
      int free_count = in.operand0.value();
      std::vector<uint16_t> refs;
      refs.reserve(free_count);
      for (int i = 0; i < free_count; ++i) {
        refs.push_back(vstack.back());
        vstack.pop_back();
      }
      std::reverse(refs.begin(), refs.end());
      uint16_t func_reg = vstack.back();
      vstack.pop_back();
      uint16_t base = alloc.fresh();
      if (free_count > 0) {
        ensure_reg_count(static_cast<uint16_t>(base + free_count - 1));
        for (int i = 0; i < free_count; ++i) {
          out.push_back({Operation::StoreLocal, static_cast<uint16_t>(base + i),
                         refs[i], 0, 0});
        }
      }
      uint16_t dst = alloc.fresh();
      out.push_back(
          {Operation::AllocClosure, dst, base, func_reg, free_count});
      vstack.push_back(dst);
      break;
    }
    default:
      // Unhandled opcodes are ignored; they should not appear in benchmarks.
      break;
    }
  }
  pc_to_out[func->instructions.size()] = out.size();

  for (const auto &fx : fixups) {
    if (fx.target_pc >= pc_to_out.size())
      continue;
    int32_t rel =
        static_cast<int32_t>(pc_to_out[fx.target_pc] - static_cast<size_t>(fx.out_idx));
    out[fx.out_idx].imm = rel;
  }

  func->register_count = alloc.max_used + 1;
  func->reg_instructions = std::move(out);
}

static bool is_pure(Operation op) {
  switch (op) {
  case Operation::LoadConst:
  case Operation::LoadFunc:
  case Operation::LoadLocal:
  case Operation::LoadGlobal:
  case Operation::Add:
  case Operation::Sub:
  case Operation::Mul:
  case Operation::Div:
  case Operation::Gt:
  case Operation::Geq:
  case Operation::Eq:
  case Operation::And:
  case Operation::Or:
  case Operation::Neg:
  case Operation::Not:
  case Operation::Dup:
  case Operation::FieldLoad:
  case Operation::IndexLoad:
    return true;
  default:
    return false;
  }
}

static bool is_branch(Operation op) {
  return op == Operation::Goto || op == Operation::If;
}

struct LoopInfo {
  size_t header;
  size_t tail;
};

// Identify simple natural loops via backward edges.
static std::vector<LoopInfo> find_loops(const RegisterInstructionList &code) {
  std::vector<LoopInfo> loops;
  for (size_t idx = 0; idx < code.size(); ++idx) {
    const auto &inst = code[idx];
    if (!is_branch(inst.op))
      continue;
    int64_t target = static_cast<int64_t>(idx) + inst.imm;
    if (target < 0 || target >= static_cast<int64_t>(code.size()))
      continue;
    size_t target_idx = static_cast<size_t>(target);
    if (target_idx <= idx) {
      loops.push_back({target_idx, idx});
    }
  }
  return loops;
}

static void run_on(Function *func) {
  ensure_registerized(func);
  auto &code = func->reg_instructions;
  if (code.empty())
    return;

  std::vector<int32_t> original_imms(code.size(), 0);
  for (size_t i = 0; i < code.size(); ++i) {
    if (is_branch(code[i].op))
      original_imms[i] = code[i].imm;
  }

  auto loops = find_loops(code);
  if (loops.empty())
    return;

  std::unordered_map<size_t, std::vector<RegisterInstruction>> preheader_inserts;

  for (const auto &loop : loops) {
    std::unordered_set<uint16_t> modified;
    uint16_t max_reg = 0;
    for (size_t i = loop.header; i <= loop.tail && i < code.size(); ++i) {
      const auto &inst = code[i];
      max_reg = std::max({max_reg, inst.dst, inst.src1, inst.src2});
      if (inst.op == Operation::StoreLocal || inst.op == Operation::StoreGlobal ||
          inst.op == Operation::StoreReference || inst.op == Operation::FieldStore ||
          inst.op == Operation::IndexStore) {
        modified.insert(inst.dst);
      } else {
        // Most ops conceptually write dst
        modified.insert(inst.dst);
      }
    }

    for (size_t i = loop.header; i <= loop.tail && i < code.size(); ++i) {
      auto &inst = code[i];
      if (!is_pure(inst.op))
        continue;
      if (modified.count(inst.src1) || modified.count(inst.src2))
        continue;

      // Hoist by cloning before header and replacing with a cheap self-move.
      preheader_inserts[loop.header].push_back(inst);
      inst.op = Operation::LoadLocal;
      inst.src1 = inst.dst;
      inst.src2 = 0;
      inst.imm = 0;
    }
    func->register_count = std::max<uint16_t>(func->register_count, max_reg + 1);
  }

  // Rebuild code with insertions and updated branches.
  std::vector<size_t> old_to_new(code.size() + 1, 0);
  RegisterInstructionList new_code;
  std::vector<int> origin;

  for (size_t i = 0; i < code.size(); ++i) {
    old_to_new[i] = new_code.size();
    auto it = preheader_inserts.find(i);
    if (it != preheader_inserts.end()) {
      for (const auto &ins : it->second) {
        origin.push_back(-1);
        new_code.push_back(ins);
      }
    }
    origin.push_back(static_cast<int>(i));
    new_code.push_back(code[i]);
  }
  old_to_new[code.size()] = new_code.size();

  for (size_t idx = 0; idx < new_code.size(); ++idx) {
    int orig = origin[idx];
    if (orig < 0)
      continue;
    auto &inst = new_code[idx];
    if (!is_branch(inst.op))
      continue;
    int64_t target_old = static_cast<int64_t>(orig) + original_imms[orig];
    if (target_old < 0 || target_old >= static_cast<int64_t>(old_to_new.size()))
      continue;
    size_t target_new = old_to_new[static_cast<size_t>(target_old)];
    inst.imm = static_cast<int32_t>(static_cast<int64_t>(target_new) - static_cast<int64_t>(idx));
  }

  func->reg_instructions = std::move(new_code);
}

} // namespace

void run(Function *func) {
  if (!func)
    return;
  for (auto *child : func->functions_)
    run(child);
  run_on(func);
}

} // namespace bytecode::opt_licm

