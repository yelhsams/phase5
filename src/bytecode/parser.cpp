#include "./parser.hpp"
#include "./instructions.hpp"
#include "./lexer.hpp"
#include "./types.hpp"
#include <cassert>
#include <optional>
#include <stdexcept>

bytecode::Parser::Parser(const std::vector<Token> &tokens)
    : tokens(tokens), pos(0) {}

bytecode::Function *bytecode::Parser::parse() {
  if (is_eof()) {
    throw std::runtime_error("Empty input");
  }

  auto function = parse_function();
  if (!function) {
    throw std::runtime_error("Failed to parse function");
  }

  if (!is_eof()) {
    const auto &current = peek();
    throw std::runtime_error(
        "Unexpected tokens after function definition at line " +
        std::to_string(current.start_line) + ", column " +
        std::to_string(current.start_col));
  }

  return function;
}

bool bytecode::Parser::is_eof() const {
  return pos >= tokens.size() ||
         (pos < tokens.size() && tokens[pos].kind == TokenKind::EOF_TOKEN);
}

bool bytecode::Parser::check(TokenKind kind) const {
  return pos < tokens.size() && tokens[pos].kind == kind;
}

const bytecode::Token &bytecode::Parser::peek() const {
  if (pos >= tokens.size()) {
    static const bytecode::Token eof_token(TokenKind::EOF_TOKEN, "", 0, 0, 0,
                                           0);
    return eof_token;
  }
  return tokens[pos];
}

const bytecode::Token &bytecode::Parser::previous() const {
  return tokens[pos - 1];
}

bytecode::Token bytecode::Parser::advance() {
  if (pos < tokens.size())
    pos++;
  return previous();
}

bool bytecode::Parser::match(std::initializer_list<TokenKind> kinds) {
  if (pos >= tokens.size())
    return false;
  const TokenKind current_kind = tokens[pos].kind;
  for (TokenKind kind : kinds) {
    if (current_kind == kind) {
      pos++;
      return true;
    }
  }
  return false;
}

bytecode::Token bytecode::Parser::consume(TokenKind kind,
                                          const std::string &message) {
  if (check(kind))
    return advance();

  const bytecode::Token &current = peek();
  throw std::runtime_error(
      message + " at line " + std::to_string(current.start_line) + ", column " +
      std::to_string(current.start_col) + " (token: '" + current.text + "')");
}

bytecode::Function *bytecode::Parser::parse_function() {
  consume(TokenKind::FUNCTION, "Expected 'function' keyword");
  consume(TokenKind::LBRACE, "Expected '{' after function");

  consume(TokenKind::FUNCTIONS, "Expected 'functions' keyword");
  consume(TokenKind::ASSIGN, "Expected '=' after 'functions'");
  consume(TokenKind::LBRACKET, "Expected '[' after 'functions ='");
  auto functions = parse_function_list_star();
  consume(TokenKind::RBRACKET, "Expected ']' after functions list");
  consume(TokenKind::COMMA, "Expected ',' after functions list");

  consume(TokenKind::CONSTANTS, "Expected 'constants' keyword");
  consume(TokenKind::ASSIGN, "Expected '=' after 'constants'");
  consume(TokenKind::LBRACKET, "Expected '[' after 'constants ='");
  auto constants = parse_constant_list_star();
  consume(TokenKind::RBRACKET, "Expected ']' after constants list");
  consume(TokenKind::COMMA, "Expected ',' after constants list");

  consume(TokenKind::PARAMETER_COUNT, "Expected 'parameter_count' keyword");
  consume(TokenKind::ASSIGN, "Expected '=' after 'parameter_count'");
  auto param_count_token =
      consume(TokenKind::INT, "Expected integer for parameter count");
  uint32_t param_count = fast_parse_uint(param_count_token.text);
  consume(TokenKind::COMMA, "Expected ',' after parameter count");

  consume(TokenKind::LOCAL_VARS, "Expected 'local_vars' keyword");
  consume(TokenKind::ASSIGN, "Expected '=' after 'local_vars'");
  consume(TokenKind::LBRACKET, "Expected '[' after 'local_vars ='");
  auto local_vars = parse_ident_list_star();
  consume(TokenKind::RBRACKET, "Expected ']' after local variables list");
  consume(TokenKind::COMMA, "Expected ',' after local variables list");

  consume(TokenKind::LOCAL_REF_VARS, "Expected 'local_ref_vars' keyword");
  consume(TokenKind::ASSIGN, "Expected '=' after 'local_ref_vars'");
  consume(TokenKind::LBRACKET, "Expected '[' after 'local_ref_vars ='");
  auto local_ref_vars = parse_ident_list_star();
  consume(TokenKind::RBRACKET,
          "Expected ']' after local reference variables list");
  consume(TokenKind::COMMA,
          "Expected ',' after local reference variables list");

  consume(TokenKind::FREE_VARS, "Expected 'free_vars' keyword");
  consume(TokenKind::ASSIGN, "Expected '=' after 'free_vars'");
  consume(TokenKind::LBRACKET, "Expected '[' after 'free_vars ='");
  auto free_vars = parse_ident_list_star();
  consume(TokenKind::RBRACKET, "Expected ']' after free variables list");
  consume(TokenKind::COMMA, "Expected ',' after free variables list");

  consume(TokenKind::NAMES, "Expected 'names' keyword");
  consume(TokenKind::ASSIGN, "Expected '=' after 'names'");
  consume(TokenKind::LBRACKET, "Expected '[' after 'names ='");
  auto names = parse_ident_list_star();
  consume(TokenKind::RBRACKET, "Expected ']' after names list");
  consume(TokenKind::COMMA, "Expected ',' after names list");

  consume(TokenKind::INSTRUCTIONS, "Expected 'instructions' keyword");
  consume(TokenKind::ASSIGN, "Expected '=' after 'instructions'");
  consume(TokenKind::LBRACKET, "Expected '[' after 'instructions ='");
  auto instructions = parse_instruction_list();
  consume(TokenKind::RBRACKET, "Expected ']' after instructions list");

  consume(TokenKind::RBRACE, "Expected '}' to end function");

  auto function = new bytecode::Function();
  function->functions_ = std::move(*functions);
  function->constants_ = std::move(*constants);
  function->parameter_count_ = param_count;
  function->local_vars_ = std::move(*local_vars);
  function->local_reference_vars_ = std::move(*local_ref_vars);
  function->free_vars_ = std::move(*free_vars);
  function->names_ = std::move(*names);
  function->instructions = std::move(*instructions);

  delete functions;
  delete constants;
  delete local_vars;
  delete local_ref_vars;
  delete free_vars;
  delete names;
  delete instructions;

  return function;
}

std::vector<bytecode::Function *> *
bytecode::Parser::parse_function_list_star() {
  if (check(TokenKind::RBRACKET)) {
    return new std::vector<bytecode::Function *>();
  }
  return parse_function_list_plus();
}

std::vector<bytecode::Function *> *
bytecode::Parser::parse_function_list_plus() {
  auto list = new std::vector<bytecode::Function *>();
  list->reserve(4); // Common case: small number of nested functions

  if (check(TokenKind::FUNCTION)) {
    auto func = parse_function();
    list->push_back(func);
  }

  while (match({TokenKind::COMMA})) {
    if (check(TokenKind::FUNCTION)) {
      auto func = parse_function();
      list->push_back(func);
    }
  }

  return list;
}

std::vector<std::string> *bytecode::Parser::parse_ident_list_star() {
  if (check(TokenKind::RBRACKET)) {
    return new std::vector<std::string>();
  }
  return parse_ident_list_plus();
}

std::vector<std::string> *bytecode::Parser::parse_ident_list_plus() {
  auto list = new std::vector<std::string>();
  list->reserve(8); // Common case: small number of identifiers

  // Optimized identifier check - check common non-identifier tokens first
  auto is_identifier_like = [](TokenKind kind) {
    // Fast path: check common non-identifier tokens first (most common case)
    if (kind == TokenKind::LBRACE || kind == TokenKind::RBRACE ||
        kind == TokenKind::LBRACKET || kind == TokenKind::RBRACKET ||
        kind == TokenKind::LPAREN || kind == TokenKind::RPAREN ||
        kind == TokenKind::COMMA || kind == TokenKind::ASSIGN ||
        kind == TokenKind::EOF_TOKEN || kind == TokenKind::INT ||
        kind == TokenKind::STRING) {
      return false;
    }
    return true; // keywords and instruction mnemonics count as identifiers here
  };

  auto consume_ident_like = [&](const std::string &message) -> std::string {
    if (pos >= tokens.size()) {
      throw std::runtime_error(message + " (unexpected EOF)");
    }
    const auto &current = tokens[pos];
    if (!is_identifier_like(current.kind)) {
      throw std::runtime_error(message + " at line " +
                               std::to_string(current.start_line) +
                               ", column " + std::to_string(current.start_col) +
                               " (token: '" + current.text + "')");
    }
    return tokens[pos++].text;
  };

  list->push_back(consume_ident_like("Expected identifier"));

  while (match({TokenKind::COMMA})) {
    if (check(TokenKind::RBRACKET))
      break; // allow trailing comma
    list->push_back(consume_ident_like("Expected identifier after comma"));
  }

  return list;
}

bytecode::Constant *bytecode::Parser::parse_constant() {
  if (match({TokenKind::NONE})) {
    return new bytecode::Constant::None();
  } else if (match({TokenKind::TRUE})) {
    return new bytecode::Constant::Boolean(true);
  } else if (match({TokenKind::FALSE})) {
    return new bytecode::Constant::Boolean(false);
  } else if (check(TokenKind::STRING)) {
    auto str_token = consume(TokenKind::STRING, "Expected string constant");
    return new bytecode::Constant::String(str_token.text);
  } else if (check(TokenKind::INT)) {
    auto int_token = consume(TokenKind::INT, "Expected integer constant");
    return new bytecode::Constant::Integer(fast_parse_int(int_token.text));
  } else {
    const auto &current = peek();
    throw std::runtime_error("Expected constant at line " +
                             std::to_string(current.start_line) + ", column " +
                             std::to_string(current.start_col));
  }
}

std::vector<bytecode::Constant *> *
bytecode::Parser::parse_constant_list_star() {
  if (check(TokenKind::RBRACKET)) {
    return new std::vector<bytecode::Constant *>();
  }
  return parse_constant_list_plus();
}

std::vector<bytecode::Constant *> *
bytecode::Parser::parse_constant_list_plus() {
  auto list = new std::vector<bytecode::Constant *>();
  list->reserve(8); // Common case: small number of constants

  auto constant = parse_constant();
  list->push_back(constant);

  while (match({TokenKind::COMMA})) {
    if (!check(TokenKind::RBRACKET)) {
      auto next_constant = parse_constant();
      list->push_back(next_constant);
    }
  }

  return list;
}

bytecode::Instruction bytecode::Parser::parse_instruction() {
  if (match({TokenKind::LOAD_CONST})) {
    auto operand =
        consume(TokenKind::INT, "Expected integer operand for load_const");
    return bytecode::Instruction(bytecode::Operation::LoadConst,
                                 fast_parse_int(operand.text));
  } else if (match({TokenKind::LOAD_FUNC})) {
    auto operand =
        consume(TokenKind::INT, "Expected integer operand for load_func");
    return bytecode::Instruction(bytecode::Operation::LoadFunc,
                                 fast_parse_int(operand.text));
  } else if (match({TokenKind::LOAD_LOCAL})) {
    auto operand =
        consume(TokenKind::INT, "Expected integer operand for load_local");
    return bytecode::Instruction(bytecode::Operation::LoadLocal,
                                 fast_parse_int(operand.text));
  } else if (match({TokenKind::STORE_LOCAL})) {
    auto operand =
        consume(TokenKind::INT, "Expected integer operand for store_local");
    return bytecode::Instruction(bytecode::Operation::StoreLocal,
                                 fast_parse_int(operand.text));
  } else if (match({TokenKind::LOAD_GLOBAL})) {
    auto operand =
        consume(TokenKind::INT, "Expected integer operand for load_global");
    return bytecode::Instruction(bytecode::Operation::LoadGlobal,
                                 fast_parse_int(operand.text));
  } else if (match({TokenKind::STORE_GLOBAL})) {
    auto operand =
        consume(TokenKind::INT, "Expected integer operand for store_global");
    return bytecode::Instruction(bytecode::Operation::StoreGlobal,
                                 fast_parse_int(operand.text));
  } else if (match({TokenKind::PUSH_REF})) {
    auto operand =
        consume(TokenKind::INT, "Expected integer operand for push_ref");
    return bytecode::Instruction(bytecode::Operation::PushReference,
                                 fast_parse_int(operand.text));
  } else if (match({TokenKind::LOAD_REF})) {
    return bytecode::Instruction(bytecode::Operation::LoadReference,
                                 std::nullopt);
  } else if (match({TokenKind::STORE_REF})) {
    return bytecode::Instruction(bytecode::Operation::StoreReference,
                                 std::nullopt);
  } else if (match({TokenKind::ALLOC_RECORD})) {
    return bytecode::Instruction(bytecode::Operation::AllocRecord,
                                 std::nullopt);
  } else if (match({TokenKind::FIELD_LOAD})) {
    auto operand =
        consume(TokenKind::INT, "Expected integer operand for field_load");
    return bytecode::Instruction(bytecode::Operation::FieldLoad,
                                 fast_parse_int(operand.text));
  } else if (match({TokenKind::FIELD_STORE})) {
    auto operand =
        consume(TokenKind::INT, "Expected integer operand for field_store");
    return bytecode::Instruction(bytecode::Operation::FieldStore,
                                 fast_parse_int(operand.text));
  } else if (match({TokenKind::INDEX_LOAD})) {
    return bytecode::Instruction(bytecode::Operation::IndexLoad, std::nullopt);
  } else if (match({TokenKind::INDEX_STORE})) {
    return bytecode::Instruction(bytecode::Operation::IndexStore, std::nullopt);
  } else if (match({TokenKind::ALLOC_CLOSURE})) {
    auto operand =
        consume(TokenKind::INT, "Expected integer operand for alloc_closure");
    return bytecode::Instruction(bytecode::Operation::AllocClosure,
                                 fast_parse_int(operand.text));
  } else if (match({TokenKind::CALL})) {
    auto operand = consume(TokenKind::INT, "Expected integer operand for call");
    return bytecode::Instruction(bytecode::Operation::Call,
                                 fast_parse_int(operand.text));
  } else if (match({TokenKind::RETURN})) {
    return bytecode::Instruction(bytecode::Operation::Return, std::nullopt);
  } else if (match({TokenKind::ADD})) {
    return bytecode::Instruction(bytecode::Operation::Add, std::nullopt);
  } else if (match({TokenKind::SUB})) {
    return bytecode::Instruction(bytecode::Operation::Sub, std::nullopt);
  } else if (match({TokenKind::MUL})) {
    return bytecode::Instruction(bytecode::Operation::Mul, std::nullopt);
  } else if (match({TokenKind::DIV})) {
    return bytecode::Instruction(bytecode::Operation::Div, std::nullopt);
  } else if (match({TokenKind::NEG})) {
    return bytecode::Instruction(bytecode::Operation::Neg, std::nullopt);
  } else if (match({TokenKind::GT})) {
    return bytecode::Instruction(bytecode::Operation::Gt, std::nullopt);
  } else if (match({TokenKind::GEQ})) {
    return bytecode::Instruction(bytecode::Operation::Geq, std::nullopt);
  } else if (match({TokenKind::EQ})) {
    return bytecode::Instruction(bytecode::Operation::Eq, std::nullopt);
  } else if (match({TokenKind::AND})) {
    return bytecode::Instruction(bytecode::Operation::And, std::nullopt);
  } else if (match({TokenKind::OR})) {
    return bytecode::Instruction(bytecode::Operation::Or, std::nullopt);
  } else if (match({TokenKind::NOT})) {
    return bytecode::Instruction(bytecode::Operation::Not, std::nullopt);
  } else if (match({TokenKind::GOTO})) {
    auto operand = consume(TokenKind::INT, "Expected integer operand for goto");
    return bytecode::Instruction(bytecode::Operation::Goto,
                                 fast_parse_int(operand.text));
  } else if (match({TokenKind::IF})) {
    auto operand = consume(TokenKind::INT, "Expected integer operand for if");
    return bytecode::Instruction(bytecode::Operation::If,
                                 fast_parse_int(operand.text));
  } else if (match({TokenKind::DUP})) {
    return bytecode::Instruction(bytecode::Operation::Dup, std::nullopt);
  } else if (match({TokenKind::SWAP})) {
    return bytecode::Instruction(bytecode::Operation::Swap, std::nullopt);
  } else if (match({TokenKind::POP})) {
    return bytecode::Instruction(bytecode::Operation::Pop, std::nullopt);
  } else {
    const auto &current = peek();
    throw std::runtime_error("Expected instruction at line " +
                             std::to_string(current.start_line) + ", column " +
                             std::to_string(current.start_col));
  }
}

std::vector<bytecode::Instruction> *bytecode::Parser::parse_instruction_list() {
  auto list = new std::vector<bytecode::Instruction>();
  list->reserve(32); // Common case: reasonable number of instructions

  while (pos < tokens.size() && tokens[pos].kind != TokenKind::RBRACKET) {
    auto instruction = parse_instruction();
    list->push_back(instruction);
  }

  return list;
}

int32_t bytecode::Parser::safe_cast(int64_t value) {
  int32_t new_value = static_cast<int32_t>(value);
  assert(new_value == value);
  return new_value;
}

uint32_t bytecode::Parser::safe_unsigned_cast(int64_t value) {
  uint32_t new_value = static_cast<uint32_t>(value);
  assert(new_value == value);
  return new_value;
}

int32_t bytecode::Parser::fast_parse_int(const std::string &str) {
  const char *p = str.c_str();
  bool negative = false;
  if (*p == '-') {
    negative = true;
    ++p;
  }
  int32_t result = 0;
  while (*p >= '0' && *p <= '9') {
    result = result * 10 + (*p - '0');
    ++p;
  }
  return negative ? -result : result;
}

uint32_t bytecode::Parser::fast_parse_uint(const std::string &str) {
  const char *p = str.c_str();
  uint32_t result = 0;
  while (*p >= '0' && *p <= '9') {
    result = result * 10 + (*p - '0');
    ++p;
  }
  return result;
}

bytecode::Function *bytecode::parse(const std::string &contents) {
  auto tokens = bytecode::lex(contents);
  auto parser = bytecode::Parser(tokens);
  return parser.parse();
}
