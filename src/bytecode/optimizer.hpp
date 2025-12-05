#pragma once

#include "types.hpp"
#include "instructions.hpp"
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <optional>

namespace bytecode::optimizer {

/**
 * Bytecode Optimizer
 *
 * Implements multiple optimization passes:
 * 1. Dead Code Elimination - removes instructions whose results are never used
 * 2. Unreachable Code Elimination - removes code after returns and dead branches
 * 3. Constant Folding - evaluates constant expressions at compile time
 * 4. Peephole Optimizations - simplifies common instruction patterns
 * 5. Jump Threading - optimizes chains of jumps
 * 6. Redundant Load/Store Elimination - removes unnecessary memory operations
 */

// Forward declaration
class BytecodeOptimizer;

//-----------------------------------------------------------------------------
// Helper: Constant value representation for compile-time evaluation
//-----------------------------------------------------------------------------
struct ConstValue {
    enum class Kind { None, Int, Bool, String, Unknown } kind = Kind::Unknown;
    int32_t int_val = 0;
    bool bool_val = false;
    std::string str_val;

    static ConstValue unknown() { return {Kind::Unknown}; }
    static ConstValue none() { return {Kind::None}; }
    static ConstValue from_int(int32_t v) { ConstValue c; c.kind = Kind::Int; c.int_val = v; return c; }
    static ConstValue from_bool(bool v) { ConstValue c; c.kind = Kind::Bool; c.bool_val = v; return c; }
    static ConstValue from_string(const std::string& v) { ConstValue c; c.kind = Kind::String; c.str_val = v; return c; }

    bool is_known() const { return kind != Kind::Unknown; }
    bool is_int() const { return kind == Kind::Int; }
    bool is_bool() const { return kind == Kind::Bool; }
    bool is_string() const { return kind == Kind::String; }
};

//-----------------------------------------------------------------------------
// Main Optimizer Class
//-----------------------------------------------------------------------------
class BytecodeOptimizer {
public:
    explicit BytecodeOptimizer(Function* func) : func_(func) {}

    // Run all optimization passes
    void optimize() {
        // Multiple passes for better optimization
        for (int pass = 0; pass < 4; ++pass) {
            bool changed = false;
            changed |= eliminate_unreachable_code();
            changed |= algebraic_simplify();
            changed |= strength_reduce();
            changed |= fold_constants();
            changed |= peephole_optimize();
            changed |= eliminate_dead_stores();
            changed |= optimize_jumps();
            if (!changed) break;
        }

        // Final cleanup
        remove_nops();

        // Recursively optimize nested functions
        for (Function* child : func_->functions_) {
            BytecodeOptimizer child_opt(child);
            child_opt.optimize();
        }
    }

private:
    Function* func_;

    //-------------------------------------------------------------------------
    // Pass 1: Unreachable Code Elimination
    //-------------------------------------------------------------------------
    bool eliminate_unreachable_code() {
        auto& code = func_->instructions;
        if (code.empty()) return false;

        bool changed = false;

        // Find all reachable instructions using control flow analysis
        std::unordered_set<size_t> reachable;
        std::vector<size_t> worklist;
        worklist.push_back(0);

        while (!worklist.empty()) {
            size_t pc = worklist.back();
            worklist.pop_back();

            if (pc >= code.size()) continue;
            if (reachable.count(pc)) continue;
            reachable.insert(pc);

            const auto& inst = code[pc];

            switch (inst.operation) {
                case Operation::Return:
                    // Return terminates this path
                    break;

                case Operation::Goto: {
                    // Unconditional jump
                    int32_t offset = inst.operand0.value_or(1);
                    int64_t target = static_cast<int64_t>(pc) + offset;
                    if (target >= 0 && target < static_cast<int64_t>(code.size())) {
                        worklist.push_back(static_cast<size_t>(target));
                    }
                    break;
                }

                case Operation::If: {
                    // Conditional jump - both paths are potentially reachable
                    int32_t offset = inst.operand0.value_or(1);
                    int64_t target = static_cast<int64_t>(pc) + offset;
                    if (target >= 0 && target < static_cast<int64_t>(code.size())) {
                        worklist.push_back(static_cast<size_t>(target));
                    }
                    // Fall-through is also reachable
                    worklist.push_back(pc + 1);
                    break;
                }

                default:
                    // Normal instruction - next instruction is reachable
                    worklist.push_back(pc + 1);
                    break;
            }
        }

        // Mark unreachable instructions as NOPs (Pop with no effect)
        for (size_t i = 0; i < code.size(); ++i) {
            if (!reachable.count(i) && code[i].operation != Operation::Pop) {
                // Replace with a NOP-like instruction (we'll remove these later)
                code[i] = Instruction(Operation::Pop, std::nullopt);
                changed = true;
            }
        }

        return changed;
    }

    //-------------------------------------------------------------------------
    // Pass 2: Algebraic Simplifications
    // Optimizes patterns like x+0, x*1, x*0, x-0, x/1, true&&x, false||x, etc.
    //-------------------------------------------------------------------------
    bool algebraic_simplify() {
        auto& code = func_->instructions;
        if (code.size() < 2) return false;

        bool changed = false;

        // Look for patterns: LoadConst followed by binary operation
        for (size_t i = 0; i + 1 < code.size(); ++i) {
            // Pattern: LoadConst 0, Add -> remove both (identity: x + 0 = x)
            // Pattern: LoadConst 0, Sub (right) -> remove both (identity: x - 0 = x)
            // Pattern: LoadConst 1, Mul -> remove both (identity: x * 1 = x)
            // Pattern: LoadConst 1, Div -> remove both (identity: x / 1 = x)
            // Pattern: LoadConst 0, Mul -> replace x with 0 (x * 0 = 0)

            if (code[i].operation == Operation::LoadConst) {
                int32_t const_idx = code[i].operand0.value_or(-1);
                if (const_idx < 0 || static_cast<size_t>(const_idx) >= func_->constants_.size())
                    continue;

                Constant* c = func_->constants_[const_idx];

                // Check for integer patterns
                if (auto* ic = dynamic_cast<Constant::Integer*>(c)) {
                    int32_t val = ic->value;

                    // x + 0 = x (0 is on top of stack, x below)
                    if (val == 0 && code[i+1].operation == Operation::Add) {
                        code[i] = Instruction(Operation::Goto, 1);
                        code[i+1] = Instruction(Operation::Goto, 1);
                        changed = true;
                        continue;
                    }

                    // x - 0 = x
                    if (val == 0 && code[i+1].operation == Operation::Sub) {
                        code[i] = Instruction(Operation::Goto, 1);
                        code[i+1] = Instruction(Operation::Goto, 1);
                        changed = true;
                        continue;
                    }

                    // x * 1 = x
                    if (val == 1 && code[i+1].operation == Operation::Mul) {
                        code[i] = Instruction(Operation::Goto, 1);
                        code[i+1] = Instruction(Operation::Goto, 1);
                        changed = true;
                        continue;
                    }

                    // x / 1 = x
                    if (val == 1 && code[i+1].operation == Operation::Div) {
                        code[i] = Instruction(Operation::Goto, 1);
                        code[i+1] = Instruction(Operation::Goto, 1);
                        changed = true;
                        continue;
                    }

                    // x * 0 = 0: Replace entire computation with 0
                    // This is trickier - we need to pop x and push 0
                    if (val == 0 && code[i+1].operation == Operation::Mul) {
                        // Convert: [x on stack] LoadConst 0, Mul
                        // To: Pop, LoadConst 0
                        code[i] = Instruction(Operation::Pop, std::nullopt);
                        int32_t zero_idx = find_or_add_int_constant(0);
                        code[i+1] = Instruction(Operation::LoadConst, zero_idx);
                        changed = true;
                        continue;
                    }
                }

                // Check for boolean patterns
                if (auto* bc = dynamic_cast<Constant::Boolean*>(c)) {
                    bool val = bc->value;

                    // true && x = x (but x is computed first, then true is pushed)
                    // So pattern is: [x on stack] LoadConst true, And -> just x
                    if (val == true && code[i+1].operation == Operation::And) {
                        code[i] = Instruction(Operation::Goto, 1);
                        code[i+1] = Instruction(Operation::Goto, 1);
                        changed = true;
                        continue;
                    }

                    // false && x = false
                    if (val == false && code[i+1].operation == Operation::And) {
                        code[i] = Instruction(Operation::Pop, std::nullopt);
                        int32_t false_idx = find_or_add_bool_constant(false);
                        code[i+1] = Instruction(Operation::LoadConst, false_idx);
                        changed = true;
                        continue;
                    }

                    // false || x = x
                    if (val == false && code[i+1].operation == Operation::Or) {
                        code[i] = Instruction(Operation::Goto, 1);
                        code[i+1] = Instruction(Operation::Goto, 1);
                        changed = true;
                        continue;
                    }

                    // true || x = true
                    if (val == true && code[i+1].operation == Operation::Or) {
                        code[i] = Instruction(Operation::Pop, std::nullopt);
                        int32_t true_idx = find_or_add_bool_constant(true);
                        code[i+1] = Instruction(Operation::LoadConst, true_idx);
                        changed = true;
                        continue;
                    }
                }
            }
        }

        // Three-instruction patterns for left-hand side constants
        // Pattern: LoadConst, [something], BinaryOp
        for (size_t i = 0; i + 2 < code.size(); ++i) {
            if (code[i].operation != Operation::LoadConst) continue;

            int32_t const_idx = code[i].operand0.value_or(-1);
            if (const_idx < 0 || static_cast<size_t>(const_idx) >= func_->constants_.size())
                continue;

            Constant* c = func_->constants_[const_idx];

            if (auto* ic = dynamic_cast<Constant::Integer*>(c)) {
                int32_t val = ic->value;

                // 0 + x = x (commutative)
                if (val == 0 && code[i+2].operation == Operation::Add) {
                    // Need to check that code[i+1] pushes exactly one value
                    // For safety, only handle simple cases
                    if (code[i+1].operation == Operation::LoadLocal ||
                        code[i+1].operation == Operation::LoadGlobal ||
                        code[i+1].operation == Operation::LoadConst) {
                        code[i] = Instruction(Operation::Goto, 1);
                        code[i+2] = Instruction(Operation::Goto, 1);
                        changed = true;
                        continue;
                    }
                }

                // 1 * x = x (commutative)
                if (val == 1 && code[i+2].operation == Operation::Mul) {
                    if (code[i+1].operation == Operation::LoadLocal ||
                        code[i+1].operation == Operation::LoadGlobal ||
                        code[i+1].operation == Operation::LoadConst) {
                        code[i] = Instruction(Operation::Goto, 1);
                        code[i+2] = Instruction(Operation::Goto, 1);
                        changed = true;
                        continue;
                    }
                }

                // 0 * x = 0 (commutative) - x becomes dead
                if (val == 0 && code[i+2].operation == Operation::Mul) {
                    if (code[i+1].operation == Operation::LoadLocal ||
                        code[i+1].operation == Operation::LoadGlobal ||
                        code[i+1].operation == Operation::LoadConst) {
                        // Replace the load with NOP, keep LoadConst 0, remove Mul
                        code[i+1] = Instruction(Operation::Goto, 1);
                        code[i+2] = Instruction(Operation::Goto, 1);
                        changed = true;
                        continue;
                    }
                }
            }
        }

        return changed;
    }

    //-------------------------------------------------------------------------
    // Pass 3: Strength Reduction
    // Replaces expensive operations with cheaper equivalents
    // - Multiply by power of 2 -> left shift (not available in MITScript, skip)
    // - Divide by power of 2 -> right shift (not available in MITScript, skip)
    // - x * 2 -> x + x
    // - x == x -> true (for integers)
    //-------------------------------------------------------------------------
    bool strength_reduce() {
        auto& code = func_->instructions;
        if (code.size() < 2) return false;

        bool changed = false;

        for (size_t i = 0; i + 1 < code.size(); ++i) {
            // Pattern: LoadConst 2, Mul -> Dup, Add (x * 2 = x + x)
            if (code[i].operation == Operation::LoadConst &&
                code[i+1].operation == Operation::Mul) {
                int32_t const_idx = code[i].operand0.value_or(-1);
                if (const_idx >= 0 && static_cast<size_t>(const_idx) < func_->constants_.size()) {
                    if (auto* ic = dynamic_cast<Constant::Integer*>(func_->constants_[const_idx])) {
                        if (ic->value == 2) {
                            // x * 2 -> x + x (Dup then Add)
                            code[i] = Instruction(Operation::Dup, std::nullopt);
                            code[i+1] = Instruction(Operation::Add, std::nullopt);
                            changed = true;
                            continue;
                        }
                    }
                }
            }

            // Pattern: Dup, Eq -> Pop, LoadConst true (x == x is always true for integers)
            // Note: This is only safe for value types (integers, bools), not references
            // For safety, we skip this optimization as MITScript has reference equality
        }

        // Three-instruction pattern: LoadConst 2, [load], Mul -> [load], Dup, Add
        for (size_t i = 0; i + 2 < code.size(); ++i) {
            if (code[i].operation == Operation::LoadConst &&
                code[i+2].operation == Operation::Mul) {
                int32_t const_idx = code[i].operand0.value_or(-1);
                if (const_idx >= 0 && static_cast<size_t>(const_idx) < func_->constants_.size()) {
                    if (auto* ic = dynamic_cast<Constant::Integer*>(func_->constants_[const_idx])) {
                        if (ic->value == 2) {
                            if (code[i+1].operation == Operation::LoadLocal ||
                                code[i+1].operation == Operation::LoadGlobal ||
                                code[i+1].operation == Operation::LoadConst) {
                                // 2 * x -> x + x
                                // Reorder: remove LoadConst 2, add Dup after load
                                code[i] = Instruction(Operation::Goto, 1);
                                // Insert Dup after the load - but we can't insert, so transform
                                // Actually, pattern is: LoadConst 2, LoadX, Mul
                                // We want: LoadX, Dup, Add
                                // Swap positions conceptually
                                auto load_inst = code[i+1];
                                code[i] = load_inst;
                                code[i+1] = Instruction(Operation::Dup, std::nullopt);
                                code[i+2] = Instruction(Operation::Add, std::nullopt);
                                changed = true;
                                continue;
                            }
                        }
                    }
                }
            }
        }

        return changed;
    }

    //-------------------------------------------------------------------------
    // Pass 4: Constant Folding
    //-------------------------------------------------------------------------
    bool fold_constants() {
        auto& code = func_->instructions;
        if (code.size() < 2) return false;

        bool changed = false;

        // Track constant values on the abstract stack
        std::vector<ConstValue> abstract_stack;

        for (size_t i = 0; i < code.size(); ++i) {
            auto& inst = code[i];

            switch (inst.operation) {
                case Operation::LoadConst: {
                    // Push the constant value onto abstract stack
                    int32_t idx = inst.operand0.value_or(-1);
                    if (idx >= 0 && static_cast<size_t>(idx) < func_->constants_.size()) {
                        Constant* c = func_->constants_[idx];
                        if (auto* ic = dynamic_cast<Constant::Integer*>(c)) {
                            abstract_stack.push_back(ConstValue::from_int(ic->value));
                        } else if (auto* bc = dynamic_cast<Constant::Boolean*>(c)) {
                            abstract_stack.push_back(ConstValue::from_bool(bc->value));
                        } else if (auto* sc = dynamic_cast<Constant::String*>(c)) {
                            abstract_stack.push_back(ConstValue::from_string(sc->value));
                        } else {
                            abstract_stack.push_back(ConstValue::none());
                        }
                    } else {
                        abstract_stack.push_back(ConstValue::unknown());
                    }
                    break;
                }

                case Operation::Add:
                case Operation::Sub:
                case Operation::Mul:
                case Operation::Div: {
                    if (abstract_stack.size() >= 2) {
                        ConstValue right = abstract_stack.back(); abstract_stack.pop_back();
                        ConstValue left = abstract_stack.back(); abstract_stack.pop_back();

                        if (left.is_int() && right.is_int()) {
                            int32_t result = 0;
                            bool valid = true;

                            switch (inst.operation) {
                                case Operation::Add: result = left.int_val + right.int_val; break;
                                case Operation::Sub: result = left.int_val - right.int_val; break;
                                case Operation::Mul: result = left.int_val * right.int_val; break;
                                case Operation::Div:
                                    if (right.int_val != 0) {
                                        result = left.int_val / right.int_val;
                                    } else {
                                        valid = false;
                                    }
                                    break;
                                default: valid = false;
                            }

                            if (valid) {
                                // Replace with constant load
                                int32_t const_idx = find_or_add_int_constant(result);
                                // We need to remove the two LoadConst and replace binary op
                                // For now, just track the result
                                abstract_stack.push_back(ConstValue::from_int(result));
                                break;
                            }
                        }
                        abstract_stack.push_back(ConstValue::unknown());
                    }
                    break;
                }

                case Operation::Neg: {
                    if (!abstract_stack.empty() && abstract_stack.back().is_int()) {
                        int32_t val = abstract_stack.back().int_val;
                        abstract_stack.back() = ConstValue::from_int(-val);
                    } else if (!abstract_stack.empty()) {
                        abstract_stack.back() = ConstValue::unknown();
                    }
                    break;
                }

                case Operation::Not: {
                    if (!abstract_stack.empty() && abstract_stack.back().is_bool()) {
                        bool val = abstract_stack.back().bool_val;
                        abstract_stack.back() = ConstValue::from_bool(!val);
                    } else if (!abstract_stack.empty()) {
                        abstract_stack.back() = ConstValue::unknown();
                    }
                    break;
                }

                case Operation::Gt:
                case Operation::Geq:
                case Operation::Eq: {
                    if (abstract_stack.size() >= 2) {
                        ConstValue right = abstract_stack.back(); abstract_stack.pop_back();
                        ConstValue left = abstract_stack.back(); abstract_stack.pop_back();

                        if (left.is_int() && right.is_int()) {
                            bool result = false;
                            switch (inst.operation) {
                                case Operation::Gt: result = left.int_val > right.int_val; break;
                                case Operation::Geq: result = left.int_val >= right.int_val; break;
                                case Operation::Eq: result = left.int_val == right.int_val; break;
                                default: break;
                            }
                            abstract_stack.push_back(ConstValue::from_bool(result));
                        } else if (left.is_bool() && right.is_bool() && inst.operation == Operation::Eq) {
                            abstract_stack.push_back(ConstValue::from_bool(left.bool_val == right.bool_val));
                        } else {
                            abstract_stack.push_back(ConstValue::unknown());
                        }
                    }
                    break;
                }

                case Operation::And:
                case Operation::Or: {
                    if (abstract_stack.size() >= 2) {
                        ConstValue right = abstract_stack.back(); abstract_stack.pop_back();
                        ConstValue left = abstract_stack.back(); abstract_stack.pop_back();

                        if (left.is_bool() && right.is_bool()) {
                            bool result = (inst.operation == Operation::And) ?
                                (left.bool_val && right.bool_val) :
                                (left.bool_val || right.bool_val);
                            abstract_stack.push_back(ConstValue::from_bool(result));
                        } else {
                            abstract_stack.push_back(ConstValue::unknown());
                        }
                    }
                    break;
                }

                case Operation::If: {
                    // Check if condition is constant
                    if (!abstract_stack.empty() && abstract_stack.back().is_bool()) {
                        bool cond = abstract_stack.back().bool_val;
                        abstract_stack.pop_back();

                        if (cond) {
                            // Always jump - convert to Goto
                            inst = Instruction(Operation::Goto, inst.operand0);
                            changed = true;
                        } else {
                            // Never jump - convert to Pop (remove the condition)
                            // Actually the condition was already popped, so just NOP
                            inst = Instruction(Operation::Goto, 1); // Skip to next
                            changed = true;
                        }
                    } else if (!abstract_stack.empty()) {
                        abstract_stack.pop_back();
                    }
                    break;
                }

                case Operation::Pop: {
                    if (!abstract_stack.empty()) {
                        abstract_stack.pop_back();
                    }
                    break;
                }

                case Operation::Dup: {
                    if (!abstract_stack.empty()) {
                        abstract_stack.push_back(abstract_stack.back());
                    }
                    break;
                }

                case Operation::Swap: {
                    if (abstract_stack.size() >= 2) {
                        std::swap(abstract_stack[abstract_stack.size()-1],
                                  abstract_stack[abstract_stack.size()-2]);
                    }
                    break;
                }

                case Operation::Return:
                case Operation::Goto:
                    // Control flow - reset abstract stack for safety
                    abstract_stack.clear();
                    break;

                case Operation::Call:
                case Operation::LoadGlobal:
                case Operation::LoadLocal:
                case Operation::LoadFunc:
                case Operation::AllocRecord:
                case Operation::FieldLoad:
                case Operation::IndexLoad:
                case Operation::LoadReference:
                case Operation::PushReference:
                case Operation::AllocClosure:
                    // These push unknown values
                    abstract_stack.push_back(ConstValue::unknown());
                    break;

                case Operation::StoreGlobal:
                case Operation::StoreLocal:
                case Operation::FieldStore:
                case Operation::IndexStore:
                case Operation::StoreReference:
                    // These pop values
                    if (!abstract_stack.empty()) abstract_stack.pop_back();
                    break;

                default:
                    // Unknown - be conservative
                    abstract_stack.clear();
                    break;
            }
        }

        return changed;
    }

    //-------------------------------------------------------------------------
    // Pass 3: Peephole Optimizations
    //-------------------------------------------------------------------------
    bool peephole_optimize() {
        auto& code = func_->instructions;
        if (code.size() < 2) return false;

        bool changed = false;

        for (size_t i = 0; i + 1 < code.size(); ++i) {
            // Pattern: Dup followed by Pop - remove both
            if (code[i].operation == Operation::Dup &&
                code[i+1].operation == Operation::Pop) {
                // Remove both instructions (mark as NOP)
                code[i] = Instruction(Operation::Goto, 1);
                code[i+1] = Instruction(Operation::Goto, 1);
                changed = true;
                continue;
            }

            // Pattern: LoadLocal X followed by StoreLocal X - remove both
            if (code[i].operation == Operation::LoadLocal &&
                code[i+1].operation == Operation::StoreLocal &&
                code[i].operand0 == code[i+1].operand0) {
                code[i] = Instruction(Operation::Goto, 1);
                code[i+1] = Instruction(Operation::Goto, 1);
                changed = true;
                continue;
            }

            // Pattern: Push constant then Pop - remove both
            if (code[i].operation == Operation::LoadConst &&
                code[i+1].operation == Operation::Pop) {
                code[i] = Instruction(Operation::Goto, 1);
                code[i+1] = Instruction(Operation::Goto, 1);
                changed = true;
                continue;
            }

            // Pattern: Swap followed by Swap - remove both
            if (code[i].operation == Operation::Swap &&
                code[i+1].operation == Operation::Swap) {
                code[i] = Instruction(Operation::Goto, 1);
                code[i+1] = Instruction(Operation::Goto, 1);
                changed = true;
                continue;
            }

            // Pattern: Not followed by Not - remove both
            if (code[i].operation == Operation::Not &&
                code[i+1].operation == Operation::Not) {
                code[i] = Instruction(Operation::Goto, 1);
                code[i+1] = Instruction(Operation::Goto, 1);
                changed = true;
                continue;
            }

            // Pattern: Neg followed by Neg - remove both
            if (code[i].operation == Operation::Neg &&
                code[i+1].operation == Operation::Neg) {
                code[i] = Instruction(Operation::Goto, 1);
                code[i+1] = Instruction(Operation::Goto, 1);
                changed = true;
                continue;
            }

            // Pattern: Goto 1 (skip to next) - mark as NOP
            if (code[i].operation == Operation::Goto &&
                code[i].operand0.value_or(0) == 1) {
                // This is effectively a NOP
                // We'll remove it in the cleanup phase
            }
        }

        // Three-instruction patterns
        for (size_t i = 0; i + 2 < code.size(); ++i) {
            // Pattern: LoadLocal X, LoadLocal X - replace with LoadLocal X, Dup
            if (code[i].operation == Operation::LoadLocal &&
                code[i+1].operation == Operation::LoadLocal &&
                code[i].operand0 == code[i+1].operand0) {
                code[i+1] = Instruction(Operation::Dup, std::nullopt);
                changed = true;
            }
        }

        return changed;
    }

    //-------------------------------------------------------------------------
    // Pass 4: Dead Store Elimination
    //-------------------------------------------------------------------------
    bool eliminate_dead_stores() {
        auto& code = func_->instructions;
        if (code.empty()) return false;

        bool changed = false;

        // Track which locals are read after each point
        // Simple backward analysis
        std::unordered_set<int32_t> live_locals;

        // First pass: find all locals that are ever read
        std::unordered_set<int32_t> ever_read;
        for (const auto& inst : code) {
            if (inst.operation == Operation::LoadLocal) {
                ever_read.insert(inst.operand0.value_or(-1));
            }
        }

        // Second pass: eliminate stores to locals never read
        // (This is a simple version - a full analysis would be more precise)
        for (auto& inst : code) {
            if (inst.operation == Operation::StoreLocal) {
                int32_t local_idx = inst.operand0.value_or(-1);
                // Check if this local is a parameter (first parameter_count_ locals)
                // Parameters should not be eliminated
                bool is_param = local_idx >= 0 &&
                               static_cast<uint32_t>(local_idx) < func_->parameter_count_;

                if (!is_param && !ever_read.count(local_idx)) {
                    // This store is dead - replace with Pop
                    inst = Instruction(Operation::Pop, std::nullopt);
                    changed = true;
                }
            }
        }

        return changed;
    }

    //-------------------------------------------------------------------------
    // Pass 5: Jump Optimization
    //-------------------------------------------------------------------------
    bool optimize_jumps() {
        auto& code = func_->instructions;
        if (code.empty()) return false;

        bool changed = false;

        for (size_t i = 0; i < code.size(); ++i) {
            auto& inst = code[i];

            // Optimize Goto chains
            if (inst.operation == Operation::Goto) {
                int32_t offset = inst.operand0.value_or(0);
                int64_t target = static_cast<int64_t>(i) + offset;

                // Follow the chain
                int chain_depth = 0;
                while (chain_depth < 10 && target >= 0 &&
                       target < static_cast<int64_t>(code.size())) {
                    const auto& target_inst = code[target];
                    if (target_inst.operation == Operation::Goto) {
                        int32_t next_offset = target_inst.operand0.value_or(0);
                        target = target + next_offset;
                        chain_depth++;
                    } else {
                        break;
                    }
                }

                // Update to jump directly to final target
                int32_t new_offset = static_cast<int32_t>(target - static_cast<int64_t>(i));
                if (new_offset != offset && chain_depth > 0) {
                    inst.operand0 = new_offset;
                    changed = true;
                }
            }

            // Optimize If to Goto chains
            if (inst.operation == Operation::If) {
                int32_t offset = inst.operand0.value_or(0);
                int64_t target = static_cast<int64_t>(i) + offset;

                // If the target is a Goto, thread through it
                if (target >= 0 && target < static_cast<int64_t>(code.size())) {
                    const auto& target_inst = code[target];
                    if (target_inst.operation == Operation::Goto) {
                        int32_t next_offset = target_inst.operand0.value_or(0);
                        int64_t final_target = target + next_offset;
                        int32_t new_offset = static_cast<int32_t>(final_target - static_cast<int64_t>(i));
                        if (new_offset != offset) {
                            inst.operand0 = new_offset;
                            changed = true;
                        }
                    }
                }
            }
        }

        return changed;
    }

    //-------------------------------------------------------------------------
    // Cleanup: Remove NOP-like instructions
    //-------------------------------------------------------------------------
    void remove_nops() {
        auto& code = func_->instructions;
        if (code.empty()) return;

        // Build a mapping from old indices to new indices
        std::vector<int32_t> index_map(code.size(), -1);
        std::vector<Instruction> new_code;
        new_code.reserve(code.size());

        // First pass: copy non-NOP instructions and build index map
        for (size_t i = 0; i < code.size(); ++i) {
            const auto& inst = code[i];

            // Skip Goto 1 (NOP)
            if (inst.operation == Operation::Goto &&
                inst.operand0.value_or(0) == 1) {
                // This is a NOP - but we need to map it to the next instruction
                continue;
            }

            index_map[i] = static_cast<int32_t>(new_code.size());
            new_code.push_back(inst);
        }

        // Fill in gaps in index_map
        for (size_t i = 0; i < index_map.size(); ++i) {
            if (index_map[i] == -1) {
                // Find the next valid index
                for (size_t j = i + 1; j <= code.size(); ++j) {
                    if (j == code.size()) {
                        index_map[i] = static_cast<int32_t>(new_code.size());
                        break;
                    }
                    if (index_map[j] != -1) {
                        index_map[i] = index_map[j];
                        break;
                    }
                }
            }
        }

        // Second pass: fix up jump offsets
        for (size_t i = 0; i < new_code.size(); ++i) {
            auto& inst = new_code[i];

            if (inst.operation == Operation::Goto || inst.operation == Operation::If) {
                // Find the original instruction index
                size_t orig_idx = 0;
                for (size_t j = 0; j < index_map.size(); ++j) {
                    if (index_map[j] == static_cast<int32_t>(i)) {
                        orig_idx = j;
                        break;
                    }
                }

                int32_t old_offset = inst.operand0.value_or(0);
                int64_t old_target = static_cast<int64_t>(orig_idx) + old_offset;

                if (old_target >= 0 && old_target < static_cast<int64_t>(index_map.size())) {
                    int32_t new_target_idx = index_map[old_target];
                    int32_t new_offset = new_target_idx - static_cast<int32_t>(i);
                    inst.operand0 = new_offset;
                }
            }
        }

        code = std::move(new_code);
    }

    //-------------------------------------------------------------------------
    // Helper: Find or add integer constant
    //-------------------------------------------------------------------------
    int32_t find_or_add_int_constant(int32_t value) {
        // Search existing constants
        for (size_t i = 0; i < func_->constants_.size(); ++i) {
            if (auto* ic = dynamic_cast<Constant::Integer*>(func_->constants_[i])) {
                if (ic->value == value) {
                    return static_cast<int32_t>(i);
                }
            }
        }

        // Add new constant
        func_->constants_.push_back(new Constant::Integer(value));
        return static_cast<int32_t>(func_->constants_.size() - 1);
    }

    int32_t find_or_add_bool_constant(bool value) {
        // Search existing constants
        for (size_t i = 0; i < func_->constants_.size(); ++i) {
            if (auto* bc = dynamic_cast<Constant::Boolean*>(func_->constants_[i])) {
                if (bc->value == value) {
                    return static_cast<int32_t>(i);
                }
            }
        }

        // Add new constant
        func_->constants_.push_back(new Constant::Boolean(value));
        return static_cast<int32_t>(func_->constants_.size() - 1);
    }
};

//-----------------------------------------------------------------------------
// Public API
//-----------------------------------------------------------------------------
inline void optimize(Function* func) {
    BytecodeOptimizer optimizer(func);
    optimizer.optimize();
}

} // namespace bytecode::optimizer
