#include "./ast.hpp"

namespace mitscript {
    void AST::accept(Visitor* v) { v->visit(this); }
    void Block::accept(Visitor* v) { v->visit(this); }
    void Assignment::accept(Visitor* v) { v->visit(this); }
    void IfStatement::accept(Visitor* v) { v->visit(this); }
    void Return::accept(Visitor* v) { v->visit(this); }
    void BinaryExpression::accept(Visitor* v) { v->visit(this); }
    void FieldDereference::accept(Visitor* v) { v->visit(this); }
    void Call::accept(Visitor* v) { v->visit(this); }
    void IntegerConstant::accept(Visitor* v) { v->visit(this); }
    void NoneConstant::accept(Visitor* v) { v->visit(this); }
    void Global::accept(Visitor* v) { v->visit(this); }
    void CallStatement::accept(Visitor* v) { v->visit(this); }
    void WhileLoop::accept(Visitor* v) { v->visit(this); }
    void FunctionDeclaration::accept(Visitor* v) { v->visit(this); }
    void UnaryExpression::accept(Visitor* v) { v->visit(this); }
    void IndexExpression::accept(Visitor* v) { v->visit(this); }
    void Record::accept(Visitor* v) { v->visit(this); }
    void StringConstant::accept(Visitor* v) { v->visit(this); }
    void BooleanConstant::accept(Visitor* v) { v->visit(this); }
    void Variable::accept(Visitor* v) { v->visit(this); }
}
