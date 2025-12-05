#pragma once
#include <string>
#include <vector>
#include <variant>
#include <memory>

namespace mitscript {
    class AST;
    class Visitor;

    class Block;
    class Assignment;
    class IfStatement;
    class Return;
    class BinaryExpression;
    class FieldDereference;
    class Call;
    class IntegerConstant;
    class NoneConstant;
    class Global;
    class CallStatement;
    class WhileLoop;
    class FunctionDeclaration;
    class UnaryExpression;
    class IndexExpression;
    class Record;
    class StringConstant;
    class BooleanConstant;

    struct SourceSpan {
        int start_line;
        int start_col;
        int end_line;
        int end_col;
    };

    struct Node {
        SourceSpan span;
        virtual void accept(Visitor* visitor) = 0;
        virtual ~Node() {}
    };

    struct Statement: Node {
        virtual void accept(Visitor* visitor) = 0;
    };

    struct Expression: Node {
        virtual void accept(Visitor* visitor) = 0;
    };

    struct Variable: public Expression {
        std::string name;

        void accept(Visitor* visitor) override;
    };

    class Visitor {
        public:
            virtual void visit(AST* node) = 0;
            virtual void visit(Block* node) = 0;
            virtual void visit(Assignment* node) = 0;
            virtual void visit(IfStatement* node) = 0;
            virtual void visit(Return* node) = 0;
            virtual void visit(BinaryExpression* node) = 0;
            virtual void visit(FieldDereference* node) = 0;
            virtual void visit(Call* node) = 0;
            virtual void visit(IntegerConstant* node) = 0;
            virtual void visit(NoneConstant* node) = 0;
            virtual void visit(Global* node) = 0;
            virtual void visit(CallStatement* node) = 0;
            virtual void visit(WhileLoop* node) = 0;
            virtual void visit(FunctionDeclaration* node) = 0;
            virtual void visit(UnaryExpression* node) = 0;
            virtual void visit(IndexExpression* node) = 0;
            virtual void visit(Record* node) = 0;
            virtual void visit(StringConstant* node) = 0;
            virtual void visit(BooleanConstant* node) = 0;
            virtual void visit(Variable* node) = 0;

            virtual ~Visitor() {}
    };

    enum class BinOp {ADD, SUB, MUL, DIV, EQ, LT, LTE, GT, GTE, AND, OR};
    enum class UnOp {NEG, NOT};


    class AST : public Node{
        public:
            std::vector<std::unique_ptr<Statement>> statements;
            void accept(Visitor* visitor) override;
    };

    class Block : public Statement{
        public:
            std::vector<std::unique_ptr<Statement>> statements;

            void accept(Visitor* visitor) override;
    };

    class Assignment : public Statement{
        public:
            std::unique_ptr<Expression> target;
            std::unique_ptr<Expression> value;

            void accept(Visitor* visitor) override;
    };

    class IfStatement : public Statement{
        public:
            std::unique_ptr<Expression> condition;
            std::unique_ptr<Block> then_block;
            std::unique_ptr<Block> else_block; // can be nullptr

            void accept(Visitor* visitor) override;
    };

    class Return : public Statement{
        public:
            std::unique_ptr<Expression> value;

            void accept(Visitor* visitor) override;
    };

    class BinaryExpression : public Expression {
        public:
            BinOp op;
            std::unique_ptr<Expression> left;
            std::unique_ptr<Expression> right;

            void accept(Visitor* visitor) override;
    };

    class FieldDereference : public Expression{
        public:
            std::unique_ptr<Expression> object;
            std::string field_name;

            void accept(Visitor* visitor) override;
    };

    class Call : public Expression {
        public:
            std::unique_ptr<Expression> callee;
            std::vector<std::unique_ptr<Expression>> arguments;

            void accept(Visitor* visitor) override;
    };

    class IntegerConstant : public Expression {
        public:
            int value;

            void accept(Visitor* visitor) override;
    };

    class NoneConstant : public Expression{
        public:

            void accept(Visitor* visitor) override;
    };

    class Global : public Statement {
        public:
            std::string name;

            void accept(Visitor* visitor) override;
    };

    class CallStatement : public Statement {
        public:
            std::unique_ptr<Call> call;

            void accept(Visitor* visitor) override;
    };

    class WhileLoop : public Statement{
        public:
            std::unique_ptr<Expression> condition;
            std::unique_ptr<Block> body;

            void accept(Visitor* visitor) override;
    };

    class FunctionDeclaration : public Expression{
        public:
            std::string name;
            std::vector<std::string> args;
            std::unique_ptr<Block> body;

            void accept(Visitor* visitor) override;
    };

    class UnaryExpression : public Expression {
        public:
            UnOp op;
            std::unique_ptr<Expression> operand;

        void accept(Visitor* visitor) override;
    };

    class IndexExpression : public Expression {
        public:
            std::unique_ptr<Expression> baseExpression;
            std::unique_ptr<Expression> indexExpression;

        void accept(Visitor* visitor) override;
    };

    class Record : public Expression {
        public:
            std::vector<std::pair<std::string, std::unique_ptr<Expression>>> fields;

        void accept(Visitor* visitor) override;
    };

    class StringConstant : public Expression {
        public:
            std::string value;

        void accept(Visitor* visitor) override;
    };

    class BooleanConstant : public Expression {
        public:
            bool value;

        void accept(Visitor* visitor) override;
    };
}
