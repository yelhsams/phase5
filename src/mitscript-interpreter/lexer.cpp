#include "./lexer.hpp"
#include "./token.hpp"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <vector>

namespace {

// Character classification lookup table for faster checks
alignas(64) constexpr uint8_t char_class[256] = {
    // 0-31: control characters
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    // 32-47: space, punctuation
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    // 48-57: digits '0'-'9'
    2,2,2,2,2,2,2,2,2,2,
    // 58-64: more punctuation
    0,0,0,0,0,0,0,
    // 65-90: 'A'-'Z'
    3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
    // 91-96: more punctuation including '_' at 95
    0,0,0,0,1,0,
    // 97-122: 'a'-'z'
    3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
    // 123-127: more punctuation
    0,0,0,0,0,
    // 128-255: extended ASCII
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
// 1 = underscore (ident start/continue)
// 2 = digit (ident continue only)
// 3 = letter (ident start/continue)

inline bool is_ident_start(unsigned char c) {
    return char_class[c] >= 1 && char_class[c] != 2;
}

inline bool is_ident_continue(unsigned char c) {
    return char_class[c] >= 1;
}

inline bool is_digit(unsigned char c) {
    return char_class[c] == 2;
}

// Keyword matching using direct character comparison for speed
inline mitscript::TokenKind keyword_kind(const char* start, size_t len) {
    using TK = mitscript::TokenKind;
    switch (len) {
    case 2:
        if (start[0] == 'i' && start[1] == 'f') return TK::IF;
        break;
    case 3:
        if (start[0] == 'f' && start[1] == 'u' && start[2] == 'n') return TK::FUN;
        break;
    case 4:
        if (start[0] == 't' && start[1] == 'r' && start[2] == 'u' && start[3] == 'e') return TK::TRUE;
        if (start[0] == 'e' && start[1] == 'l' && start[2] == 's' && start[3] == 'e') return TK::ELSE;
        if (start[0] == 'N' && start[1] == 'o' && start[2] == 'n' && start[3] == 'e') return TK::NONE;
        break;
    case 5:
        if (std::memcmp(start, "while", 5) == 0) return TK::WHILE;
        if (std::memcmp(start, "false", 5) == 0) return TK::FALSE;
        break;
    case 6:
        if (std::memcmp(start, "return", 6) == 0) return TK::RETURN;
        if (std::memcmp(start, "global", 6) == 0) return TK::GLOBAL;
        break;
    }
    return TK::IDENTIFIER;
}

// Static strings for single-character tokens to avoid repeated allocations
static const std::string STR_LBRACE = "{";
static const std::string STR_RBRACE = "}";
static const std::string STR_LBRACKET = "[";
static const std::string STR_RBRACKET = "]";
static const std::string STR_LPAREN = "(";
static const std::string STR_RPAREN = ")";
static const std::string STR_COMMA = ",";
static const std::string STR_COLON = ":";
static const std::string STR_SEMICOLON = ";";
static const std::string STR_DOT = ".";
static const std::string STR_MULT = "*";
static const std::string STR_DIV = "/";
static const std::string STR_ADD = "+";
static const std::string STR_SUB = "-";
static const std::string STR_BANG = "!";
static const std::string STR_AMP = "&";
static const std::string STR_BAR = "|";
static const std::string STR_LT = "<";
static const std::string STR_LE = "<=";
static const std::string STR_GT = ">";
static const std::string STR_GE = ">=";
static const std::string STR_ASSIGN = "=";
static const std::string STR_EQEQ = "==";

} // anonymous namespace

void mitscript::Lexer::skip_ws_and_comments(const char *&p, const char *end, int &line, int &col)
{
    for (;;) {
        while (p < end) {
            unsigned char c = static_cast<unsigned char>(*p);
            if (c == ' ' || c == '\t' || c == '\f' || c == '\v') {
                ++p;
                ++col;
                continue;
            }
            if (c == '\n') {
                ++p;
                ++line;
                col = 1;
                continue;
            }
            if (c == '\r') {
                ++p;
                if (p < end && *p == '\n') {
                    ++p;
                }
                ++line;
                col = 1;
                continue;
            }
            break;
        }

        // Check for line comment
        if (p + 1 < end && p[0] == '/' && p[1] == '/') {
            p += 2;
            col += 2;
            while (p < end && *p != '\n' && *p != '\r') {
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
    string_literal.reserve(32);  // Reserve reasonable initial size
    string_literal.push_back('"');

    while (p < end) {
        char c = *p;

        if (c == '\n' || c == '\r') {
            return make_error("Unterminated string literal starting at line " +
                            std::to_string(start_line) + ", column " + std::to_string(start_col),
                            start_line, start_col, line, col);
        }

        if (c == '"') {
            ++p;
            ++col;
            string_literal.push_back('"');
            return mitscript::Token(mitscript::TokenKind::STRING,
                                    std::move(string_literal), start_line, start_col,
                                    line, col);
        }

        if (c == '\\') {
            ++p;
            ++col;
            if (p >= end) {
                return make_error("Unterminated string literal starting at line " +
                                std::to_string(start_line) + ", column " + std::to_string(start_col),
                                start_line, start_col, line, col);
            }
            char e = *p;
            switch (e) {
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
                return make_error("Invalid escape sequence '\\" + std::string(1, e) +
                                "' in string literal starting at line " +
                                std::to_string(start_line) + ", column " + std::to_string(start_col),
                                start_line, start_col, line, col);
            }
            continue;
        }

        string_literal.push_back(c);
        ++p;
        ++col;
    }

    return make_error("Unterminated string literal starting at line " +
                    std::to_string(start_line) + ", column " + std::to_string(start_col),
                    start_line, start_col, line, col);
}

mitscript::Token mitscript::Lexer::lex_number(const char *&p, const char *end, int &line, int &col, int start_line, int start_col)
{
    const char *start = p;
    while (p < end && is_digit(static_cast<unsigned char>(*p))) {
        ++p;
        ++col;
    }
    return mitscript::Token(mitscript::TokenKind::INT, std::string(start, static_cast<size_t>(p - start)), start_line, start_col, line, col);
}

mitscript::Token mitscript::Lexer::lex_identifier_or_keyword(const char *&p, const char *end, int &line, int &col, int start_line, int start_col)
{
    const char *start = p;
    while (p < end && is_ident_continue(static_cast<unsigned char>(*p))) {
        ++p;
        ++col;
    }

    size_t len = static_cast<size_t>(p - start);
    TokenKind kind = keyword_kind(start, len);
    return mitscript::Token(kind, std::string(start, len), start_line, start_col, line, col);
}

std::vector<mitscript::Token> mitscript::Lexer::lex()
{
    std::vector<mitscript::Token> tokens;
    tokens.reserve(input.size() / 4 + 8);

    const char *p = data;
    const char *end = data + size;
    int line = 1;
    int col = 1;

    while (true) {
        skip_ws_and_comments(p, end, line, col);

        if (p >= end) {
            break;
        }

        int sl = line, sc = col;
        unsigned char c = static_cast<unsigned char>(*p);

        // String literal
        if (c == '"') {
            tokens.push_back(lex_string(p, end, line, col, sl, sc));
            continue;
        }

        // Number
        if (is_digit(c)) {
            tokens.push_back(lex_number(p, end, line, col, sl, sc));
            continue;
        }

        // Identifier or keyword
        if (is_ident_start(c)) {
            tokens.push_back(lex_identifier_or_keyword(p, end, line, col, sl, sc));
            continue;
        }

        // Single and double-character operators
        switch (c) {
        case '{':
            ++p; ++col;
            tokens.emplace_back(TokenKind::LBRACE, STR_LBRACE, sl, sc, line, col);
            break;
        case '}':
            ++p; ++col;
            tokens.emplace_back(TokenKind::RBRACE, STR_RBRACE, sl, sc, line, col);
            break;
        case '[':
            ++p; ++col;
            tokens.emplace_back(TokenKind::LBRACKET, STR_LBRACKET, sl, sc, line, col);
            break;
        case ']':
            ++p; ++col;
            tokens.emplace_back(TokenKind::RBRACKET, STR_RBRACKET, sl, sc, line, col);
            break;
        case '(':
            ++p; ++col;
            tokens.emplace_back(TokenKind::LPAREN, STR_LPAREN, sl, sc, line, col);
            break;
        case ')':
            ++p; ++col;
            tokens.emplace_back(TokenKind::RPAREN, STR_RPAREN, sl, sc, line, col);
            break;
        case ',':
            ++p; ++col;
            tokens.emplace_back(TokenKind::COMMA, STR_COMMA, sl, sc, line, col);
            break;
        case ':':
            ++p; ++col;
            tokens.emplace_back(TokenKind::COLON, STR_COLON, sl, sc, line, col);
            break;
        case ';':
            ++p; ++col;
            tokens.emplace_back(TokenKind::SEMICOLON, STR_SEMICOLON, sl, sc, line, col);
            break;
        case '.':
            ++p; ++col;
            tokens.emplace_back(TokenKind::DOT, STR_DOT, sl, sc, line, col);
            break;
        case '*':
            ++p; ++col;
            tokens.emplace_back(TokenKind::MULT, STR_MULT, sl, sc, line, col);
            break;
        case '/':
            ++p; ++col;
            tokens.emplace_back(TokenKind::DIV, STR_DIV, sl, sc, line, col);
            break;
        case '+':
            ++p; ++col;
            tokens.emplace_back(TokenKind::ADD, STR_ADD, sl, sc, line, col);
            break;
        case '-':
            ++p; ++col;
            tokens.emplace_back(TokenKind::SUB, STR_SUB, sl, sc, line, col);
            break;
        case '!':
            ++p; ++col;
            tokens.emplace_back(TokenKind::BANG, STR_BANG, sl, sc, line, col);
            break;
        case '&':
            ++p; ++col;
            tokens.emplace_back(TokenKind::AMP, STR_AMP, sl, sc, line, col);
            break;
        case '|':
            ++p; ++col;
            tokens.emplace_back(TokenKind::BAR, STR_BAR, sl, sc, line, col);
            break;
        case '<':
            if (p + 1 < end && p[1] == '=') {
                p += 2; col += 2;
                tokens.emplace_back(TokenKind::LE, STR_LE, sl, sc, line, col);
            } else {
                ++p; ++col;
                tokens.emplace_back(TokenKind::LT, STR_LT, sl, sc, line, col);
            }
            break;
        case '>':
            if (p + 1 < end && p[1] == '=') {
                p += 2; col += 2;
                tokens.emplace_back(TokenKind::GE, STR_GE, sl, sc, line, col);
            } else {
                ++p; ++col;
                tokens.emplace_back(TokenKind::GT, STR_GT, sl, sc, line, col);
            }
            break;
        case '=':
            if (p + 1 < end && p[1] == '=') {
                p += 2; col += 2;
                tokens.emplace_back(TokenKind::EQEQ, STR_EQEQ, sl, sc, line, col);
            } else {
                ++p; ++col;
                tokens.emplace_back(TokenKind::ASSIGN, STR_ASSIGN, sl, sc, line, col);
            }
            break;
        default: {
            std::string error_msg;
            if (c >= 32 && c <= 126) {
                error_msg = "illegal character '";
                error_msg += static_cast<char>(c);
                error_msg += "'";
            } else {
                std::ostringstream oss;
                oss << "illegal character 0x" << std::hex << std::uppercase << static_cast<int>(c);
                error_msg = oss.str();
            }
            ++p; ++col;
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
