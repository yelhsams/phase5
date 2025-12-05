#include "./parser.hpp"
#include "./ast.hpp"
#include "./token.hpp"
#include <stdexcept>

using mitscript::Token;

mitscript::Parser::Parser(const std::vector<Token> &tokens)
    : tokens(tokens), current(0) {}

// Define member operator helpers (replacing previous free static helpers)
bool mitscript::Parser::is_binary_op(mitscript::TokenKind k) const {
  using TK = mitscript::TokenKind;
  switch (k) {
  case TK::ADD:
  case TK::SUB:
  case TK::MULT:
  case TK::DIV:
  case TK::LT:
  case TK::LE:
  case TK::GE:
  case TK::GT:
  case TK::EQEQ:
  case TK::AMP:
  case TK::BAR:
    return true;
  default:
    return false;
  }
}

int mitscript::Parser::precedence_of(mitscript::TokenKind k) const {
  using TK = mitscript::TokenKind;
  // higher = tighter
  switch (k) {
  case TK::BAR:
    return 1; // |
  case TK::AMP:
    return 2; // &
  // Unary '!' should bind tighter than '|'/'&' but looser than
  // comparisons/equality.
  case TK::BANG:
    return 3; // ! (used for guiding unary parse)
  case TK::EQEQ:
    return 4; // ==
  case TK::LT:
  case TK::LE:
  case TK::GE:
  case TK::GT:
    return 4; // < <= >= >
  case TK::ADD:
  case TK::SUB:
    return 5; // + -
  case TK::MULT:
  case TK::DIV:
    return 6; // * /
  default:
    return -1;
  }
}

static inline mitscript::BinOp token_to_binop(mitscript::TokenKind k) {
  using TK = mitscript::TokenKind;
  using BO = mitscript::BinOp;
  switch (k) {
  case TK::ADD:
    return BO::ADD;
  case TK::SUB:
    return BO::SUB;
  case TK::MULT:
    return BO::MUL;
  case TK::DIV:
    return BO::DIV;
  case TK::LT:
    return BO::LT;
  case TK::LE:
    return BO::LTE;
  case TK::GE:
    return BO::GTE;
  case TK::GT:
    return BO::GT;
  case TK::EQEQ:
    return BO::EQ;
  case TK::AMP:
    return BO::AND;
  case TK::BAR:
    return BO::OR;
  default:
    return BO::ADD; // unreachable
  }
}

const mitscript::Token &mitscript::Parser::previous() const {
  return tokens[current - 1];
}

void mitscript::Parser::advance() {
  if (!is_eof())
    current++;
}

const mitscript::Token &mitscript::Parser::expect(TokenKind kind,
                                                  const std::string &msg) {
  if (!check(kind, 0)) {
    error_here(msg);
  }

  const auto &t = peek(0);

  advance();
  return t;
}

[[noreturn]] void mitscript::Parser::error_here(const std::string &msg) const {
  const auto &t = peek(0);
  throw std::runtime_error("Parser error at line " +
                           std::to_string(t.start_line) + ", col " +
                           std::to_string(t.start_col) + ": " + msg);
}

bool mitscript::Parser::is_eof() const {
  return current >= tokens.size() || peek(0).kind == TokenKind::EOF_TOKEN;
}

const mitscript::Token &mitscript::Parser::peek(int offset) const {
  if (current + offset >= tokens.size()) {
    static mitscript::Token eof_token(TokenKind::EOF_TOKEN, "", 0, 0, 0, 0);
    return eof_token;
  }
  return tokens[current + offset];
}

bool mitscript::Parser::check(TokenKind kind, int offset) const {
  if (is_eof()) {
    return false;
  }
  return peek(offset).kind == kind;
}

bool mitscript::Parser::match(TokenKind kind) {
  if (check(kind, 0)) {
    advance();
    return true;
  }
  return false;
}

std::unique_ptr<mitscript::AST> mitscript::Parser::parse() {
  auto root = std::make_unique<mitscript::AST>();
  while (!is_eof()) {
    auto statement = parse_statement();
    if (!statement) {
      if (!is_eof()) {
        error_here("Expected a statement");
      }
      break;
    }
    root->statements.push_back(std::move(statement));
  }
  return root;
}

std::unique_ptr<mitscript::Statement> mitscript::Parser::parse_statement() {
  if (check(TokenKind::LBRACE, 0))
    return parse_block();
  if (check(TokenKind::IF, 0))
    return parse_if_statement();
  if (check(TokenKind::WHILE, 0))
    return parse_while_loop();
  if (check(TokenKind::RETURN, 0))
    return parse_return_statement();
  if (check(TokenKind::GLOBAL, 0))
    return parse_global_statement();

  // Statements that start with a location (assign/call)
  if (!check(TokenKind::IDENTIFIER, 0)) {
    error_here("Expected a statement");
  }

  // parse the leading location
  auto loc = parse_location();

  if (match(TokenKind::ASSIGN)) {
    auto rhs = parse_expression();
    expect(TokenKind::SEMICOLON, "Expected ';' after assignment");
    auto asn = std::make_unique<mitscript::Assignment>();
    asn->target = std::move(loc);
    asn->value = std::move(rhs);
    return asn;
  }

  if (check(TokenKind::LPAREN, 0)) {
    // Support chained calls like b()() by repeatedly parsing call postfixes
    std::unique_ptr<mitscript::Expression> expr = std::move(loc);
    while (check(TokenKind::LPAREN, 0)) {
      expr = parse_call_from_location(std::move(expr));
    }
    expect(TokenKind::SEMICOLON, "Expected ';' after function call");
    auto cs = std::make_unique<mitscript::CallStatement>();
    auto *callPtr = dynamic_cast<mitscript::Call *>(expr.release());
    if (!callPtr) {
      error_here("Expected function call");
    }
    cs->call.reset(callPtr);
    return cs;
  }

  error_here("Expected assignment or function call statement");
}

std::unique_ptr<mitscript::Block> mitscript::Parser::parse_block() {
  expect(TokenKind::LBRACE, "Expected '{' to start block");
  auto block = std::make_unique<mitscript::Block>();

  while (!check(TokenKind::RBRACE, 0) && !is_eof()) {
    auto statement = parse_statement();
    if (statement) {
      block->statements.push_back(std::move(statement));
    }
  }

  expect(TokenKind::RBRACE, "Expected '}' to end block");
  return block;
}

std::unique_ptr<mitscript::IfStatement>
mitscript::Parser::parse_if_statement() {
  expect(TokenKind::IF, "Expected 'IF' to start an If statement");

  expect(TokenKind::LPAREN, "Expected opening paren, '(' after If");
  auto condition = parse_expression();
  expect(TokenKind::RPAREN,
         "Expected closing paren for If statement condition");

  auto then_block = parse_block();

  std::unique_ptr<mitscript::Block> else_block;
  if (match(TokenKind::ELSE)) {
    else_block = parse_block();
  }

  auto astNode = std::make_unique<mitscript::IfStatement>();
  astNode->condition = std::move(condition);
  astNode->then_block = std::move(then_block);
  astNode->else_block = std::move(else_block);

  return astNode;
}

std::unique_ptr<mitscript::WhileLoop> mitscript::Parser::parse_while_loop() {
  expect(TokenKind::WHILE, "Expected 'While' to start a WHILE loop");
  expect(TokenKind::LPAREN, "Expected opening paren '(' after While");

  auto condition = parse_expression();
  expect(TokenKind::RPAREN, "Expected closing paren ')' after while condition");

  auto body = parse_block();

  auto astNode = std::make_unique<mitscript::WhileLoop>();
  astNode->condition = std::move(condition);
  astNode->body = std::move(body);

  return astNode;
}

std::unique_ptr<mitscript::Return> mitscript::Parser::parse_return_statement() {
  expect(TokenKind::RETURN, "Expected Return for a return statement");

  auto return_value = parse_expression();

  expect(TokenKind::SEMICOLON, "Expected ';' after return");

  auto astNode = std::make_unique<mitscript::Return>();
  astNode->value = std::move(return_value);

  return astNode;
}

std::unique_ptr<mitscript::Global> mitscript::Parser::parse_global_statement() {
  expect(TokenKind::GLOBAL, "Expected Global for a global statement");

  auto token =
      expect(TokenKind::IDENTIFIER, "Expected identifier after 'Global'");

  expect(TokenKind::SEMICOLON, "Expected ';' after global");

  auto globalNode = std::make_unique<mitscript::Global>();

  globalNode->name = token.text;

  return globalNode;
}

std::unique_ptr<mitscript::Expression> mitscript::Parser::parse_expression() {
  if (match(TokenKind::FUN)) {
    expect(TokenKind::LPAREN, "Expected '(' after 'fun'");
    std::vector<std::string> params;
    if (!check(TokenKind::RPAREN, 0)) {
      const auto &id0 =
          expect(TokenKind::IDENTIFIER, "Expected parameter name");
      params.push_back(id0.text);
      while (match(TokenKind::COMMA)) {
        if (check(TokenKind::RPAREN, 0))
          break;
        const auto &id =
            expect(TokenKind::IDENTIFIER, "Expected parameter name");
        params.push_back(id.text);
      }
    }
    expect(TokenKind::RPAREN, "Expected ')' after parameters");
    auto body = parse_block();
    auto fn = std::make_unique<mitscript::FunctionDeclaration>();
    fn->name = "";
    fn->args = std::move(params);
    fn->body = std::move(body);
    return fn;
  }

  if (match(TokenKind::LBRACE)) {
    auto rec = std::make_unique<mitscript::Record>();
    while (!check(TokenKind::RBRACE, 0)) {
      const auto &key = expect(TokenKind::IDENTIFIER, "Expected field name");
      expect(TokenKind::COLON, "Expected ':' after field name");
      auto val = parse_expression();
      expect(TokenKind::SEMICOLON, "Expected ';' after record field");
      rec->fields.emplace_back(key.text, std::move(val));
    }
    expect(TokenKind::RBRACE, "Expected '}' to close record");
    return rec;
  }

  // Use precedence-climbing parser for expressions
  return parse_binary_expression(1);
}

std::unique_ptr<mitscript::Expression>
mitscript::Parser::parse_simple_expression() {
  auto lhs = parse_simple_atom();
  while (is_binary_op(peek(0).kind)) {
    auto opk = peek(0).kind;
    int prec = precedence_of(opk);
    advance();
    auto rhs = parse_binary_rhs(prec + 1);
    auto b = std::make_unique<mitscript::BinaryExpression>();
    b->op = token_to_binop(opk);
    b->left = std::move(lhs);
    b->right = std::move(rhs);
    lhs = std::move(b);
  }
  return lhs;
}

std::unique_ptr<mitscript::Expression>
mitscript::Parser::parse_binary_rhs(int min_prec) {
  auto lhs = parse_simple_atom();
  while (is_binary_op(peek(0).kind) &&
         precedence_of(peek(0).kind) >= min_prec) {
    auto opk = peek(0).kind;
    int prec = precedence_of(opk);
    advance();
    auto rhs = parse_binary_rhs(prec + 1);
    auto b = std::make_unique<mitscript::BinaryExpression>();
    b->op = token_to_binop(opk);
    b->left = std::move(lhs);
    b->right = std::move(rhs);
    lhs = std::move(b);
  }
  return lhs;
}

namespace {
std::string decode_string_literal(const std::string &literal) {
  if (literal.size() < 2)
    return literal;

  std::string result;
  result.reserve(literal.size() - 2);
  for (size_t i = 1; i + 1 < literal.size(); ++i) {
    char c = literal[i];
    if (c == '\\' && i + 1 < literal.size()) {
      char next = literal[++i];
      switch (next) {
      case 'n':
        result.push_back('\n');
        break;
      case 't':
        result.push_back('\t');
        break;
      case '\\':
        result.push_back('\\');
        break;
      case '"':
        result.push_back('"');
        break;
      default:
        result.push_back(next);
        break;
      }
    } else {
      result.push_back(c);
    }
  }
  return result;
}
} // namespace

std::unique_ptr<mitscript::Expression> mitscript::Parser::parse_simple_atom() {
  // unary
  if (match(TokenKind::SUB)) {
    auto u = std::make_unique<mitscript::UnaryExpression>();
    u->op = mitscript::UnOp::NEG;
    u->operand = parse_simple_expression();
    return u;
  }
  if (match(TokenKind::BANG)) {
    auto u = std::make_unique<mitscript::UnaryExpression>();
    u->op = mitscript::UnOp::NOT;
    u->operand = parse_simple_expression();
    return u;
  }

  // parentheses wrap simple_expr (not full expr)
  if (match(TokenKind::LPAREN)) {
    auto e = parse_simple_expression();
    expect(TokenKind::RPAREN, "Expected ')'");
    return e;
  }

  // literals
  if (match(TokenKind::INT)) {
    auto c = std::make_unique<mitscript::IntegerConstant>();
    c->value = std::stoi(previous().text);
    return c;
  }
  if (match(TokenKind::STRING)) {
    auto c = std::make_unique<mitscript::StringConstant>();
    c->value = decode_string_literal(previous().text);
    return c;
  }
  if (match(TokenKind::TRUE)) {
    auto c = std::make_unique<mitscript::BooleanConstant>();
    c->value = true;
    return c;
  }
  if (match(TokenKind::FALSE)) {
    auto c = std::make_unique<mitscript::BooleanConstant>();
    c->value = false;
    return c;
  }
  if (match(TokenKind::NONE)) {
    return std::make_unique<mitscript::NoneConstant>();
  }

  // location, optionally followed by a call
  if (check(TokenKind::IDENTIFIER, 0)) {
    auto loc = parse_location();
    if (check(TokenKind::LPAREN, 0)) {
      return parse_call_from_location(std::move(loc));
    }
    return loc;
  }

  error_here("Expected simple expression");
  return std::make_unique<mitscript::NoneConstant>(); // unreachable
}

std::unique_ptr<mitscript::Expression>
mitscript::Parser::parse_postfix_expression() {
  auto expression = parse_primary_expression();

  while (true) {

    if (match(TokenKind::LPAREN)) {
      // (Optional but recommended) enforce callee is a location:
      auto isLocation = [](const mitscript::Expression *e) {
        return dynamic_cast<const mitscript::Variable *>(e) != nullptr ||
               dynamic_cast<const mitscript::FieldDereference *>(e) !=
                   nullptr ||
               dynamic_cast<const mitscript::IndexExpression *>(e) != nullptr;
      };
      if (!isLocation(expression.get())) {
        error_here("Callee must be a location (identifier, field, or index)");
      }

      auto call = std::make_unique<mitscript::Call>();
      call->callee = std::move(expression);

      if (!check(TokenKind::RPAREN, 0)) {
        call->arguments.push_back(parse_expression());

        while (match(TokenKind::COMMA)) {
          if (check(TokenKind::RPAREN, 0)) {
            break;
          }
          call->arguments.push_back(parse_expression());
        }
      }

      expect(TokenKind::RPAREN, "Expected ')' after arguments");
      expression = std::move(call);
      continue;
    } else if (match(TokenKind::DOT)) {
      const auto &tok =
          expect(TokenKind::IDENTIFIER, "Expected field name after '.'");
      auto fld = std::make_unique<mitscript::FieldDereference>();
      fld->object = std::move(expression);

      fld->field_name = tok.text;

      expression = std::move(fld);

    } else if (match(TokenKind::LBRACKET)) {
      auto idx = std::make_unique<mitscript::IndexExpression>();
      idx->baseExpression = std::move(expression);

      idx->indexExpression = parse_expression();

      expect(TokenKind::RBRACKET, "Expected ']' after index expression");

      expression = std::move(idx);

    } else {
      break;
    }
  }

  return expression;
}

std::unique_ptr<mitscript::Expression>
mitscript::Parser::parse_binary_expression(int min_prec) {
  auto lhs = parse_unary_expression();
  while (is_binary_op(peek(0).kind)) {
    auto op_kind = peek(0).kind;
    int precedence = precedence_of(op_kind);
    if (precedence < min_prec)
      break;

    advance();
    auto rhs = parse_binary_expression(precedence + 1);
    auto ex = std::make_unique<mitscript::BinaryExpression>();
    ex->op = token_to_binop(op_kind);
    ex->left = std::move(lhs);
    ex->right = std::move(rhs);

    lhs = std::move(ex);
  }
  return lhs;
}

std::unique_ptr<mitscript::Expression>
mitscript::Parser::parse_unary_expression() {
  if (match(TokenKind::SUB)) {
    auto unex = std::make_unique<mitscript::UnaryExpression>();
    unex->op = mitscript::UnOp::NEG;
    unex->operand = parse_unary_expression();
    return unex;
  } else if (match(TokenKind::BANG)) {
    auto unex = std::make_unique<mitscript::UnaryExpression>();
    unex->op = mitscript::UnOp::NOT;
    // Special: '!' binds looser than comparisons/equality but tighter than
    // '&'/'|'. Parse the operand as a binary expression with minimum precedence
    // equal to '!'s precedence.
    unex->operand = parse_binary_expression(precedence_of(TokenKind::BANG));
    return unex;
  }
  return parse_postfix_expression();
}

std::unique_ptr<mitscript::Expression>
mitscript::Parser::parse_primary_expression() {
  if (match(TokenKind::INT)) {
    auto ex = std::make_unique<mitscript::IntegerConstant>();
    ex->value = std::stoi(previous().text);
    return ex;
  } else if (match(TokenKind::STRING)) {
    auto ex = std::make_unique<mitscript::StringConstant>();
    ex->value = decode_string_literal(previous().text);
    return ex;
  } else if (match(TokenKind::TRUE)) {
    auto ex = std::make_unique<mitscript::BooleanConstant>();
    ex->value = true;
    return ex;
  } else if (match(TokenKind::FALSE)) {
    auto ex = std::make_unique<mitscript::BooleanConstant>();
    ex->value = false;
    return ex;
  } else if (match(TokenKind::NONE)) {
    return std::make_unique<mitscript::NoneConstant>();
  } else if (match(TokenKind::IDENTIFIER)) {
    auto ex = std::make_unique<mitscript::Variable>();
    ex->name = previous().text;
    return ex;
  } else if (match(TokenKind::LPAREN)) {
    auto ex = parse_expression();
    expect(TokenKind::RPAREN, "Expected closing paren, ')' after expression");
    return ex;
  } else if (match(TokenKind::LBRACE)) {
    auto ex = std::make_unique<mitscript::Record>();
    while (!check(TokenKind::RBRACE, 0)) {
      const auto &key = expect(TokenKind::IDENTIFIER, "Expected field name");
      expect(TokenKind::COLON, "Expected ':' after field name");
      auto val = parse_expression();
      expect(TokenKind::SEMICOLON, "Expected ';' after record field");
      ex->fields.emplace_back(key.text, std::move(val));
    }
    expect(TokenKind::RBRACE, "Expected '}' to close record");
    return ex;
  } else if (match(TokenKind::FUN)) {
    expect(TokenKind::LPAREN, "Expected '(' after 'fun'");
    std::vector<std::string> params;
    if (!check(TokenKind::RPAREN, 0)) {
      const auto &id = expect(TokenKind::IDENTIFIER, "Expected parameter name");
      params.push_back(id.text);
      while (match(TokenKind::COMMA)) {
        if (check(TokenKind::RPAREN, 0))
          break;
        const auto &id2 =
            expect(TokenKind::IDENTIFIER, "Expected parameter name");
        params.push_back(id2.text);
      }
    }
    expect(TokenKind::RPAREN, "Expected  ')' after parameters");
    auto body = parse_block();
    auto function = std::make_unique<mitscript::FunctionDeclaration>();
    function->name = "";
    function->args = std::move(params);
    function->body = std::move(body);
    return function;
  }
  error_here("Expected expression");
  return std::make_unique<mitscript::NoneConstant>();
}

std::unique_ptr<mitscript::Expression> mitscript::Parser::parse_location() {
  const auto &id =
      expect(TokenKind::IDENTIFIER, "Expected identifier for location");
  std::unique_ptr<mitscript::Expression> var =
      std::make_unique<mitscript::Variable>();
  static_cast<mitscript::Variable *>(var.get())->name = id.text;

  for (;;) {
    if (match(TokenKind::DOT)) {
      const auto &field =
          expect(TokenKind::IDENTIFIER, "Expected field name after '.'");
      auto fld = std::make_unique<mitscript::FieldDereference>();
      fld->object = std::move(var);
      fld->field_name = field.text;
      var = std::move(fld);
    } else if (match(TokenKind::LBRACKET)) {
      auto idx_expr = parse_expression();
      expect(TokenKind::RBRACKET, "Expected ']' after index expression");
      auto idx = std::make_unique<mitscript::IndexExpression>();
      idx->baseExpression = std::move(var);
      idx->indexExpression = std::move(idx_expr);
      var = std::move(idx);
    } else {
      break;
    }
  }
  return var;
}

std::unique_ptr<mitscript::Expression>
mitscript::Parser::parse_call_from_location(
    std::unique_ptr<mitscript::Expression> loc) {
  expect(TokenKind::LPAREN, "Expected '(' after function name");

  auto call = std::make_unique<mitscript::Call>();
  call->callee = std::move(loc);

  if (!check(TokenKind::RPAREN, 0)) {
    call->arguments.push_back(parse_expression());

    while (match(TokenKind::COMMA)) {
      if (check(TokenKind::RPAREN, 0)) {
        break;
      }
      call->arguments.push_back(parse_expression());
    }
  }

  expect(TokenKind::RPAREN, "Expected ')' after arguments");
  return call;
}
