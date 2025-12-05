#pragma once

#include <string>

namespace bytecode {

enum class TokenKind {
  // Literals
  INT,
  STRING,
  IDENTIFIER,

  // Keywords
  NONE,
  TRUE,
  FALSE,
  FUNCTION,
  FUNCTIONS,
  CONSTANTS,
  PARAMETER_COUNT,
  LOCAL_VARS,
  LOCAL_REF_VARS,
  FREE_VARS,
  NAMES,
  INSTRUCTIONS,

  // Bytecode instruction mnemonics
  LOAD_CONST,
  LOAD_FUNC,
  LOAD_LOCAL,
  STORE_LOCAL,
  LOAD_GLOBAL,
  STORE_GLOBAL,
  PUSH_REF,
  LOAD_REF,
  STORE_REF,
  ALLOC_RECORD,
  FIELD_LOAD,
  FIELD_STORE,
  INDEX_LOAD,
  INDEX_STORE,
  ALLOC_CLOSURE,
  CALL,
  RETURN,
  ADD,
  SUB,
  MUL,
  DIV,
  NEG,
  GT,
  GEQ,
  EQ,
  AND,
  OR,
  NOT,
  GOTO,
  IF,
  DUP,
  SWAP,
  POP,

  LBRACE,
  RBRACE,
  LBRACKET,
  RBRACKET,
  LPAREN,
  RPAREN,
  COMMA,
  ASSIGN,

  EOF_TOKEN
};

struct Token {
  TokenKind kind;
  std::string text;
  int start_line;
  int start_col;
  int end_line;
  int end_col;

  Token(TokenKind k, std::string t, int sl, int sc, int el, int ec)
    : kind(k), text(std::move(t)), start_line(sl), start_col(sc), end_line(el), end_col(ec) {}
};

} // namespace bytecode