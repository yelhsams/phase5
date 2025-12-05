#pragma once

#include "./token.hpp"
#include <optional>
#include <string>
#include <vector>

namespace bytecode {

class Lexer {
public:
  Lexer(const std::string &file_contents);

  std::vector<Token> lex();

private:
  std::string rest;
  int current_line;
  int current_col;

  bool is_eof() const;
  char peek() const;
  std::string consume(int n);
  template <int (*predicate)(int)> std::string take_while() const;

  bool lex_whitespace();
  bool lex_comment();
  std::optional<Token> lex_symbol();
  std::optional<Token> lex_intliteral();
  std::optional<Token> lex_identifier_or_keyword();
  std::optional<Token> lex_stringliteral();
  std::string escape_string(const std::string &str);
};

std::vector<Token> lex(const std::string &contents);

} // namespace bytecode
