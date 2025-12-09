#include "./lexer.hpp"
#include "./token.hpp"

#include <cctype>
#include <iostream>
#include <optional>
#include <sstream>
#include <vector>

static inline bool is_ident_start(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

static inline bool is_ident_continue(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

static inline std::optional<mitscript::TokenKind> keyword_kind(const std::string &identifier)
{
    switch (identifier.size())
    {
    case 2:
        if (identifier == "if")
            return mitscript::TokenKind::IF;
        break;
    case 3:
        if (identifier == "fun")
            return mitscript::TokenKind::FUN;
        break;
    case 4:
        if (identifier == "true")
            return mitscript::TokenKind::TRUE;
        if (identifier == "else")
            return mitscript::TokenKind::ELSE;
        if (identifier == "None")
            return mitscript::TokenKind::NONE;
        break;
    case 5:
        if (identifier == "while")
            return mitscript::TokenKind::WHILE;
        if (identifier == "false")
            return mitscript::TokenKind::FALSE;
        break;
    case 6:
        if (identifier == "return")
            return mitscript::TokenKind::RETURN;
        if (identifier == "global")
            return mitscript::TokenKind::GLOBAL;
        break;
    default:
        break;
    }
    return std::nullopt;
}

void mitscript::Lexer::skip_ws_and_comments(const char *&p, const char *end, int &line, int &col)
{
    for (;;)
    {
        while (p < end)
        {
            char c = *p;
            if (c == ' ' || c == '\t' || c == '\f' || c == '\v')
            {
                ++p;
                ++col;
                continue;
            }
            if (c == '\n')
            {
                ++p;
                ++line;
                col = 1;
                continue;
            }
            if (c == '\r')
            {
                ++p;
                if (p < end && *p == '\n')
                {
                    ++p;
                }
                ++line;
                col = 1;
                continue;
            }
            break;
        }

        if (p + 1 < end && *p == '/' && p[1] == '/')
        {
            p += 2;
            col += 2;
            while (p < end)
            {
                char c = *p;
                if (c == '\n' || c == '\r')
                {
                    break;
                }
                ++p;
                ++col;
            }
            continue;
        }

        break;
    }
}

mitscript::Token mitscript::Lexer::make_error(const std::string &error_msg, int start_line, int start_col, int end_line, int end_col)
{
    throw std::runtime_error("Lexer error at line " + std::to_string(start_line) + ", col " + std::to_string(start_col) + ": " + error_msg);
}

mitscript::Lexer::Lexer(const std::string &file_contents)
    : input(file_contents), data(input.c_str()), size(input.size()), pos(0), current_line(1), current_col(1) {}

mitscript::Token
mitscript::Lexer::lex_string(const char *&p, const char *end, int &line, int &col, int start_line, int start_col)
{
    ++p;
    ++col;

    std::string string_literal;
    string_literal.push_back('"');

    while (p < end)
    {
        char c = *p;

        if (c == '\n' || c == '\r')
        {
            std::string error_msg = "Unterminated string literal starting at line " +
                                    std::to_string(start_line) + ", column " + std::to_string(start_col);
            return make_error(error_msg, start_line, start_col, line, col);
        }

        if (c == '"')
        {
            ++p;
            ++col;
            string_literal.push_back('"');
            return mitscript::Token(mitscript::TokenKind::STRING,
                                    string_literal, start_line, start_col,
                                    line, col);
        }

        if (c == '\\')
        {
            ++p;
            ++col;
            if (p >= end)
            {
                std::string error_msg = "Unterminated string literal starting at line " +
                                        std::to_string(start_line) + ", column " + std::to_string(start_col);
                return make_error(error_msg, start_line, start_col, line, col);
            }
            char e = *p;
            switch (e)
            {
            case '"':
            case '\\':
            case 'n':
            case 't':
                string_literal.push_back('\\');
                string_literal.push_back(e);
                ++p;
                ++col;
                break;
            default:
            {
                std::string error_msg = "Invalid escape sequence '\\\"" + std::string(1, e) + "' in string literal starting at line " + std::to_string(start_line) + ", column " + std::to_string(start_col);
                return make_error(error_msg, start_line, start_col, line, col);
            }
            }
            continue;
        }

        string_literal.push_back(c);
        ++p;
        ++col;
    }

    std::string error_msg = "Unterminated string literal starting at line " +
                            std::to_string(start_line) + ", column " + std::to_string(start_col);
    return make_error(error_msg, start_line, start_col, line, col);
}

mitscript::Token mitscript::Lexer::lex_number(const char *&p, const char *end, int &line, int &col, int start_line, int start_col)
{
    const char *start = p;
    while (p < end && std::isdigit(static_cast<unsigned char>(*p)))
    {
        ++p;
        ++col;
    }
    return mitscript::Token(mitscript::TokenKind::INT, std::string(start, p - start), start_line, start_col, line, col);
}

mitscript::Token mitscript::Lexer::lex_identifier_or_keyword(const char *&p, const char *end, int &line, int &col, int start_line, int start_col)
{
    const char *start = p;
    while (p < end && is_ident_continue(*p))
    {
        ++p;
        ++col;
    }

    std::string identifier(start, p - start);
    auto keyword = keyword_kind(identifier);
    if (keyword.has_value())
    {
        return mitscript::Token(*keyword, identifier, start_line, start_col, line, col);
    }
    return mitscript::Token(mitscript::TokenKind::IDENTIFIER, identifier, start_line, start_col, line, col);
}

std::vector<mitscript::Token> mitscript::Lexer::lex()
{
    std::vector<mitscript::Token> tokens;
    tokens.reserve(input.size() / 4 + 8);

    const char *p = data;
    const char *end = data + size;
    int line = 1;
    int col = 1;

    while (true)
    {
        skip_ws_and_comments(p, end, line, col);

        if (p >= end)
        {
            break;
        }

        int sl = line, sc = col;
        char c = *p;

        if (c == '"')
        {
            tokens.push_back(lex_string(p, end, line, col, sl, sc));
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            tokens.push_back(lex_number(p, end, line, col, sl, sc));
            continue;
        }

        if (is_ident_start(c))
        {
            tokens.push_back(lex_identifier_or_keyword(p, end, line, col, sl, sc));
            continue;
        }

        switch (c)
        {
        case '{':
            ++p;
            ++col;
            tokens.emplace_back(mitscript::TokenKind::LBRACE, "{", sl, sc, line, col);
            break;
        case '}':
            ++p;
            ++col;
            tokens.emplace_back(mitscript::TokenKind::RBRACE, "}", sl, sc, line, col);
            break;
        case '[':
            ++p;
            ++col;
            tokens.emplace_back(mitscript::TokenKind::LBRACKET, "[", sl, sc, line, col);
            break;
        case ']':
            ++p;
            ++col;
            tokens.emplace_back(mitscript::TokenKind::RBRACKET, "]", sl, sc, line, col);
            break;
        case '(':
            ++p;
            ++col;
            tokens.emplace_back(mitscript::TokenKind::LPAREN, "(", sl, sc, line, col);
            break;
        case ')':
            ++p;
            ++col;
            tokens.emplace_back(mitscript::TokenKind::RPAREN, ")", sl, sc, line, col);
            break;
        case ',':
            ++p;
            ++col;
            tokens.emplace_back(mitscript::TokenKind::COMMA, ",", sl, sc, line, col);
            break;
        case ':':
            ++p;
            ++col;
            tokens.emplace_back(mitscript::TokenKind::COLON, ":", sl, sc, line, col);
            break;
        case ';':
            ++p;
            ++col;
            tokens.emplace_back(mitscript::TokenKind::SEMICOLON, ";", sl, sc, line, col);
            break;
        case '.':
            ++p;
            ++col;
            tokens.emplace_back(mitscript::TokenKind::DOT, ".", sl, sc, line, col);
            break;
        case '*':
            ++p;
            ++col;
            tokens.emplace_back(mitscript::TokenKind::MULT, "*", sl, sc, line, col);
            break;
        case '/':
            ++p;
            ++col;
            tokens.emplace_back(mitscript::TokenKind::DIV, "/", sl, sc, line, col);
            break;
        case '+':
            ++p;
            ++col;
            tokens.emplace_back(mitscript::TokenKind::ADD, "+", sl, sc, line, col);
            break;
        case '-':
            ++p;
            ++col;
            tokens.emplace_back(mitscript::TokenKind::SUB, "-", sl, sc, line, col);
            break;
        case '!':
            ++p;
            ++col;
            tokens.emplace_back(mitscript::TokenKind::BANG, "!", sl, sc, line, col);
            break;
        case '&':
            ++p;
            ++col;
            tokens.emplace_back(mitscript::TokenKind::AMP, "&", sl, sc, line, col);
            break;
        case '|':
            ++p;
            ++col;
            tokens.emplace_back(mitscript::TokenKind::BAR, "|", sl, sc, line, col);
            break;
        case '<':
            if (p + 1 < end && p[1] == '=')
            {
                p += 2;
                col += 2;
                tokens.emplace_back(mitscript::TokenKind::LE, "<=", sl, sc, line, col);
            }
            else
            {
                ++p;
                ++col;
                tokens.emplace_back(mitscript::TokenKind::LT, "<", sl, sc, line, col);
            }
            break;
        case '>':
            if (p + 1 < end && p[1] == '=')
            {
                p += 2;
                col += 2;
                tokens.emplace_back(mitscript::TokenKind::GE, ">=", sl, sc, line, col);
            }
            else
            {
                ++p;
                ++col;
                tokens.emplace_back(mitscript::TokenKind::GT, ">", sl, sc, line, col);
            }
            break;
        case '=':
            if (p + 1 < end && p[1] == '=')
            {
                p += 2;
                col += 2;
                tokens.emplace_back(mitscript::TokenKind::EQEQ, "==", sl, sc, line, col);
            }
            else
            {
                ++p;
                ++col;
                tokens.emplace_back(mitscript::TokenKind::ASSIGN, "=", sl, sc, line, col);
            }
            break;
        default:
        {
            std::string error_msg;
            if ((unsigned char)c >= 32 && (unsigned char)c <= 126)
            {
                error_msg = "illegal character '" + std::string(1, c) + "'";
            }
            else
            {
                std::ostringstream oss;
                oss << "illegal character 0x" << std::hex << std::uppercase << (int)(unsigned char)c;
                error_msg = oss.str();
            }
            ++p;
            ++col;
            tokens.push_back(make_error(error_msg, sl, sc, line, col));
            break;
        }
        }
    }

    pos = static_cast<size_t>(p - data);
    current_line = line;
    current_col = col;

    return tokens;
}
