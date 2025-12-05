#pragma once
#include "./token.hpp"
#include "./ast.hpp"

namespace mitscript {
    class Parser {
        public:
            Parser(const std::vector<Token> &tokens);
            std::unique_ptr<AST> parse();
        private:
            const std::vector<Token>& tokens;
            size_t current{0};

            // ------------------------
            // Token utilities
            // ------------------------
            bool is_eof() const;
            const Token& peek(int offset = 0) const;   // lookahead
            const Token& peek() const { return peek(0); }
            const Token& previous() const;             // last consumed
            void advance();                            // consume one
            bool check(TokenKind kind, int offset = 0) const;
            bool check(TokenKind kind) const { return check(kind, 0); }
            bool match(TokenKind kind);                // if matches, consume and return true
            const Token& expect(TokenKind kind, const std::string& error_message);

            // ------------------------
            // Top-level / statements
            // ------------------------
            std::unique_ptr<Statement>     parse_statement();
            std::unique_ptr<Block>         parse_block();               // { statement* }
            std::unique_ptr<Assignment>    parse_assignment();          // location '=' expr ';'
            std::unique_ptr<IfStatement>   parse_if_statement();        // if (...) block [else block]
            std::unique_ptr<WhileLoop>     parse_while_loop();          // while (...) block
            std::unique_ptr<Return>        parse_return_statement();    // return expr ';'
            std::unique_ptr<Global>        parse_global_statement();    // global name ';'
            std::unique_ptr<CallStatement> parse_call_statement();      // call ';'

            // ------------------------
            // Expressions (precedence)
            // ------------------------
            std::unique_ptr<Expression> parse_expression();                 // entry
            std::unique_ptr<FunctionDeclaration> parse_function_declaration(); // if you keep fun as a node
            std::unique_ptr<Expression> parse_binary_expression(int min_precedence = 0);
            std::unique_ptr<Expression> parse_unary_expression();           // '!' | '-' | postfix
            std::unique_ptr<Expression> parse_postfix_expression();         // call/field/index chaining
            std::unique_ptr<Expression> parse_primary_expression();         // literals, identifiers, parens, fun-literal, record
            std::unique_ptr<Expression> parse_location();
            std::unique_ptr<Expression> parse_call_from_location(std::unique_ptr<Expression> loc);
            // Additional helpers used by an alternate precedence-climbing path
            std::unique_ptr<Expression> parse_simple_atom();
            std::unique_ptr<Expression> parse_simple_expression();
            std::unique_ptr<Expression> parse_binary_rhs(int min_prec);
            // (Optional) helpers for argument/field/record parsing
            std::vector<std::unique_ptr<Expression>> parse_argument_list(); // ( ... )

            // ------------------------
            // Operator helpers
            // ------------------------
            bool is_binary_op(TokenKind kind) const;
            int  precedence_of(TokenKind kind) const;   // larger = tighter binding

            // ------------------------
            // Error handling
            // ------------------------
            [[noreturn]] void error_here(const std::string& message) const; // throw/report using peek()
            void synchronize(); // skip to a likely statement boundary (;, }, EOF)



    };

}
