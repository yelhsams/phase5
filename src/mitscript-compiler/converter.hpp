#pragma once

#include "./cfg.hpp"
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

using namespace mitscript;
using namespace mitscript::CFG;

struct CFGBuilder : public Visitor {

  void get_vars(Block *b, std::unordered_set<std::string> &vars) {
    for (const auto &statement_ptr : b->statements) {
      Statement *statement = statement_ptr.get();
      if (auto if_stmt = dynamic_cast<IfStatement *>(statement)) {
        get_vars(if_stmt, vars);
      } else if (auto while_stmt = dynamic_cast<WhileLoop *>(statement)) {
        get_vars(while_stmt, vars);
      } else if (auto assign_stmt = dynamic_cast<Assignment *>(statement)) {
        get_vars(assign_stmt, vars);
      } else if (auto block_stmt = dynamic_cast<Block *>(statement)) {
        get_vars(block_stmt, vars);
      }
    }
  }

  std::unordered_set<std::string> get_vars(Block *b) {
    std::unordered_set<std::string> vars;
    get_vars(b, vars);
    return vars;
  }

  void get_vars(IfStatement *if_stmt, std::unordered_set<std::string> &vars) {
    if (if_stmt->then_block) {
      get_vars(if_stmt->then_block.get(), vars);
    }
    if (if_stmt->else_block) {
      get_vars(if_stmt->else_block.get(), vars);
    }
  }

  std::unordered_set<std::string> get_vars(IfStatement *if_stmt) {
    std::unordered_set<std::string> vars;
    get_vars(if_stmt, vars);
    return vars;
  }

  void get_vars(WhileLoop *while_stmt, std::unordered_set<std::string> &vars) {
    get_vars(while_stmt->body.get(), vars);
  }

  std::unordered_set<std::string> get_vars(WhileLoop *while_stmt) {
    std::unordered_set<std::string> vars;
    get_vars(while_stmt, vars);
    return vars;
  }

  void get_vars(Assignment *assign_stmt,
                std::unordered_set<std::string> &vars) {
    if (auto var_target = dynamic_cast<Variable *>(assign_stmt->target.get())) {
      vars.insert(var_target->name);
    }
  }

  std::unordered_set<std::string> get_vars(Assignment *assign_stmt) {
    std::unordered_set<std::string> vars;
    get_vars(assign_stmt, vars);
    return vars;
  }

  FunctionCFG &CFG;
  std::unordered_map<std::string, int> localSlots;

  // std::unordered_set<std::string> globals;
  std::unordered_set<std::string> funcGlobals;

  std::unordered_set<std::string> notedNames;
  bool is_module_scope = false;
  std::shared_ptr<std::unordered_set<std::string>> moduleGlobals;
  std::shared_ptr<std::unordered_set<std::string>> parentLocals;
  std::shared_ptr<std::unordered_set<std::string>> parentGlobals;
  std::unordered_set<std::string> capturedFreeVars;
  std::unordered_set<std::string> byRefSet;

  explicit CFGBuilder(FunctionCFG &out, bool moduleScope = false)
      : CFG(out), is_module_scope(moduleScope) {};

  CFGBuilder(FunctionCFG &out,
             std::shared_ptr<std::unordered_set<std::string>> parentLocals_,
             bool moduleScope = false)
      : CFG(out), is_module_scope(moduleScope),
        parentLocals(std::move(parentLocals_)) {};

  ~CFGBuilder() noexcept override = default;

  bool isBuiltinName(const std::string &name) const {
    // Fast path: check length first to avoid string comparisons
    const size_t len = name.length();
    if (len == 5) {
      return name == "print" || name == "input";
    }
    if (len == 7) {
      return name == "intcast";
    }
    return false;
  }

  void noteName(const std::string &name) {
    if (name.empty())
      return;
    if (notedNames.insert(name).second) {
      CFG.names.push_back(name);
    }
  }

  bool isGlobalReference(const std::string &n) const {
    // Explicit global in this function overrides locals/params.
    if (funcGlobals.count(n))
      return true;

    // Locals/params (and captured parent locals) stay local.
    if (localSlots.count(n))
      return false;
    if (parentLocals && parentLocals->count(n))
      return false;

    // Inherited globals from ancestors.
    // if (parentGlobals && parentGlobals->count(n)) return true;

    // Module scope and builtins default to global.
    if (is_module_scope)
      return true;
    if (isBuiltinName(n))
      return true;

    return false;
  }

  int ensureLocal(const std::string &n) {
    auto item = localSlots.find(n);
    if (item != localSlots.end())
      return item->second;

    int slot = static_cast<int>(CFG.params.size() + CFG.locals.size());
    CFG.locals.push_back(n);
    localSlots[n] = slot;
    return slot;
  }

  int slotOfLocal(const std::string &n) { return ensureLocal(n); }

  void ensureByRef(const std::string &n) {
    if (byRefSet.insert(n).second) {
      CFG.byRefLocals.push_back(n);
    }
  }

  BasicBlock *curr = nullptr;

  int nextVReg = 0;
  VReg newVreg() { return nextVReg++; };

  BasicBlock *newBlock() {
    auto basicBlock = std::make_unique<BasicBlock>();
    basicBlock->id = (int)CFG.blocks.size();
    auto *raw = basicBlock.get();
    CFG.blocks.push_back(std::move(basicBlock));
    return raw;
  };

  void startFunction(const std::vector<std::string> &params) {
    CFG.entry = 0;
    CFG.params = params;
    localSlots.clear();
    localSlots.reserve(params.size() +
                       16); // Reserve space for params + expected locals
    notedNames.clear();
    byRefSet.clear();
    for (size_t i = 0; i < params.size(); ++i) {
      localSlots.emplace(params[i], static_cast<int>(i));
    }
    curr = newBlock();
  }

  void collectGlobals(Statement *s) {
    if (!s)
      return;
    if (auto g = dynamic_cast<Global *>(s)) {
      funcGlobals.insert(g->name);
      return;
    }
    if (auto blk = dynamic_cast<Block *>(s)) {
      for (auto &st : blk->statements)
        collectGlobals(st.get());
      return;
    }
    if (auto iff = dynamic_cast<IfStatement *>(s)) {
      collectGlobals(iff->then_block.get());
      if (iff->else_block)
        collectGlobals(iff->else_block.get());
      return;
    }
    if (auto wh = dynamic_cast<WhileLoop *>(s)) {
      collectGlobals(wh->body.get());
      return;
    }
  }

  void endWithJump(BasicBlock *from, BasicBlock *to) {
    from->term.kind = Terminator::Kind::Jump;
    from->term.target = to->id;

    from->successors = {to->id};
    to->predecessors.push_back(from->id);
  };

  void endWithCond(VReg cond, BasicBlock *true_, BasicBlock *false_) {
    curr->term.kind = Terminator::Kind::CondJump;
    curr->term.condition = cond;

    curr->term.trueTarget = true_->id;
    curr->term.falseTarget = false_->id;

    curr->successors = {true_->id, false_->id};
    true_->predecessors.push_back(curr->id);
    false_->predecessors.push_back(curr->id);
  };

  void endWithReturn(std::optional<VReg> v) {
    curr->term.kind = Terminator::Kind::Return;
    curr->term.condition = v.value_or(-1);
  };

  VReg emitValuedInstr(IROp op, std::vector<IROperand> input) {
    VReg outputReg = newVreg();
    curr->code.emplace_back(
        IRInstr{op, std::move(input), IROperand{IROperand::VREG, outputReg}});
    return outputReg;
  }

  void emitUnvaluedInstr(IROp op, std::vector<IROperand> input) {
    curr->code.emplace_back(IRInstr{op, std::move(input), std::nullopt});
  };

  VReg lastVreg = -1;

  VReg evalExpression(Expression *e) {
    e->accept(this);
    return lastVreg;
  }

  bool hasOpenTerminator() const {
    for (const auto &blkPtr : CFG.blocks) {
      if (!blkPtr)
        continue;
      const auto &blk = *blkPtr;
      if (blk.post_return)
        continue;
      if (blk.term.kind == Terminator::Kind::Jump && blk.term.target == -1) {
        return true;
      }
    }
    return false;
  }

  void execStatement(Statement *s) { s->accept(this); };

  // Visitor Implementations

  void visit(AST *node) override {
    startFunction({});
    is_module_scope = true;

    for (auto &stmt : node->statements) {
      execStatement(stmt.get());
    }

    if (hasOpenTerminator()) {
      VReg noneV =
          emitValuedInstr(IROp::LoadConst, {IROperand{IROperand::NONE, 0}});
      endWithReturn(std::optional<VReg>(noneV));
    }

    CFG.exit = curr ? curr->id : 0;
  }

  void visit(IntegerConstant *node) override {
    lastVreg = emitValuedInstr(IROp::LoadConst,
                               {IROperand{IROperand::CONSTI, node->value}});
  };
  void visit(NoneConstant * /*node*/) override {
    lastVreg =
        emitValuedInstr(IROp::LoadConst, {IROperand{IROperand::NONE, 0}});
  };
  void visit(StringConstant *node) override {
    lastVreg = emitValuedInstr(IROp::LoadConst,
                               {IROperand{IROperand::CONSTS, 0, node->value}});
  };
  void visit(BooleanConstant *node) override {
    lastVreg = emitValuedInstr(IROp::LoadConst,
                               {IROperand{IROperand::CONSTB, node->value}});
  };

  void visit(Variable *node) override {
    const auto &name = node->name;
    auto localIt = localSlots.find(name);
    bool isLocalHere = localIt != localSlots.end();

    if (!isGlobalReference(name) && !isLocalHere && parentLocals &&
        parentLocals->count(name)) {
      capturedFreeVars.insert(name);
    }

    if (isGlobalReference(name)) {
      noteName(name);
      lastVreg = emitValuedInstr(IROp::LoadGlobal,
                                 {IROperand{IROperand::NAME, 0, name}});
    } else if (isLocalHere) {
      int slot = localIt->second;
      lastVreg =
          emitValuedInstr(IROp::LoadLocal, {IROperand{IROperand::LOCAL, slot}});
    } else {
      // Captured free variable
      noteName(name);
      lastVreg = emitValuedInstr(IROp::LoadLocal,
                                 {IROperand{IROperand::NAME, 0, name}});
    }
  };

  void visit(UnaryExpression *node) override {
    VReg val = evalExpression(node->operand.get());
    lastVreg = emitValuedInstr(node->op == UnOp::NEG ? IROp::Neg : IROp::Not,
                               {IROperand{IROperand::VREG, val}});
  };

  void visit(Record *node) override {
    VReg record = emitValuedInstr(IROp::MakeRecord, {});
    for (const auto &[key, expr] : node->fields) {
      emitUnvaluedInstr(IROp::Dup, {});
      VReg value = evalExpression(expr.get());
      noteName(key);
      emitUnvaluedInstr(IROp::StoreField, {{IROperand::VREG, record},
                                           {IROperand::NAME, 0, key},
                                           {IROperand::VREG, value}});
    }
    lastVreg = record;
  };

  void visit(Block *node) override {
    for (auto &stmt : node->statements) {
      execStatement(stmt.get());
    };
  };
  void visit(Assignment *node) override {
    auto *target = node->target.get();

    if (auto fieldDeref = dynamic_cast<FieldDereference *>(target)) {
      VReg obj = evalExpression(fieldDeref->object.get());
      VReg rhs = evalExpression(node->value.get());
      noteName(fieldDeref->field_name);
      emitUnvaluedInstr(IROp::StoreField,
                        {{IROperand::VREG, obj},
                         {IROperand::NAME, 0, fieldDeref->field_name},
                         {IROperand::VREG, rhs}});
      return;
    }

    if (auto idxExpr = dynamic_cast<IndexExpression *>(target)) {
      VReg baseExpr = evalExpression(idxExpr->baseExpression.get());
      VReg idx = evalExpression(idxExpr->indexExpression.get());
      VReg rhs = evalExpression(node->value.get());
      emitUnvaluedInstr(IROp::StoreIndex, {{IROperand::VREG, baseExpr},
                                           {IROperand::VREG, idx},
                                           {IROperand::VREG, rhs}});
      return;
    }

    auto var = dynamic_cast<Variable *>(target);
    if (!var)
      return;

    const auto &varName = var->name;
    bool targetIsGlobalVar = false;
    if (is_module_scope || funcGlobals.count(varName)) {
      targetIsGlobalVar = true;
      noteName(varName);
    }

    VReg rhs = evalExpression(node->value.get());

    if (targetIsGlobalVar) {
      emitUnvaluedInstr(IROp::StoreGlobal, {{IROperand::NAME, 0, varName},
                                            {IROperand::VREG, rhs}});
    } else {
      auto localIt = localSlots.find(varName);
      if (localIt != localSlots.end()) {
        int slot = localIt->second;
        ensureByRef(varName);
        emitUnvaluedInstr(IROp::StoreLocal,
                          {{IROperand::LOCAL, slot}, {IROperand::VREG, rhs}});
      } else if (parentLocals && parentLocals->count(varName)) {
        capturedFreeVars.insert(varName);
        noteName(varName);
        emitUnvaluedInstr(IROp::Dup, {});
        emitUnvaluedInstr(IROp::StoreLocal, {{IROperand::NAME, 0, varName},
                                             {IROperand::VREG, rhs}});
      } else {
        // Default to local if nothing else matched
        int slot = ensureLocal(varName);
        ensureByRef(varName);
        emitUnvaluedInstr(IROp::StoreLocal,
                          {{IROperand::LOCAL, slot}, {IROperand::VREG, rhs}});
      }
    }
  };
  void visit(IfStatement *node) override {
    VReg cond = evalExpression(node->condition.get());

    BasicBlock *thenBlock = newBlock();
    BasicBlock *elseBlock = node->else_block ? newBlock() : nullptr;
    BasicBlock *joinBlock = newBlock();

    endWithCond(cond, thenBlock, elseBlock ? elseBlock : joinBlock);

    curr = thenBlock;

    for (auto &stmt : node->then_block->statements) {
      execStatement(stmt.get());
    }

    if (curr->term.kind == Terminator::Jump && curr->term.target == -1) {
      endWithJump(curr, joinBlock);
    }

    if (elseBlock) {
      curr = elseBlock;
      for (auto &stmt : node->else_block->statements) {
        execStatement(stmt.get());
      }
      bool elseReturns = (elseBlock->term.kind == Terminator::Kind::Return);
      if (!elseReturns && curr->term.kind == Terminator::Jump &&
          curr->term.target == -1) {
        endWithJump(curr, joinBlock);
      }
    }

    curr = joinBlock;
  };
  void visit(::mitscript::Return *node) override {
    // VReg rval = evalExpression(node -> value.get());
    // emitUnvaluedInstr(IROp::Return, {{IROperand::VREG, rval}});
    std::optional<VReg> rval;
    if (node->value) {
      rval = evalExpression(node->value.get());
    }

    BasicBlock *returnBlock = curr;
    endWithReturn(rval);
    curr = newBlock();
    curr->post_return = true;
    returnBlock->successors.push_back(curr->id);
    curr->predecessors.push_back(returnBlock->id);
  };
  void visit(BinaryExpression *node) override {
    IROp op = mapBin(node->op);
    VReg lhs = evalExpression(node->left.get());
    VReg rhs = evalExpression(node->right.get());

    lastVreg =
        emitValuedInstr(op, {{IROperand::VREG, lhs}, {IROperand::VREG, rhs}});
  };
  void visit(FieldDereference *node) override {
    VReg obj = evalExpression(node->object.get());
    noteName(node->field_name);
    lastVreg = emitValuedInstr(IROp::LoadField,
                               {IROperand{IROperand::VREG, obj},
                                {IROperand::NAME, 0, node->field_name}});
  };
  void visit(::mitscript::Call *node) override {
    VReg callee = evalExpression(node->callee.get());
    std::vector<IROperand> input;
    input.reserve(node->arguments.size() + 1);
    input.push_back({IROperand::VREG, callee});

    for (auto &arg : node->arguments) {
      input.push_back({IROperand::VREG, evalExpression(arg.get())});
    }

    lastVreg = emitValuedInstr(IROp::Call, std::move(input));
  };
  void visit(Global *node) override {
    funcGlobals.insert(node->name);
    noteName(node->name);
  };
  void visit(CallStatement *node) override {
    evalExpression(node->call.get());
    emitUnvaluedInstr(IROp::Pop, {});
  };
  void visit(WhileLoop *node) override {

    BasicBlock *B_head = newBlock();
    BasicBlock *B_body = newBlock();
    BasicBlock *B_exit = newBlock();

    endWithJump(curr, B_head);

    curr = B_head;

    VReg cond = evalExpression(node->condition.get());
    endWithCond(cond, B_body, B_exit);
    curr = B_body;
    for (auto &stmt : node->body->statements) {
      execStatement(stmt.get());
    }

    if (curr->term.kind == Terminator::Kind::Jump && curr->term.target == -1) {
      endWithJump(curr, B_head);
    }

    curr = B_exit;
  };

  void visit(FunctionDeclaration *node) override {
    // Build child function CFG
    auto childFunction = std::make_unique<FunctionCFG>();

    // Compute the set of parent locals for free-var analysis
    auto parentLocalSet = std::make_shared<std::unordered_set<std::string>>();
    size_t estimatedSize =
        localSlots.size() + byRefSet.size() + capturedFreeVars.size();
    if (parentLocals)
      estimatedSize += parentLocals->size();
    parentLocalSet->reserve(estimatedSize);

    auto addParentLocal = [&](const std::string &name) {
      // Names marked global in this function should not be seen as locals
      // by children; they must resolve to the module/global scope.
      if (!funcGlobals.count(name))
        parentLocalSet->insert(name);
    };

    for (auto &kv : localSlots)
      addParentLocal(kv.first);
    for (const auto &n : byRefSet)
      addParentLocal(n);
    for (const auto &n : capturedFreeVars)
      addParentLocal(n);
    if (parentLocals) {
      for (const auto &n : *parentLocals)
        addParentLocal(n);
    }

    CFGBuilder subBuilder(*childFunction, parentLocalSet, false);

    // 1) find function-level globals
    subBuilder.collectGlobals(node->body.get());

    // 2) initialize params & entry block
    subBuilder.startFunction(node->args);

    // 3) precompute locals: all LHS names minus globals and params
    auto locals = subBuilder.get_vars(node->body.get());
    for (const auto &g : subBuilder.funcGlobals) {
      locals.erase(g);
    }
    for (const auto &p : node->args) {
      locals.erase(p);
    }
    // Do not treat names that live in the parent as new locals;
    // those should be captured as free variables instead.
    // if (parentLocalSet) {
    //     for (const auto& name : *parentLocalSet) {
    //         locals.erase(name);
    //     }
    // }
    for (const auto &name : locals) {
      subBuilder.ensureLocal(name);
    }

    // 4) generate code for body
    for (auto &stmt : node->body->statements) {
      subBuilder.execStatement(stmt.get());
    }

    // 5) ensure an implicit "return None" if needed
    if (subBuilder.hasOpenTerminator()) {
      VReg noneReturn = subBuilder.emitValuedInstr(
          IROp::LoadConst, {{IROperand{IROperand::NONE, 0}}});
      subBuilder.endWithReturn(noneReturn);
    }

    // 6) record free vars of this child
    childFunction->freeVars.assign(subBuilder.capturedFreeVars.begin(),
                                   subBuilder.capturedFreeVars.end());
    std::sort(childFunction->freeVars.begin(), childFunction->freeVars.end());

    // mark our own locals as by-ref if they’re captured
    for (const auto &fv : childFunction->freeVars) {
      if (localSlots.count(fv))
        ensureByRef(fv);
    }

    // bubble up free vars through parents
    if (parentLocals) {
      for (const auto &fv : childFunction->freeVars) {
        if (parentLocals->count(fv)) {
          capturedFreeVars.insert(fv);
        }
      }
    }

    int childIndex = (int)CFG.children.size();
    CFG.children.push_back(std::move(childFunction));

    const FunctionCFG &child = *CFG.children.back();

    // 7) build closure: free vars (by name) + function index
    std::vector<IROperand> closureInputs;
    closureInputs.reserve(child.freeVars.size() + 1);
    for (const auto &fv : child.freeVars) {
      closureInputs.push_back({IROperand::NAME, 0, fv});
    }
    closureInputs.push_back({IROperand::CONSTI, childIndex});
    lastVreg = emitValuedInstr(IROp::AllocClosure, std::move(closureInputs));
  }
  void visit(IndexExpression *node) override {
    VReg base = evalExpression(node->baseExpression.get());
    VReg index = evalExpression(node->indexExpression.get());

    lastVreg =
        emitValuedInstr(IROp::LoadIndex, {IROperand{IROperand::VREG, base},
                                          IROperand{IROperand::VREG, index}});
  };

  IROp mapBin(BinOp op) {
    switch (op) {
    case BinOp::ADD:
      return IROp::Add;
    case BinOp::SUB:
      return IROp::Sub;
    case BinOp::MUL:
      return IROp::Mul;
    case BinOp::DIV:
      return IROp::Div;
    case BinOp::EQ:
      return IROp::CmpEq;
    case BinOp::LT:
      return IROp::CmpLt;
    case BinOp::GT:
      return IROp::CmpGt;
    case BinOp::LTE:
      return IROp::CmpLe; // NEW
    case BinOp::GTE:
      return IROp::CmpGe; // NEW
    case BinOp::AND:
      return IROp::And;
    case BinOp::OR:
      return IROp::Or;
    }
    // Should be unreachable
    return IROp::CmpEq;
  }
};
