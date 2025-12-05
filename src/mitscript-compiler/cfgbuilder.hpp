#pragma once
#include "../mitscript-interpreter/ast.hpp"

using namespace mitscript;

class CFGBuilder : public Visitor {
    void visit(AST* node) override;
    void visit(Block* node) override;
    void visit(Assignment* node) override;
    void visit(IfStatement* node) override;
    void visit(::mitscript::Return* node) override;
    void visit(BinaryExpression* node) override;
    void visit(FieldDereference* node) override;
    void visit(::mitscript::Call* node) override;
    void visit(Global* node) override;
    void visit(CallStatement* node) override;
    void visit(WhileLoop* node) override;
    void visit(FunctionDeclaration* node) override;
    void visit(UnaryExpression* node) override;
    void visit(IndexExpression* node) override;
    void visit(Record* node) override;
    void visit(StringConstant* node) override;
    void visit(BooleanConstant* node) override;
    void visit(Variable* node) override;
};
