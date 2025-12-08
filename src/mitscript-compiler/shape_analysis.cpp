#include "shape_analysis.hpp"

#include <algorithm>
#include <queue>
#include <sstream>
#include <unordered_set>

using namespace mitscript::CFG;

namespace mitscript::analysis {

namespace {

size_t count_vregs(const FunctionCFG& fn) {
    int max_v = -1;
    for (const auto& blk_ptr : fn.blocks) {
        if (!blk_ptr) continue;
        for (const auto& ir : blk_ptr->code) {
            if (ir.output && ir.output->kind == IROperand::VREG)
                max_v = std::max(max_v, ir.output->i);
            for (const auto& inp : ir.inputs) {
                if (inp.kind == IROperand::VREG)
                    max_v = std::max(max_v, inp.i);
            }
        }
        if (blk_ptr->term.kind == Terminator::Kind::CondJump ||
            blk_ptr->term.kind == Terminator::Kind::Return) {
            max_v = std::max(max_v, blk_ptr->term.condition);
        }
    }
    return (max_v < 0) ? 0u : static_cast<size_t>(max_v + 1);
}

size_t count_locals(const FunctionCFG& fn) {
    return fn.params.size() + fn.locals.size();
}

ShapeInfo meet(const ShapeInfo& a, const ShapeInfo& b) {
    if (a.kind == ShapeKind::Top || b.kind == ShapeKind::Top) return ShapeInfo::top();
    if (a.kind == ShapeKind::Unknown) return b;
    if (b.kind == ShapeKind::Unknown) return a;
    if (a.kind == ShapeKind::ShapeId && b.kind == ShapeKind::ShapeId) {
        if (a.shape_id == b.shape_id) return a;
        return ShapeInfo::top();
    }
    return ShapeInfo::top();
}

void meet_state(ShapeState& dst, const ShapeState& src) {
    for (size_t i = 0; i < dst.vregs.size(); ++i) {
        dst.vregs[i] = meet(dst.vregs[i], src.vregs[i]);
    }
    for (size_t i = 0; i < dst.locals.size(); ++i) {
        dst.locals[i] = meet(dst.locals[i], src.locals[i]);
    }
}

struct AllocationShape {
    BlockId block;
    size_t instr_index;
    int vreg;
    std::vector<std::string> fields;
};

// Discover shapes from MakeRecord + subsequent StoreField to same object in block.
std::vector<AllocationShape> discover_record_shapes(FunctionCFG& fn) {
    std::vector<AllocationShape> shapes;

    for (size_t bid = 0; bid < fn.blocks.size(); ++bid) {
        if (!fn.blocks[bid]) continue;
        const auto& blk = *fn.blocks[bid];

        for (size_t idx = 0; idx < blk.code.size(); ++idx) {
            const auto& ir = blk.code[idx];
            if (ir.op != IROp::MakeRecord) continue;
            if (!ir.output || ir.output->kind != IROperand::VREG) continue;
            int target = ir.output->i;

            AllocationShape a{static_cast<BlockId>(bid), idx, target, {}};
            for (size_t j = idx + 1; j < blk.code.size(); ++j) {
                const auto& follower = blk.code[j];
                if (follower.op != IROp::StoreField) continue;
                if (follower.inputs.size() < 2) continue;
                const auto& obj = follower.inputs[0];
                const auto& field = follower.inputs[1];
                if (obj.kind != IROperand::VREG || obj.i != target) continue;
                if (field.kind == IROperand::NAME) {
                    a.fields.push_back(field.s);
                }
            }
            shapes.push_back(std::move(a));
        }
    }
    return shapes;
}

} // namespace

int ShapeRegistry::intern_shape(const std::vector<std::string>& fields) {
    std::ostringstream oss;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) oss << '\0';
        oss << fields[i];
    }
    std::string key = oss.str();
    auto it = key_to_id_.find(key);
    if (it != key_to_id_.end()) return it->second;

    int id = static_cast<int>(shapes_.size());
    Shape sh;
    sh.id = id;
    sh.fields = fields;
    shapes_.push_back(std::move(sh));
    key_to_id_[key] = id;
    return id;
}

const Shape* ShapeRegistry::lookup(int id) const {
    if (id < 0 || id >= static_cast<int>(shapes_.size())) return nullptr;
    return &shapes_[id];
}

int ShapeRegistry::slot_index(int shape_id, const std::string& field) const {
    const Shape* sh = lookup(shape_id);
    if (!sh) return -1;
    for (size_t i = 0; i < sh->fields.size(); ++i) {
        if (sh->fields[i] == field) return static_cast<int>(i);
    }
    return -1;
}

ShapeAnalysisResult run_shape_analysis(FunctionCFG& fn) {
    ShapeAnalysisResult res;
    res.num_vregs = count_vregs(fn);
    res.num_locals = count_locals(fn);

    // Pre-discover record literal shapes.
    std::unordered_map<BlockId, std::unordered_map<size_t, int>> make_record_shape;
    for (const auto& a : discover_record_shapes(fn)) {
        int sid = res.registry.intern_shape(a.fields);
        make_record_shape[a.block][a.instr_index] = sid;
    }

    ShapeState bottom;
    bottom.vregs.assign(res.num_vregs, ShapeInfo::unknown());
    bottom.locals.assign(res.num_locals, ShapeInfo::unknown());

    res.in_states.assign(fn.blocks.size(), bottom);
    res.out_states.assign(fn.blocks.size(), bottom);

    std::queue<BlockId> worklist;
    if (fn.entry >= 0 && fn.entry < static_cast<BlockId>(fn.blocks.size()) && fn.blocks[fn.entry]) {
        worklist.push(fn.entry);
    }

    auto transfer = [&](BlockId bid) {
        ShapeState state = res.in_states[bid];
        const auto& blk = *fn.blocks[bid];

        for (size_t i = 0; i < blk.code.size(); ++i) {
            const auto& ir = blk.code[i];
            auto output_shape = ShapeInfo::unknown();

            switch (ir.op) {
                case IROp::MakeRecord: {
                    auto it = make_record_shape[bid].find(i);
                    if (it != make_record_shape[bid].end()) {
                        output_shape = ShapeInfo::shape(it->second);
                    } else {
                        output_shape = ShapeInfo::top();
                    }
                    break;
                }
                case IROp::LoadLocal: {
                    if (ir.output && !ir.inputs.empty() && ir.inputs[0].kind == IROperand::LOCAL) {
                        int slot = ir.inputs[0].i;
                        if (slot >= 0 && slot < static_cast<int>(state.locals.size())) {
                            output_shape = state.locals[slot];
                        }
                    }
                    break;
                }
                case IROp::StoreLocal: {
                    if (ir.inputs.size() >= 2 &&
                        ir.inputs[0].kind == IROperand::LOCAL &&
                        ir.inputs[1].kind == IROperand::VREG) {
                        int slot = ir.inputs[0].i;
                        int reg = ir.inputs[1].i;
                        if (slot >= 0 && slot < static_cast<int>(state.locals.size()) &&
                            reg >= 0 && reg < static_cast<int>(state.vregs.size())) {
                            state.locals[slot] = state.vregs[reg];
                        }
                    }
                    // StoreLocal does not define a vreg.
                    continue;
                }
                case IROp::LoadField:
                case IROp::LoadIndex:
                case IROp::Add:
                case IROp::Sub:
                case IROp::Mul:
                case IROp::Div:
                case IROp::CmpEq:
                case IROp::CmpLt:
                case IROp::CmpGt:
                case IROp::CmpLe:
                case IROp::CmpGe:
                case IROp::Neg:
                case IROp::Not:
                case IROp::And:
                case IROp::Or:
                case IROp::LoadConst:
                case IROp::LoadGlobal:
                case IROp::AllocClosure:
                case IROp::Dup:
                case IROp::Pop:
                case IROp::CondJump:
                case IROp::Jump:
                case IROp::Return:
                case IROp::StoreField:
                case IROp::StoreIndex:
                case IROp::StoreGlobal:
                case IROp::Call:
                default:
                    // Unknown shape result.
                    output_shape = (ir.op == IROp::Call) ? ShapeInfo::top() : ShapeInfo::unknown();
                    break;
            }

            if (ir.output && ir.output->kind == IROperand::VREG) {
                int out = ir.output->i;
                if (out >= 0 && out < static_cast<int>(state.vregs.size())) {
                    state.vregs[out] = output_shape;
                }
            }
        }

        res.out_states[bid] = state;
    };

    std::vector<char> in_queue(fn.blocks.size(), 0);
    if (!worklist.empty()) in_queue[fn.entry] = 1;

    while (!worklist.empty()) {
        BlockId bid = worklist.front();
        worklist.pop();
        in_queue[bid] = 0;
        if (bid < 0 || bid >= static_cast<BlockId>(fn.blocks.size())) continue;
        if (!fn.blocks[bid]) continue;

        transfer(bid);
        const auto& out_state = res.out_states[bid];

        for (auto succ : fn.blocks[bid]->successors) {
            if (succ < 0 || succ >= static_cast<BlockId>(fn.blocks.size())) continue;
            if (!fn.blocks[succ]) continue;

            ShapeState joined = res.in_states[succ];
            meet_state(joined, out_state);
            if (joined.vregs != res.in_states[succ].vregs ||
                joined.locals != res.in_states[succ].locals) {
                res.in_states[succ] = std::move(joined);
                if (!in_queue[succ]) {
                    worklist.push(succ);
                    in_queue[succ] = 1;
                }
            }
        }
    }

    return res;
}

ShapeInfo get_shape_at_block_out(const ShapeAnalysisResult& res, BlockId bid, int vreg) {
    if (bid < 0 || bid >= static_cast<BlockId>(res.out_states.size())) return ShapeInfo::unknown();
    const auto& st = res.out_states[bid];
    if (vreg < 0 || vreg >= static_cast<int>(st.vregs.size())) return ShapeInfo::unknown();
    return st.vregs[vreg];
}

bool is_field_access_monomorphic(const ShapeAnalysisResult& res, BlockId bid, int vreg) {
    auto s = get_shape_at_block_out(res, bid, vreg);
    return s.kind == ShapeKind::ShapeId;
}

int get_slot_index(const ShapeAnalysisResult& res, int shape_id, const std::string& field_name) {
    return res.registry.slot_index(shape_id, field_name);
}

} // namespace mitscript::analysis
