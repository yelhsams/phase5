#include "./cfg-prettyprinter.hpp"

#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace mitscript::CFG {
namespace {
std::string indent(int n) { return std::string(n, ' '); }

template <typename T>
std::string joinList(const std::vector<T>& items, const char* prefix = "", const char* suffix = "") {
    if (items.empty()) return "[]";
    std::ostringstream oss;
    oss << '[';
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) oss << ", ";
        oss << prefix << items[i] << suffix;
    }
    oss << ']';
    return oss.str();
}

std::string opName(IROp op) {
    switch (op) {
    case IROp::LoadConst: return "LoadConst";
    case IROp::LoadLocal: return "LoadLocal";
    case IROp::StoreLocal: return "StoreLocal";
    case IROp::LoadGlobal: return "LoadGlobal";
    case IROp::StoreGlobal: return "StoreGlobal";
    case IROp::Add: return "Add";
    case IROp::Sub: return "Sub";
    case IROp::Mul: return "Mul";
    case IROp::Div: return "Div";
    case IROp::CmpEq: return "CmpEq";
    case IROp::CmpLt: return "CmpLt";
    case IROp::CmpGt: return "CmpGt";
    case IROp::CmpLe: return "CmpLe";
    case IROp::CmpGe: return "CmpGe";
    case IROp::Neg: return "Neg";
    case IROp::Not: return "Not";
    case IROp::LoadField: return "LoadField";
    case IROp::StoreField: return "StoreField";
    case IROp::LoadIndex: return "LoadIndex";
    case IROp::StoreIndex: return "StoreIndex";
    case IROp::Call: return "Call";
    case IROp::Pop: return "Pop";
    case IROp::Dup: return "Dup";
    case IROp::And: return "And";
    case IROp::Or: return "Or";
    case IROp::CondJump: return "CondJump";
    case IROp::Jump: return "Jump";
    case IROp::Return: return "Return";
    case IROp::MakeRecord: return "MakeRecord";
    case IROp::AllocClosure: return "AllocClosure";
    }
    return "<unknown>";
}

std::string operandToString(const IROperand& operand) {
    switch (operand.kind) {
    case IROperand::VREG: return "v" + std::to_string(operand.i);
    case IROperand::LOCAL: return "local#" + std::to_string(operand.i);
    case IROperand::NAME: return "name(" + operand.s + ')';
    case IROperand::CONSTI: return "const(" + std::to_string(operand.i) + ')';
    case IROperand::CONSTS: return "const(\"" + operand.s + "\")";
    case IROperand::CONSTB: return std::string("const(") + (operand.i ? "true" : "false") + ')';
    case IROperand::NONE: return "None";
    }
    return "<operand>";
}

void printTerminator(const Terminator& term, std::ostream& os, int baseIndent) {
    auto ind = indent(baseIndent);
    os << ind << "terminator: ";
    switch (term.kind) {
    case Terminator::Kind::Jump:
        os << "jump -> " << term.target;
        break;
    case Terminator::Kind::CondJump:
        os << "cond v" << term.condition << " ? " << term.trueTarget << " : " << term.falseTarget;
        break;
    case Terminator::Kind::Return:
        os << "return";
        if (term.condition >= 0) {
            os << " v" << term.condition;
        } else {
            os << " void";
        }
        break;
    }
    os << '\n';
}

void printBlock(const BasicBlock& block, std::ostream& os, int baseIndent) {
    auto ind = indent(baseIndent);
    os << ind << "block #" << block.id;
    if (block.post_return) {
        os << " (post-return)";
    }
    os << '\n';
    if (!block.predecessors.empty()) {
        os << ind << "  preds: " << joinList(block.predecessors) << '\n';
    }
    if (!block.successors.empty()) {
        os << ind << "  succs: " << joinList(block.successors) << '\n';
    }
    if (block.code.empty()) {
        os << ind << "  <no instructions>\n";
    } else {
        for (const auto& instr : block.code) {
            os << ind << "  " << opName(instr.op);
            if (!instr.inputs.empty()) {
                os << " [";
                for (size_t i = 0; i < instr.inputs.size(); ++i) {
                    if (i) os << ", ";
                    os << operandToString(instr.inputs[i]);
                }
                os << ']';
            }
            if (instr.output) {
                os << " -> " << operandToString(*instr.output);
            }
            os << '\n';
        }
    }
    printTerminator(block.term, os, baseIndent + 2);
}
} // namespace

void prettyprint(const FunctionCFG& fn, std::ostream& os, int indentLevel) {
    auto ind = indent(indentLevel);
    os << ind << "FunctionCFG\n";
    auto printStringList = [&](const char* label, const std::vector<std::string>& values) {
        os << ind << "  " << label << ": " << joinList(values) << '\n';
    };
    printStringList("params", fn.params);
    printStringList("locals", fn.locals);
    printStringList("free_vars", fn.freeVars);
    printStringList("byref_locals", fn.byRefLocals);
    printStringList("names", fn.names);
    os << ind << "  entry: " << fn.entry << '\n';
    os << ind << "  exit: " << fn.exit << '\n';

    os << ind << "  blocks:\n";
    if (fn.blocks.empty()) {
        os << ind << "    <no blocks>\n";
    } else {
        for (const auto& blkPtr : fn.blocks) {
            if (!blkPtr) continue;
            printBlock(*blkPtr, os, indentLevel + 4);
        }
    }

    if (!fn.children.empty()) {
        os << ind << "  children:\n";
        for (size_t i = 0; i < fn.children.size(); ++i) {
            os << ind << "    [" << i << "]\n";
            prettyprint(*fn.children[i], os, indentLevel + 6);
        }
    }
}

} // namespace mitscript::CFG
