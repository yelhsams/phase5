#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <optional>

#include "./cfg.hpp"
#include "../bytecode/instructions.hpp"
#include "../bytecode/types.hpp"

using namespace mitscript;
using namespace mitscript::CFG;

struct BytecodeConverter {

    // BytecodeConverter(FunctionCFG* cfg) : CFG(*cfg) {};
    struct Fixup {
        int instr_index;
        BlockId target_block;
    };

    std::unordered_set<std::string> global_names_;
    std::vector<std::unique_ptr<bytecode::Function>> function_arena;
    std::vector<std::unique_ptr<bytecode::Constant>> constant_arena;

    struct ConstKey {
        enum Kind {NONE, I32, STR, BOOL} kind = NONE;
        int i32{};
        bool b{};
        std::string s;

        bool operator==(const ConstKey& o) const {
            if (kind != o.kind) return false;
            switch (kind) {
                case NONE: return true;
                case I32: return i32 == o.i32;
                case STR: return s == o.s;
                case BOOL: return b == o.b;

            }
            return false;
        }
    };

    struct ConstKeyHasher {
        std::size_t operator()(ConstKey const& k) const noexcept {
            switch (k.kind) {
                case ConstKey::NONE: return 0x9e3779b1u;
                case ConstKey::I32:  return std::hash<int>{}(k.i32) ^ 0x12345u;
                case ConstKey::STR:  return std::hash<std::string>{}(k.s) ^ 0x23456u;
                case ConstKey::BOOL: return std::hash<bool>{}(k.b) ^ 0x34567u;
            }
            return 0u;
        }
    };

    static std::string decodeStringLiteral(const std::string& literal) {
        if (literal.size() >= 2 && literal.front() == '"' && literal.back() == '"') {
            std::string out;
            out.reserve(literal.size() - 2);
            for (size_t i = 1; i + 1 < literal.size(); ++i) {
                char c = literal[i];
                if (c == '\\' && i + 1 < literal.size()) {
                    char next = literal[++i];
                    switch (next) {
                        case 'n': out.push_back('\n'); break;
                        case 't': out.push_back('\t'); break;
                        case '\\': out.push_back('\\'); break;
                        case '"': out.push_back('"'); break;
                        default: out.push_back(next); break;
                    }
                } else {
                    out.push_back(c);
                }
            }
            return out;
        }
        return literal;
    }

    int internConstant(bytecode::Function* fn,
                    std::unordered_map<ConstKey, int, ConstKeyHasher>& cmap,
                    const IROperand& operand,
                    bool /*is_toplevel*/) {

        using K = ConstKey;
        K key{};
        std::unique_ptr<bytecode::Constant> cptr;

        switch (operand.kind) {
            case IROperand::CONSTI:
                key.kind = K::I32;
                key.i32 = operand.i;
                break;
            case IROperand::CONSTS:
                key.kind = K::STR;
                key.s = decodeStringLiteral(operand.s);
                break;
            case IROperand::CONSTB:
                key.kind = K::BOOL;
                key.b = (operand.i != 0);
                break;
            case IROperand::NONE:
                key.kind = K::NONE;
                break;
            default:
                throw std::runtime_error("LoadConst expects CONSTI/CONSTS/CONSTB/NONE");

        }

        auto it = cmap.find(key);
        if (it != cmap.end()) return it -> second;

        bytecode::Constant* raw = nullptr;
        switch (key.kind) {
            case K::NONE: {
                auto up = std::make_unique<bytecode::Constant::None>();
                raw = up.get();
                constant_arena.push_back(std::move(up));
                break;
            }
            case K::I32: {
                auto up = std::make_unique<bytecode::Constant::Integer>(key.i32);
                raw = up.get();
                constant_arena.push_back(std::move(up));
                break;

            }
            case K::BOOL: {
                auto up = std::make_unique<bytecode::Constant::Boolean>(key.b);
                raw = up.get();
                constant_arena.push_back(std::move(up));
                break;
            }

            case K::STR: {
                auto up = std::make_unique<bytecode::Constant::String>(key.s);
                raw = up.get();
                constant_arena.push_back(std::move(up));
                break;
            }
        }
        int index = static_cast<int>(fn -> constants_.size());
        fn -> constants_.push_back(raw);
        cmap.emplace(key, index);
        return index;
    };

    static void dfsPostorder(const FunctionCFG& cfg,
        BlockId b,
        std::vector<char>& seen,
        std::vector<BlockId>& out
        ) {

        if (b < 0 || b >= (BlockId)cfg.blocks.size()) return;
        if (seen[b]) return;

        seen[b] = 1;

        const BasicBlock& block = *cfg.blocks[b];
        if (block.post_return) return;

        for (auto it = block.successors.rbegin(); it != block.successors.rend(); ++it) {
            dfsPostorder(cfg, *it, seen, out);
        }

        out.push_back(b);
    }


    std::vector<BlockId> getBlocks(FunctionCFG& cfg, BlockId entry) {
        std::vector<BlockId> out;
        std::vector<char> seen(cfg.blocks.size(), 0);
        dfsPostorder(cfg, entry, seen, out);
        return out;
    };

    bytecode::Operation mapToBytecodeOp(IROp op) {
        using O = bytecode::Operation;
        switch (op) {
            case IROp::LoadConst:   return O::LoadConst;
            case IROp::LoadLocal:   return O::LoadLocal;
            case IROp::StoreLocal:  return O::StoreLocal;
            case IROp::LoadGlobal:  return O::LoadGlobal;
            case IROp::StoreGlobal: return O::StoreGlobal;

            case IROp::Add:   return O::Add;
            case IROp::Sub:   return O::Sub;
            case IROp::Mul:   return O::Mul;
            case IROp::Div:   return O::Div;
            case IROp::CmpEq: return O::Eq;
            case IROp::CmpLt: return O::Gt; // handle operand order swap in emitter
            case IROp::CmpLe: return O::Geq; // swap handled in emitter
            case IROp::CmpGt: return O::Gt;
            case IROp::CmpGe: return O::Geq;

            case IROp::Neg: return O::Neg;
            case IROp::Not: return O::Not;

            case IROp::LoadField:  return O::FieldLoad;
            case IROp::StoreField: return O::FieldStore;
            case IROp::LoadIndex:  return O::IndexLoad;
            case IROp::StoreIndex: return O::IndexStore;

            case IROp::Call:         return O::Call;
            case IROp::Pop:          return O::Pop;
            case IROp::Dup:          return O::Dup;
            case IROp::And:          return O::And;
            case IROp::Or:           return O::Or;
            case IROp::AllocClosure: return O::AllocClosure;

            case IROp::CondJump: return O::If;
            case IROp::Jump:     return O::Goto;
            case IROp::Return:   return O::Return;

            case IROp::MakeRecord: return O::AllocRecord;

            default:
                throw std::runtime_error("Unsupported IROp in mapToBytecodeInstr");
        }
    }


    bytecode::Function* convert(FunctionCFG& cfg, bool is_toplevel = false) {
        auto func = std::make_unique<bytecode::Function>();
        bytecode::Function* fn = func.get();
        function_arena.push_back(std::move(func));

        int builtin_count = 0;
        const char* builtin_names[] = {"print", "input", "intcast"};
        const uint32_t builtin_params[] = {1, 0, 1};
        const int builtin_total = 3;

        // Reset global name set each conversion; only populate for top-level.
        global_names_.clear();

        // Top-level loads builtins
        if (is_toplevel) {
            for (const auto& n : cfg.names) global_names_.insert(n);

            for (int i = 0; i < builtin_total; ++i) global_names_.insert(builtin_names[i]);

            for (int i = 0; i < builtin_total; ++i) {
                auto builtin = std::make_unique<bytecode::Function>();
                bytecode::Function* raw_builtin = builtin.get();
                raw_builtin->parameter_count_ = builtin_params[i];
                function_arena.push_back(std::move(builtin));
                fn->functions_.push_back(raw_builtin);
            }
            builtin_count = builtin_total;
        }

        // Recurse on nested functions
        for (auto& child : cfg.children) {
            bytecode::Function* childPtr = convert(*child, false);
            fn->functions_.push_back(childPtr);
        }

        fn->parameter_count_ = static_cast<uint32_t>(cfg.params.size());

        fn->local_vars_ = cfg.params;
        fn->local_vars_.insert(fn->local_vars_.end(), cfg.locals.begin(), cfg.locals.end());
        fn->local_reference_vars_ = cfg.byRefLocals;
        fn->free_vars_ = cfg.freeVars;

        std::unordered_map<std::string, int> ref_index;
        for (int i = 0; i < (int)fn->local_reference_vars_.size(); ++i) {
            ref_index.emplace(fn->local_reference_vars_[i], i);
        }

        // Map local names to indices
        std::unordered_map<std::string, int> local_index;
        for (int i = 0; i < (int)fn->local_vars_.size(); i++) {
            local_index.emplace(fn->local_vars_[i], i);
        }

        std::unordered_map<std::string, int> nameIdxMapping;
        for (auto &n : cfg.names) {
            nameIdxMapping.emplace(n, (int)fn->names_.size());
            fn->names_.push_back(n);
        }

        // Name interning for globals/fields
        auto internName = [&](const std::string& name) {
            auto [it, inserted] = nameIdxMapping.emplace(name, (int)fn->names_.size());
            if (inserted) fn->names_.push_back(name);
            return it->second;
        };

        std::unordered_map<ConstKey, int, ConstKeyHasher> constMap;

        std::vector<BlockId> blockIds = getBlocks(cfg, cfg.entry);
        std::reverse(blockIds.begin(), blockIds.end());

        std::vector<int> block_first_instrs(cfg.blocks.size(), -1);
        std::vector<Fixup> fixups;

        auto emit = [&](bytecode::Operation op, std::optional<int32_t> o = std::nullopt) {
            fn->instructions.emplace_back(op, o);
            return (int)fn->instructions.size() - 1;
        };

        // If top-level: register builtins as globals
        int builtin_name_indices[3] = {-1, -1, -1};
        if (is_toplevel) {
            for (int i = 0; i < builtin_total; ++i) {
                builtin_name_indices[i] = internName(builtin_names[i]);
            }

            for (int i = 0; i < builtin_total; ++i) {
                emit(bytecode::Operation::LoadFunc, i);
                emit(bytecode::Operation::AllocClosure, 0);
                emit(bytecode::Operation::StoreGlobal, builtin_name_indices[i]);
            }
        }


        for (BlockId b : blockIds) {
            auto& block = *cfg.blocks[b];
            block_first_instrs[b] = (int)fn->instructions.size();

            for (const auto& ir : block.code) {
                bytecode::Operation op = mapToBytecodeOp(ir.op);
                std::optional<int32_t> operand;

                switch (ir.op) {
                    case IROp::LoadLocal:
                    case IROp::StoreLocal: {
                        auto &x = ir.inputs[0];

                        if (x.kind == IROperand::LOCAL) {
                            int li = x.i;
                            if (li < 0 || li >= (int)fn->local_vars_.size())
                                throw std::runtime_error("Load/StoreLocal: local index out of range");
                            const std::string& lname = fn->local_vars_[li];

                            if (global_names_.count(lname)) {
                                op = (ir.op == IROp::LoadLocal) ? bytecode::Operation::LoadGlobal
                                                                : bytecode::Operation::StoreGlobal;
                                operand = internName(lname);
                            } else {
                                operand = li;
                            }
                            break;
                        }

                        if (x.kind == IROperand::NAME) {
                            if (auto it = local_index.find(x.s); it != local_index.end()) {
                                operand = it->second;
                            } else {
                                // Free variable captured via closure
                                auto fvIt = std::find(fn->free_vars_.begin(), fn->free_vars_.end(), x.s);
                                if (fvIt != fn->free_vars_.end()) {
                                    int idx = (int)(fn->local_reference_vars_.size() +
                                                    std::distance(fn->free_vars_.begin(), fvIt));
                                    emit(bytecode::Operation::PushReference, idx);
                                    if (ir.op == IROp::LoadLocal) {
                                        op = bytecode::Operation::LoadReference;
                                        operand = std::nullopt;
                                    } else {
                                        emit(bytecode::Operation::Swap);
                                        op = bytecode::Operation::StoreReference;
                                        operand = std::nullopt;
                                    }
                                    break;
                                }
                                op = (ir.op == IROp::LoadLocal) ? bytecode::Operation::LoadGlobal
                                                                : bytecode::Operation::StoreGlobal;
                                operand = internName(x.s);
                                break;
                            }
                        }

                        // Fallback: treat as global name
                        op = (ir.op == IROp::LoadLocal) ? bytecode::Operation::LoadGlobal
                                                        : bytecode::Operation::StoreGlobal;
                        operand = internName(x.s);
                        break;
                    }

                    case IROp::LoadGlobal:
                    case IROp::StoreGlobal:
                        operand = internName(ir.inputs[0].s);
                        break;

                    case IROp::LoadConst: {
                        operand = internConstant(fn, constMap, ir.inputs[0], false);
                        break;
                    }

                    case IROp::LoadField:

                    case IROp::StoreField:
                        for (auto &x : ir.inputs)
                            if (x.kind == IROperand::NAME)
                                operand = internName(x.s);
                        break;

                    case IROp::CmpLt:
                        // a < b  →  swap then Gt (computes b > a)
                        emit(bytecode::Operation::Swap);
                        op = bytecode::Operation::Gt;
                        break;

                    case IROp::CmpLe:
                        // a <= b → swap then Geq (computes b >= a)
                        emit(bytecode::Operation::Swap);
                        op = bytecode::Operation::Geq;
                        break;

                    case IROp::CmpGt:
                        op = bytecode::Operation::Gt;
                        break;

                    case IROp::CmpGe:
                        op = bytecode::Operation::Geq;
                        break;

                    case IROp::Call: {
                    if (!ir.inputs.empty()) {
                        const auto& cal = ir.inputs[0];
                        if (cal.kind == IROperand::NAME) {
                            emit(bytecode::Operation::LoadGlobal, internName(cal.s));
                        } else if (cal.kind == IROperand::LOCAL) {
                            emit(bytecode::Operation::LoadLocal, cal.i);
                        }
                    }

                    operand = static_cast<int>(ir.inputs.size()) - 1;
                    break;
                }

                    case IROp::AllocClosure: {

                        int n = (int)ir.inputs.size();
                        if (n == 0) throw std::runtime_error("AllocClosure missing function operand");

                        const IROperand &fop = ir.inputs[n - 1];
                        if (fop.kind != IROperand::CONSTI)
                            throw std::runtime_error("AllocClosure expects final operand CONSTI(functionIndex)");

                        int functionIndex = builtin_count + fop.i;
                        emit(bytecode::Operation::LoadFunc, functionIndex);

                        // Use the child function's free_vars list as the source of captured refs.
                        if (functionIndex < 0 ||
                            functionIndex >= static_cast<int>(fn->functions_.size())) {
                            throw std::runtime_error("AllocClosure: function index out of range");
                        }
                        bytecode::Function *child_fn = fn->functions_[functionIndex];

                        int m = static_cast<int>(child_fn->free_vars_.size());
                        for (const auto &fv_name : child_fn->free_vars_) {
                            // Local ref?
                            if (auto it = ref_index.find(fv_name); it != ref_index.end()) {
                                emit(bytecode::Operation::PushReference, it->second);
                            } else {
                                // Free var from closure env
                                auto it2 = std::find(fn->free_vars_.begin(), fn->free_vars_.end(), fv_name);
                                if (it2 == fn->free_vars_.end())
                                    throw std::runtime_error("AllocClosure unknown free var: " + fv_name);

                                int idx = (int)(fn->local_reference_vars_.size() +
                                                std::distance(fn->free_vars_.begin(), it2));
                                emit(bytecode::Operation::PushReference, idx);
                            }
                        }

                        op = bytecode::Operation::AllocClosure;
                        operand = m;
                        break;
                    }

                    default:
                        break;
                }

                emit(op, operand);
            }

            switch (block.term.kind) {
                case Terminator::Kind::Return:
                    if (block.term.condition == -1) {
                        IROperand noneOp;
                        noneOp.kind = IROperand::NONE;
                        int noneIdx = internConstant(fn, constMap, noneOp, is_toplevel);
                        emit(bytecode::Operation::LoadConst, noneIdx);
                    }
                    emit(bytecode::Operation::Return);
                    break;

                case Terminator::Kind::Jump: {
                    if (block.term.target < 0 ||
                        block.term.target >= static_cast<BlockId>(cfg.blocks.size())) {
                        throw std::runtime_error("Jump target out of range");
                    }
                    int at = emit(bytecode::Operation::Goto, 0);
                    fixups.push_back({at, block.term.target});
                    break;
                }

                case Terminator::Kind::CondJump: {
                    if (block.term.falseTarget < 0 ||
                        block.term.falseTarget >= static_cast<BlockId>(cfg.blocks.size())) {
                        throw std::runtime_error("CondJump false target out of range");
                    }
                    emit(bytecode::Operation::Not);
                    int at = emit(bytecode::Operation::If, 0);
                    fixups.push_back({at, block.term.falseTarget});
                    break;
                }
            }
        }

        // Patch fixups
        for (auto &f : fixups) {
            if (f.target_block < 0 ||
                f.target_block >= static_cast<BlockId>(cfg.blocks.size())) {
                throw std::runtime_error("Fixup target out of range");
            }
            int to = block_first_instrs[f.target_block];
            if (to < 0) {
                throw std::runtime_error("Fixup target not visited");
            }
            int rel = to - f.instr_index;
            fn->instructions[f.instr_index].operand0 = rel;
        }



        return fn;
    }

};
