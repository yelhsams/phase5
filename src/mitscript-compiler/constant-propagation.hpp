#pragma once

#include "cfg.hpp"
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>
#include <cassert>

namespace mitscript::analysis {

/**
 * Constant-propagation lattice:
 *   - Bottom      : unreachable / no info
 *   - Constant<T> : known literal (int/bool/string/None)
 *   - Top         : unknown / varying
 */
struct CPValue {
    enum class Kind { Bottom, ConstInt, ConstBool, ConstString, ConstNone, Top } kind;
    std::optional<int> int_val;
    std::optional<bool> bool_val;
    std::optional<std::string> str_val;

    static CPValue bottom() { return {Kind::Bottom, std::nullopt, std::nullopt, std::nullopt}; }
    static CPValue top()    { return {Kind::Top,    std::nullopt, std::nullopt, std::nullopt}; }
    static CPValue none()   { return {Kind::ConstNone, std::nullopt, std::nullopt, std::nullopt}; }
    static CPValue cint(int v)   { return {Kind::ConstInt, v, std::nullopt, std::nullopt}; }
    static CPValue cbool(bool v) { return {Kind::ConstBool, std::nullopt, v, std::nullopt}; }
    static CPValue cstr(std::string v) {
        return {Kind::ConstString, std::nullopt, std::nullopt, std::move(v)};
    }
};

inline bool operator==(const CPValue& a, const CPValue& b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case CPValue::Kind::ConstInt:    return a.int_val  == b.int_val;
        case CPValue::Kind::ConstBool:   return a.bool_val == b.bool_val;
        case CPValue::Kind::ConstString: return a.str_val  == b.str_val;
        default: return true;
    }
}

inline bool operator!=(const CPValue& a, const CPValue& b) { return !(a == b); }

// Lattice meet (∧) for forward constant propagation.
inline CPValue meet(const CPValue& a, const CPValue& b) {
    if (a.kind == CPValue::Kind::Bottom) return b;
    if (b.kind == CPValue::Kind::Bottom) return a;
    if (a.kind == CPValue::Kind::Top || b.kind == CPValue::Kind::Top) return CPValue::top();
    if (a == b) return a;
    return CPValue::top();
}

struct CPState {
    std::vector<CPValue> regs;  // one lattice value per VReg
};

// ---------- Helpers to interpret IR ops in the lattice ----------

inline CPValue eval_unary(CFG::IROp op, const CPValue& a) {
    if (a.kind == CPValue::Kind::Bottom || a.kind == CPValue::Kind::Top) {
        return CPValue::top();
    }
    switch (op) {
        case CFG::IROp::Neg:
            if (a.kind == CPValue::Kind::ConstInt) {
                return CPValue::cint(-*a.int_val);
            }
            break;
        case CFG::IROp::Not:
            if (a.kind == CPValue::Kind::ConstBool) {
                return CPValue::cbool(!*a.bool_val);
            }
            break;
        default:
            break;
    }
    return CPValue::top();
}

inline CPValue eval_binary(CFG::IROp op, const CPValue& a, const CPValue& b) {
    if (a.kind == CPValue::Kind::Bottom || b.kind == CPValue::Kind::Bottom ||
        a.kind == CPValue::Kind::Top    || b.kind == CPValue::Kind::Top) {
        return CPValue::top();
    }

    if (a.kind == CPValue::Kind::ConstInt && b.kind == CPValue::Kind::ConstInt) {
        int av = *a.int_val;
        int bv = *b.int_val;
        switch (op) {
            case CFG::IROp::Add:   return CPValue::cint(av + bv);
            case CFG::IROp::Sub:   return CPValue::cint(av - bv);
            case CFG::IROp::Mul:   return CPValue::cint(av * bv);
            case CFG::IROp::Div:   return (bv != 0) ? CPValue::cint(av / bv) : CPValue::top();

            case CFG::IROp::CmpEq: return CPValue::cbool(av == bv);
            case CFG::IROp::CmpLt: return CPValue::cbool(av <  bv);
            case CFG::IROp::CmpGt: return CPValue::cbool(av >  bv);
            case CFG::IROp::CmpLe: return CPValue::cbool(av <= bv);
            case CFG::IROp::CmpGe: return CPValue::cbool(av >= bv);
            default: break;
        }
    }

    if (a.kind == CPValue::Kind::ConstBool && b.kind == CPValue::Kind::ConstBool) {
        bool av = *a.bool_val;
        bool bv = *b.bool_val;
        switch (op) {
            case CFG::IROp::And:   return CPValue::cbool(av && bv);
            case CFG::IROp::Or:    return CPValue::cbool(av || bv);
            case CFG::IROp::CmpEq: return CPValue::cbool(av == bv);
            default: break;
        }
    }

    return CPValue::top();
}

class ConstantPropagation {
public:
    explicit ConstantPropagation(mitscript::CFG::FunctionCFG& fn) : fn_(fn) {}

    void run() {
        size_t reg_count = count_vregs(fn_);
        states_.clear();
        states_.resize(fn_.blocks.size());
        for (auto& st : states_) {
            st.in.regs.assign(reg_count, CPValue::bottom());
            st.out.regs.assign(reg_count, CPValue::bottom());
        }

        if (fn_.blocks.empty()) return;

        std::queue<CFG::BlockId> worklist;
        worklist.push(fn_.entry);

        while (!worklist.empty()) {
            CFG::BlockId bid = worklist.front();
            worklist.pop();
            if (bid < 0 || bid >= (CFG::BlockId)fn_.blocks.size()) continue;
            if (!fn_.blocks[bid]) continue;

            CPState in  = join_predecessors(bid);
            CPState out = transfer(bid, in);

            if (out.regs != states_[bid].out.regs) {
                states_[bid].in  = std::move(in);
                states_[bid].out = std::move(out);
                for (auto succ : fn_.blocks[bid]->successors) {
                    worklist.push(succ);
                }
            }
        }
    }

    const CPState& in_state(CFG::BlockId b)  const { return states_[b].in; }
    const CPState& out_state(CFG::BlockId b) const { return states_[b].out; }

    // SAFE rewrite: only simplify CondJump terminators when condition is constant.
    void rewrite() {
        if (states_.empty()) run();

        for (size_t bid = 0; bid < fn_.blocks.size(); ++bid) {
            if (!fn_.blocks[bid]) continue;
            auto& blk = *fn_.blocks[bid];

            if (blk.term.kind != CFG::Terminator::Kind::CondJump) continue;

            const auto& out = states_[bid].out;
            int cond_v = blk.term.condition;
            if (cond_v < 0 || cond_v >= (int)out.regs.size()) continue;

            const CPValue& c = out.regs[cond_v];
            if (c.kind != CPValue::Kind::ConstBool || !c.bool_val.has_value())
                continue;

            bool take_true = *c.bool_val;
            CFG::BlockId tgt = take_true ? blk.term.trueTarget : blk.term.falseTarget;

            blk.term.kind   = CFG::Terminator::Kind::Jump;
            blk.term.target = tgt;
            blk.term.trueTarget  = -1;
            blk.term.falseTarget = -1;

            blk.successors.clear();
            if (tgt >= 0) blk.successors.push_back(tgt);
        }
    }

private:
    struct BlockState {
        CPState in;
        CPState out;
    };

    mitscript::CFG::FunctionCFG& fn_;
    std::vector<BlockState> states_;

    static size_t count_vregs(const mitscript::CFG::FunctionCFG& fn) {
        int max_v = -1;
        for (const auto& blk_ptr : fn.blocks) {
            if (!blk_ptr) continue;
            for (const auto& ir : blk_ptr->code) {
                if (ir.output && ir.output->kind == CFG::IROperand::VREG)
                    max_v = std::max(max_v, ir.output->i);
                for (const auto& inp : ir.inputs) {
                    if (inp.kind == CFG::IROperand::VREG)
                        max_v = std::max(max_v, inp.i);
                }
            }
        }
        return (max_v < 0) ? 0u : static_cast<size_t>(max_v + 1);
    }

    CPState join_predecessors(CFG::BlockId b) const {
        CPState result;
        if (b < 0 || b >= (CFG::BlockId)states_.size()) return result;
        const auto& preds = fn_.blocks[b]->predecessors;
        if (preds.empty()) {
            // entry / no-pred block: its in-state is whatever we already have
            return states_[b].in;
        }

        result = states_[preds.front()].out;
        for (size_t i = 1; i < preds.size(); ++i) {
            const auto& other = states_[preds[i]].out;
            for (size_t r = 0; r < result.regs.size(); ++r) {
                result.regs[r] = meet(result.regs[r], other.regs[r]);
            }
        }
        return result;
    }

    CPState transfer(CFG::BlockId b, const CPState& in) const {
        CPState out = in;
        const auto& blk = *fn_.blocks[b];

        auto read = [&](const CFG::IROperand& op) -> CPValue {
            switch (op.kind) {
                case CFG::IROperand::VREG:
                    if (op.i >= 0 && op.i < (int)out.regs.size())
                        return out.regs[op.i];
                    return CPValue::top();
                case CFG::IROperand::CONSTI: return CPValue::cint(op.i);
                case CFG::IROperand::CONSTB: return CPValue::cbool(op.i != 0);
                case CFG::IROperand::CONSTS: return CPValue::cstr(op.s);
                case CFG::IROperand::NONE:   return CPValue::none();
                default:                     return CPValue::top();
            }
        };

        for (const auto& ir : blk.code) {
            switch (ir.op) {
                case CFG::IROp::LoadConst: {
                    if (ir.output && ir.inputs.size() == 1) {
                        out.regs[ir.output->i] = read(ir.inputs[0]);
                    }
                    break;
                }

                case CFG::IROp::Add:
                case CFG::IROp::Sub:
                case CFG::IROp::Mul:
                case CFG::IROp::Div:
                case CFG::IROp::CmpEq:
                case CFG::IROp::CmpLt:
                case CFG::IROp::CmpGt:
                case CFG::IROp::CmpLe:
                case CFG::IROp::CmpGe:
                case CFG::IROp::And:
                case CFG::IROp::Or: {
                    if (ir.output && ir.output->kind == CFG::IROperand::VREG &&
                        ir.inputs.size() == 2) {
                        CPValue a = read(ir.inputs[0]);
                        CPValue b = read(ir.inputs[1]);
                        out.regs[ir.output->i] = eval_binary(ir.op, a, b);
                    } else if (ir.output && ir.output->kind == CFG::IROperand::VREG) {
                        out.regs[ir.output->i] = CPValue::top();
                    }
                    break;
                }

                case CFG::IROp::Neg:
                case CFG::IROp::Not: {
                    if (ir.output && ir.output->kind == CFG::IROperand::VREG &&
                        ir.inputs.size() == 1) {
                        CPValue a = read(ir.inputs[0]);
                        out.regs[ir.output->i] = eval_unary(ir.op, a);
                    } else if (ir.output && ir.output->kind == CFG::IROperand::VREG) {
                        out.regs[ir.output->i] = CPValue::top();
                    }
                    break;
                }

                case CFG::IROp::LoadLocal:
                case CFG::IROp::LoadGlobal:
                case CFG::IROp::Call:
                case CFG::IROp::LoadField:
                case CFG::IROp::LoadIndex:
                case CFG::IROp::MakeRecord:
                case CFG::IROp::AllocClosure: {
                    // define new, unknown value in vreg
                    if (ir.output && ir.output->kind == CFG::IROperand::VREG) {
                        out.regs[ir.output->i] = CPValue::top();
                    }
                    break;
                }

                case CFG::IROp::StoreLocal:
                case CFG::IROp::StoreGlobal:
                case CFG::IROp::StoreField:
                case CFG::IROp::StoreIndex:
                case CFG::IROp::Pop:
                case CFG::IROp::Dup:
                case CFG::IROp::CondJump:
                case CFG::IROp::Jump:
                case CFG::IROp::Return:
                    // no vreg defined; nothing to update
                    break;

                default:
                    // unknown op that defines a vreg → kill constness
                    if (ir.output && ir.output->kind == CFG::IROperand::VREG) {
                        out.regs[ir.output->i] = CPValue::top();
                    }
                    break;
            }
        }
        return out;
    }
};

inline IROperand cpvalue_to_operand(const CPValue& v) {
    switch (v.kind) {
        case CPValue::Kind::ConstInt:    return IROperand(IROperand::CONSTI, *v.int_val);
        case CPValue::Kind::ConstBool:   return IROperand(IROperand::CONSTB, *v.bool_val ? 1 : 0);
        case CPValue::Kind::ConstString: return IROperand(IROperand::CONSTS, 0, *v.str_val);
        case CPValue::Kind::ConstNone:   return IROperand(IROperand::NONE);
        default:                         return IROperand(IROperand::NONE);
    }
}

inline bool is_constant(const CPValue& v) {
    return v.kind == CPValue::Kind::ConstInt ||
           v.kind == CPValue::Kind::ConstBool ||
           v.kind == CPValue::Kind::ConstString ||
           v.kind == CPValue::Kind::ConstNone;
}

// Run constant propagation and fold instructions/branches in-place.
inline void run_constant_folding(mitscript::CFG::FunctionCFG& fn) {
    ConstantPropagation cp(fn);
    cp.run();
    cp.rewrite(); // simplify branches first

    using namespace mitscript::CFG;
    for (size_t bid = 0; bid < fn.blocks.size(); ++bid) {
        if (!fn.blocks[bid]) continue;
        auto& blk = *fn.blocks[bid];

        CPState state = cp.in_state(static_cast<BlockId>(bid));

        auto read = [&](const IROperand& op) -> CPValue {
            switch (op.kind) {
                case IROperand::VREG:
                    if (op.i >= 0 && op.i < (int)state.regs.size()) return state.regs[op.i];
                    return CPValue::top();
                case IROperand::CONSTI: return CPValue::cint(op.i);
                case IROperand::CONSTB: return CPValue::cbool(op.i != 0);
                case IROperand::CONSTS: return CPValue::cstr(op.s);
                case IROperand::NONE:   return CPValue::none();
                default:                return CPValue::top();
            }
        };

        for (auto& ir : blk.code) {
            auto set_dst = [&](const CPValue& v) {
                if (ir.output && ir.output->kind == IROperand::VREG &&
                    ir.output->i >= 0 && ir.output->i < (int)state.regs.size()) {
                    state.regs[ir.output->i] = v;
                }
            };

            switch (ir.op) {
                case IROp::LoadConst: {
                    if (ir.output && !ir.inputs.empty()) {
                        set_dst(read(ir.inputs[0]));
                    }
                    break;
                }

                case IROp::Add: case IROp::Sub: case IROp::Mul: case IROp::Div:
                case IROp::CmpEq: case IROp::CmpLt: case IROp::CmpGt:
                case IROp::CmpLe: case IROp::CmpGe: case IROp::And: case IROp::Or: {
                    if (ir.output && ir.output->kind == IROperand::VREG && ir.inputs.size() == 2) {
                        CPValue a = read(ir.inputs[0]);
                        CPValue b = read(ir.inputs[1]);
                        CPValue res = eval_binary(ir.op, a, b);
                        if (is_constant(res)) {
                            ir.op = IROp::LoadConst;
                            ir.inputs.clear();
                            ir.inputs.push_back(cpvalue_to_operand(res));
                        } else {
                            // Strength reduction optimizations
                            bool a_is_int = a.kind == CPValue::Kind::ConstInt;
                            bool b_is_int = b.kind == CPValue::Kind::ConstInt;

                            if (ir.op == IROp::Mul) {
                                // x * 0 = 0, 0 * x = 0
                                if ((a_is_int && *a.int_val == 0) || (b_is_int && *b.int_val == 0)) {
                                    ir.op = IROp::LoadConst;
                                    ir.inputs.clear();
                                    ir.inputs.push_back(IROperand(IROperand::CONSTI, 0));
                                }
                                // x * 1 = x: convert to Add x + 0
                                else if (b_is_int && *b.int_val == 1) {
                                    ir.op = IROp::Add;
                                    ir.inputs[1] = IROperand(IROperand::CONSTI, 0);
                                }
                                // 1 * x = x: convert to Add 0 + x
                                else if (a_is_int && *a.int_val == 1) {
                                    ir.op = IROp::Add;
                                    ir.inputs[0] = IROperand(IROperand::CONSTI, 0);
                                }
                                // x * 2 = x + x (strength reduction)
                                else if (b_is_int && *b.int_val == 2) {
                                    ir.op = IROp::Add;
                                    ir.inputs[1] = ir.inputs[0]; // x + x
                                }
                                // 2 * x = x + x
                                else if (a_is_int && *a.int_val == 2) {
                                    ir.op = IROp::Add;
                                    ir.inputs[0] = ir.inputs[1];
                                    // ir.inputs[1] already holds x
                                }
                            }
                            else if (ir.op == IROp::Add) {
                                // x + 0 = x: keep as Add for simplicity, bytecode handles this
                                // 0 + x = x: swap operands so x is first
                                if (a_is_int && *a.int_val == 0) {
                                    std::swap(ir.inputs[0], ir.inputs[1]);
                                }
                            }
                            else if (ir.op == IROp::Div) {
                                // x / 1 = x: convert to Add x + 0
                                if (b_is_int && *b.int_val == 1) {
                                    ir.op = IROp::Add;
                                    ir.inputs[1] = IROperand(IROperand::CONSTI, 0);
                                }
                            }
                        }
                        set_dst(res);
                    } else {
                        set_dst(CPValue::top());
                    }
                    break;
                }

                case IROp::Neg:
                case IROp::Not: {
                    if (ir.output && ir.output->kind == IROperand::VREG && ir.inputs.size() == 1) {
                        CPValue a = read(ir.inputs[0]);
                        CPValue res = eval_unary(ir.op, a);
                        if (is_constant(res)) {
                            ir.op = IROp::LoadConst;
                            ir.inputs.clear();
                            ir.inputs.push_back(cpvalue_to_operand(res));
                        }
                        set_dst(res);
                    } else {
                        set_dst(CPValue::top());
                    }
                    break;
                }

                case IROp::LoadLocal:
                case IROp::LoadGlobal:
                case IROp::Call:
                case IROp::LoadField:
                case IROp::LoadIndex:
                case IROp::MakeRecord:
                case IROp::AllocClosure: {
                    if (ir.output && ir.output->kind == IROperand::VREG) {
                        set_dst(CPValue::top());
                    }
                    break;
                }

                default: {
                    if (ir.output && ir.output->kind == IROperand::VREG) {
                        set_dst(CPValue::top());
                    }
                    break;
                }
            }
        }
    }
}

} // namespace mitscript::analysis
