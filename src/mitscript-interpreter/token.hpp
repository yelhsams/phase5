#pragma once

#include <string>

namespace mitscript {

enum class TokenKind {

    // Literals
    INT,
    STRING,
    IDENTIFIER, // [A-Za-z_][A-Za-z0-9_]*
    EOF_TOKEN,

    // Keywords
    GLOBAL,
    IF,
    ELSE,
    WHILE,
    RETURN,
    FUN,
    TRUE,
    FALSE,
    NONE,

    // Punctuation & Delimiters
    LBRACE, RBRACE, // { }
    LBRACKET, RBRACKET, // [ ]
    LPAREN, RPAREN, // ( )
    COMMA, // ,
    COLON, // :
    SEMICOLON, // ;
    DOT, // .
    ASSIGN, // =
    // Operators
    MULT, // *
    DIV, // /
    ADD, // +
    SUB, // -
    LT, // <
    LE, // <=
    GE, // >=
    GT, // >
    EQEQ, // ==
    BANG, // !
    AMP, // &
    BAR, // |

    //ERROR Token
    ERROR


};

struct Token {
    TokenKind kind;
    std::string text;
    int start_line;
    int start_col;
    int end_line;
    int end_col;

    Token(TokenKind k, std::string text, int sl, int sc, int el, int ec)
        : kind(k), text(std::move(text)), start_line(sl), start_col(sc), end_line(el), end_col(ec) {}
};
};
