#pragma once

#include "./token.hpp"
#include "./types.hpp"
#include <vector>

namespace bytecode {

class Parser {
public:
  Parser(const std::vector<Token> &tokens);

  Function *parse();

private:
  std::vector<Token> tokens;
  size_t pos;

  bool is_eof() const;
  bool check(TokenKind kind) const;
  const Token &peek() const;
  const Token &previous() const;
  Token advance();
  bool match(std::initializer_list<TokenKind> kinds);
  Token consume(TokenKind kind, const std::string &message);

  Function *parse_function();
  std::vector<Function *> *parse_function_list_star();
  std::vector<Function *> *parse_function_list_plus();
  std::vector<std::string> *parse_ident_list_star();
  std::vector<std::string> *parse_ident_list_plus();
  Constant *parse_constant();
  std::vector<Constant *> *parse_constant_list_star();
  std::vector<Constant *> *parse_constant_list_plus();
  Instruction parse_instruction();
  std::vector<Instruction> *parse_instruction_list();

  int32_t safe_cast(int64_t value);
  uint32_t safe_unsigned_cast(int64_t value);

  // Fast integer parsing - avoids std::stoi overhead
  static int32_t fast_parse_int(const std::string &str);
  static uint32_t fast_parse_uint(const std::string &str);
};

Function *parse(const std::string &contents);

} // namespace bytecode
