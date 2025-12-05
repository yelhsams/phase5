#include "./lexer.hpp"
#include "./token.hpp"

#include <cctype>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

static std::vector<std::tuple<std::string, bytecode::TokenKind>>
    keyword_to_token{{"None", bytecode::TokenKind::NONE},
                     {"true", bytecode::TokenKind::TRUE},
                     {"false", bytecode::TokenKind::FALSE},
                     {"function", bytecode::TokenKind::FUNCTION},
                     {"functions", bytecode::TokenKind::FUNCTIONS},
                     {"constants", bytecode::TokenKind::CONSTANTS},
                     {"parameter_count", bytecode::TokenKind::PARAMETER_COUNT},
                     {"local_vars", bytecode::TokenKind::LOCAL_VARS},
                     {"local_ref_vars", bytecode::TokenKind::LOCAL_REF_VARS},
                     {"names", bytecode::TokenKind::NAMES},
                     {"free_vars", bytecode::TokenKind::FREE_VARS},
                     {"instructions", bytecode::TokenKind::INSTRUCTIONS},
                     {"load_const", bytecode::TokenKind::LOAD_CONST},
                     {"load_func", bytecode::TokenKind::LOAD_FUNC},
                     {"load_local", bytecode::TokenKind::LOAD_LOCAL},
                     {"store_local", bytecode::TokenKind::STORE_LOCAL},
                     {"load_global", bytecode::TokenKind::LOAD_GLOBAL},
                     {"store_global", bytecode::TokenKind::STORE_GLOBAL},
                     {"push_ref", bytecode::TokenKind::PUSH_REF},
                     {"load_ref", bytecode::TokenKind::LOAD_REF},
                     {"store_ref", bytecode::TokenKind::STORE_REF},
                     {"alloc_record", bytecode::TokenKind::ALLOC_RECORD},
                     {"field_load", bytecode::TokenKind::FIELD_LOAD},
                     {"field_store", bytecode::TokenKind::FIELD_STORE},
                     {"index_load", bytecode::TokenKind::INDEX_LOAD},
                     {"index_store", bytecode::TokenKind::INDEX_STORE},
                     {"alloc_closure", bytecode::TokenKind::ALLOC_CLOSURE},
                     {"call", bytecode::TokenKind::CALL},
                     {"return", bytecode::TokenKind::RETURN},
                     {"add", bytecode::TokenKind::ADD},
                     {"sub", bytecode::TokenKind::SUB},
                     {"mul", bytecode::TokenKind::MUL},
                     {"div", bytecode::TokenKind::DIV},
                     {"neg", bytecode::TokenKind::NEG},
                     {"gt", bytecode::TokenKind::GT},
                     {"geq", bytecode::TokenKind::GEQ},
                     {"eq", bytecode::TokenKind::EQ},
                     {"and", bytecode::TokenKind::AND},
                     {"or", bytecode::TokenKind::OR},
                     {"not", bytecode::TokenKind::NOT},
                     {"goto", bytecode::TokenKind::GOTO},
                     {"if", bytecode::TokenKind::IF},
                     {"dup", bytecode::TokenKind::DUP},
                     {"swap", bytecode::TokenKind::SWAP},
                     {"pop", bytecode::TokenKind::POP}};

static std::vector<std::tuple<std::string, bytecode::TokenKind>>
    symbol_to_token{
        {"[", bytecode::TokenKind::LBRACKET},
        {"]", bytecode::TokenKind::RBRACKET},
        {"(", bytecode::TokenKind::LPAREN},
        {")", bytecode::TokenKind::RPAREN},
        {"{", bytecode::TokenKind::LBRACE},
        {"}", bytecode::TokenKind::RBRACE},
        {"=", bytecode::TokenKind::ASSIGN},
        {",", bytecode::TokenKind::COMMA},
    };

static int is_comment(int c) { return c != '\r' && c != '\n'; }

static int is_identifier_start(int c) { return std::isalpha(c) || c == '_'; }

static int is_identifier_continue(int c) { return std::isalnum(c) || c == '_'; }

bytecode::Lexer::Lexer(const std::string &file_contents)
    : rest(file_contents), current_line(1), current_col(1) {}

std::vector<bytecode::Token> bytecode::Lexer::lex() {
  std::vector<bytecode::Token> result;
  while (!is_eof()) {
    if (lex_comment())
      continue;
    if (auto token = lex_symbol()) {
      result.push_back(*token);
      continue;
    }
    if (auto token = lex_intliteral()) {
      result.push_back(*token);
      continue;
    }
    if (auto token = lex_identifier_or_keyword()) {
      result.push_back(*token);
      continue;
    }
    if (auto token = lex_stringliteral()) {
      result.push_back(*token);
      continue;
    }

    if (lex_whitespace())
      continue;

    throw std::runtime_error("Error: Unexpected character '" +
                             std::string(1, peek()) + "' at line " +
                             std::to_string(current_line) + ", column " +
                             std::to_string(current_col));
  }
  result.emplace_back(bytecode::TokenKind::EOF_TOKEN, "", current_line,
                      current_col, current_line, current_col);
  return result;
}

bool bytecode::Lexer::lex_whitespace() {
  if (!is_eof() && std::isspace(peek())) {
    consume(1);
    return true;
  }
  return false;
}

bool bytecode::Lexer::lex_comment() {
  if (this->rest.substr(0, 2) == "//") {
    std::string text = take_while<is_comment>();
    consume(text.length());
    return true;
  }
  return false;
}

std::optional<bytecode::Token> bytecode::Lexer::lex_symbol() {
  for (const auto &[symbol, token] : symbol_to_token) {
    if (this->rest.substr(0, symbol.length()) == symbol) {
      auto start_line = this->current_line;
      auto start_col = this->current_col;
      std::string text = consume(symbol.length());
      return bytecode::Token(token, text, start_line, start_col,
                             this->current_line, this->current_col);
    }
  }
  return std::nullopt;
}

std::optional<bytecode::Token> bytecode::Lexer::lex_intliteral() {
  if (!is_eof() && (std::isdigit(peek()) || peek() == '-')) {
    auto start_line = this->current_line;
    auto start_col = this->current_col;
    std::string text;
    if (peek() == '-') {
      text = consume(1);
    }
    if (!is_eof() && std::isdigit(peek())) {
      std::string to_add = take_while<std::isdigit>();
      consume(to_add.length());
      text += to_add;
      return bytecode::Token(bytecode::TokenKind::INT, text, start_line,
                             start_col, this->current_line, this->current_col);
    } else if (text == "-") {
      this->rest = "-" + this->rest;
      this->current_col--;
    }
  }
  return std::nullopt;
}

std::optional<bytecode::Token> bytecode::Lexer::lex_identifier_or_keyword() {
  if (!is_eof() && is_identifier_start(peek())) {
    auto start_line = this->current_line;
    auto start_col = this->current_col;
    std::string text = take_while<is_identifier_continue>();
    consume(text.length());
    bytecode::TokenKind token_kind = bytecode::TokenKind::IDENTIFIER;
    for (const auto &[keyword, keyword_token] : keyword_to_token) {
      if (text == keyword) {
        token_kind = keyword_token;
        break;
      }
    }
    return bytecode::Token(token_kind, text, start_line, start_col,
                           this->current_line, this->current_col);
  }
  return std::nullopt;
}

std::optional<bytecode::Token> bytecode::Lexer::lex_stringliteral() {
  if (!is_eof() && peek() == '"') {
    auto start_line = this->current_line;
    auto start_col = this->current_col;
    std::ostringstream oss;
    oss << consume(1);
    while (!is_eof() && peek() != '"') {
      if (peek() == '\\') {
        oss << consume(1);
        if (!is_eof()) {
          char escaped = peek();
          if (escaped == '\\' || escaped == 'n' || escaped == 't' ||
              escaped == '"') {
            oss << consume(1);
          } else {
            throw std::runtime_error("Error: Invalid escape sequence '\\" +
                                     std::string(1, escaped) + "' at line " +
                                     std::to_string(current_line) +
                                     ", column " + std::to_string(current_col));
          }
        }
      } else {
        oss << consume(1);
      }
    }

    if (is_eof()) {
      throw std::runtime_error("Error: Unterminated string literal at line " +
                               std::to_string(current_line) + ", column " +
                               std::to_string(current_col));
    } else if (peek() == '"') {
      oss << consume(1);
    }

    std::string text = oss.str();

    std::string processed_text =
        escape_string(text.substr(1, text.length() - 2));

    return bytecode::Token(bytecode::TokenKind::STRING, processed_text,
                           start_line, start_col, this->current_line,
                           this->current_col);
  }
  return std::nullopt;
}

bool bytecode::Lexer::is_eof() const { return this->rest.empty(); }

char bytecode::Lexer::peek() const { return this->rest[0]; }

std::string bytecode::Lexer::consume(int n) {
  for (size_t i = 0; i < (size_t)n; i++) {
    if (i >= this->rest.length())
      break;

    if (this->rest[i] == '\n') {
      this->current_col = 1;
      this->current_line += 1;
    } else {
      this->current_col += 1;
    }
  }

  std::string result = this->rest.substr(0, n);
  this->rest = this->rest.substr(n);
  return result;
}

template <int (*predicate)(int)>
std::string bytecode::Lexer::take_while() const {
  size_t i = 0;
  while (i < this->rest.length() && predicate(this->rest[i])) {
    i++;
  }
  return this->rest.substr(0, i);
}

std::string bytecode::Lexer::escape_string(const std::string &str) {
  std::string result;
  for (size_t i = 0; i < str.length(); i++) {
    if (str[i] == '\\' && i + 1 < str.length()) {
      switch (str[i + 1]) {
      case '\\':
        result += '\\';
        break;
      case '"':
        result += '"';
        break;
      case 'n':
        result += '\n';
        break;
      case 't':
        result += '\t';
        break;
      default:
        result += str[i + 1];
        break;
      }
      i++; // Skip the next character
    } else {
      result += str[i];
    }
  }
  return result;
}

std::vector<bytecode::Token> bytecode::lex(const std::string &contents) {
  return bytecode::Lexer(contents).lex();
}
