#pragma once
#include "../mitscript-interpreter/ast.hpp"
#include <optional>

namespace mitscript::CFG
{

    using BlockId = int;
    using VReg = int;

    enum IROp {
        LoadConst,

        LoadLocal,
        StoreLocal,
        LoadGlobal,
        StoreGlobal,

        Add,
        Sub,
        Mul,
        Div,
        CmpEq,
        CmpLt,
        CmpGt,
        CmpLe,   
        CmpGe,

        Neg,
        Not,

        LoadField,
        StoreField,

        LoadIndex,
        StoreIndex,

        Call,
        Pop,
        Dup,
        And,
        Or,

        CondJump,
        Jump,

        Return,

        MakeRecord,
        AllocClosure,
    };
;

    struct IROperand {
        enum Kind { VREG, LOCAL, NAME, CONSTI, CONSTS, CONSTB, NONE } kind;
        int i = 0;
        std::string s;

        IROperand() : kind(NONE), i(0), s() {}
        IROperand(Kind k, int i_val = 0, std::string s_val = {})
            : kind(k), i(i_val), s(std::move(s_val)) {}
    };

    struct IRInstr {
        IROp op;
        std::vector<IROperand> inputs;
        std::optional<IROperand> output;

    };

    struct Terminator{
        enum Kind {Jump, CondJump, Return} kind;
        BlockId target = -1;
        VReg condition = -1;
        BlockId trueTarget = -1;
        BlockId falseTarget = -1;

    };

    struct BasicBlock {
        BlockId id;
        std::vector<IRInstr> code;
        Terminator term{Terminator::Kind::Jump, -1};
        std::vector<BlockId> successors;
        std::vector<BlockId> predecessors;
        bool post_return = false;
    };

    struct FunctionCFG {
        std::vector<std::string> params;
        std::vector<std::string> locals;
        std::vector<std::string> freeVars;
        std::vector<std::string> byRefLocals;
        std::vector<std::string> names;

        BlockId entry = 0;
        BlockId exit = -1;

        std::vector<std::unique_ptr<BasicBlock>> blocks;

        std::vector<std::unique_ptr<FunctionCFG>> children;
    };

}; // namespace mitscript
