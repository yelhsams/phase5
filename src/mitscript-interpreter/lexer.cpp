#include "./lexer.hpp"
#include "./token.hpp"

#include <cctype>
#include <iostream>
#include <optional>
#include <sstream>
#include <vector>




static inline bool is_ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

static inline bool is_ident_continue(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

static inline std::optional<mitscript::TokenKind> keyword_kind(const std::string &identifier) {
    switch (identifier.size()) {
        case 2:
            if (identifier == "if") return mitscript::TokenKind::IF;
            break;
        case 3:
            if (identifier == "fun") return mitscript::TokenKind::FUN;
            break;
        case 4:
            if (identifier == "true") return mitscript::TokenKind::TRUE;
            if (identifier == "else") return mitscript::TokenKind::ELSE;
            if (identifier == "None") return mitscript::TokenKind::NONE;
            break;
        case 5:
            if (identifier == "while") return mitscript::TokenKind::WHILE;
            if (identifier == "false") return mitscript::TokenKind::FALSE;
            break;
        case 6:
            if (identifier == "return") return mitscript::TokenKind::RETURN;
            if (identifier == "global") return mitscript::TokenKind::GLOBAL;
            break;
        default:
            break;
    }
    return std::nullopt;
}

void mitscript::Lexer::skip_ws_and_comments() {
    for (;;) {
        while (pos < size) {
            char c = data[pos];
            if (c == ' ' || c == '\t' || c == '\f' || c == '\v') {
                ++pos;
                ++current_col;
                continue;
            }
            if (c == '\n') {
                ++pos;
                ++current_line;
                current_col = 1;
                continue;
            }
            if (c == '\r') {
                ++pos;
                if (pos < size && data[pos] == '\n') {
                    ++pos;
                }
                ++current_line;
                current_col = 1;
                continue;
            }
            break;
        }

        if (pos + 1 < size && data[pos] == '/' && data[pos + 1] == '/') {
            pos += 2;
            current_col += 2;
            while (pos < size) {
                char c = data[pos];
                if (c == '\n' || c == '\r') {
                    break;
                }
                ++pos;
                ++current_col;
            }
            continue;
        }

        break;
    }
}
mitscript::Token mitscript::Lexer::make_error(const std::string &error_msg, int start_line, int start_col, int end_line, int end_col) {
    return mitscript::Token(mitscript::TokenKind::ERROR, error_msg, start_line, start_col, end_line, end_col);
}
mitscript::Lexer::Lexer(const std::string &file_contents)
    : input(file_contents), data(input.c_str()), size(input.size()), pos(0), current_line(1), current_col(1) {}

char mitscript::Lexer::peek(size_t lookahead) const {
    const size_t idx = pos + lookahead;
    if (idx >= size) {
        return '\0';
    }
    return data[idx];
}

bool mitscript::Lexer::is_eof() const {
    return pos >= size;
}
void mitscript::Lexer::advance() {
    if (is_eof()) return;

    char c = input[pos++];

    if (c == '\r') {
        if (peek() == '\n') {
            ++pos; // consume the '\n'
        }
        ++current_line;
        current_col = 1;
        return;
    }

    if (c == '\n') {
        ++current_line;
        current_col = 1;
    } else {
        ++current_col;
    }
}


//     advance();
//     std::string string_literal;
//     string_literal += '"';
//     while(!is_eof() && peek() != '"') {
//         string_literal += peek();
//         advance();
//     }
//     if (is_eof()) {
//         std::string error_msg = "Unterminated string literal starting at line " + std::to_string(start_line) + ", column " + std::to_string(start_col);
//         return make_error(error_msg, start_line, start_col, this->current_line, this->current_col);
//     } else {
//         advance(); // Skip the closing quote
//         string_literal += '"';
//         return mitscript::Token(mitscript::TokenKind::STRING, string_literal, start_line, start_col, this->current_line, this->current_col);
//     }
// }
std::optional<mitscript::Token>
mitscript::Lexer::lex_string(int start_line, int start_col) {
    advance();

    std::string string_literal;
    string_literal.push_back('"');

    while (pos < size) {
        char c = data[pos];

        if (c == '\n' || c == '\r') {
            std::string error_msg = "Unterminated string literal starting at line " +
                std::to_string(start_line) + ", column " + std::to_string(start_col);
            return make_error(error_msg, start_line, start_col, current_line, current_col);
        }

        if (c == '"') {
            advance();
            string_literal.push_back('"');
            return mitscript::Token(mitscript::TokenKind::STRING,
                                    string_literal, start_line, start_col,
                                    current_line, current_col);
        }

        if (c == '\\') {
            ++pos;
            ++current_col;
            if (pos >= size) {
                std::string error_msg = "Unterminated string literal starting at line " +
                    std::to_string(start_line) + ", column " + std::to_string(start_col);
                return make_error(error_msg, start_line, start_col, current_line, current_col);
            }
            char e = data[pos];
            switch (e) {
                case '"':
                case '\\':
                case 'n':
                case 't':
                    string_literal.push_back('\\');
                    string_literal.push_back(e);
                    ++pos;
                    ++current_col;
                    break;
                default: {
                    std::string error_msg = "Invalid escape sequence '\\"
                        + std::string(1, e) + "' in string literal starting at line "
                        + std::to_string(start_line) + ", column " + std::to_string(start_col);
                    return make_error(error_msg, start_line, start_col, current_line, current_col);
                }
            }
            continue;
        }

        string_literal.push_back(c);
        ++pos;
        ++current_col;
    }

    // EOF before closing quote
    std::string error_msg = "Unterminated string literal starting at line " +
        std::to_string(start_line) + ", column " + std::to_string(start_col);
    return make_error(error_msg, start_line, start_col, current_line, current_col);
}


std::optional<mitscript::Token> mitscript::Lexer::lex_number(int start_line, int start_col) {
    const size_t start = pos;
    while (!is_eof()) {
        char c = peek();
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            break;
        }
        advance();
    }
    return mitscript::Token(mitscript::TokenKind::INT, std::string(data + start, pos - start), start_line, start_col, this->current_line, this->current_col);
}

std::optional<mitscript::Token> mitscript::Lexer::lex_identifier_or_keyword(int start_line, int start_col) {
    const size_t start = pos;
    while (!is_eof() && is_ident_continue(peek())) {
        advance();
    }

    std::string identifier(data + start, pos - start);
    auto keyword = keyword_kind(identifier);
    if (keyword.has_value()) {
        return mitscript::Token(*keyword, identifier, start_line, start_col, this->current_line, this->current_col);
    }
    return mitscript::Token(mitscript::TokenKind::IDENTIFIER, identifier, start_line, start_col, this->current_line, this->current_col);



}

std::vector<mitscript::Token> mitscript::Lexer::lex() {
    std::vector<mitscript::Token> tokens;
    tokens.reserve(input.size() / 4 + 8);
    while (true) {
        skip_ws_and_comments();

        if (is_eof()) {
            break;
        }


        int sl = current_line, sc = current_col;
        char c = peek();

        if (c == '"') {
            auto token = lex_string(sl, sc);
            tokens.push_back(*token);
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c))) {
            auto token = lex_number(sl, sc);
            tokens.push_back(*token);
            continue;
        }

        if (is_ident_start(c)) {
            auto token = lex_identifier_or_keyword(sl, sc);
            tokens.push_back(*token);
            continue;
        }

        switch (c) {
            case '{': advance(); tokens.emplace_back(mitscript::TokenKind::LBRACE, "{", sl, sc, current_line, current_col); break;
            case '}': advance(); tokens.emplace_back(mitscript::TokenKind::RBRACE, "}", sl, sc, current_line, current_col); break;
            case '[': advance(); tokens.emplace_back(mitscript::TokenKind::LBRACKET, "[", sl, sc, current_line, current_col); break;
            case ']': advance(); tokens.emplace_back(mitscript::TokenKind::RBRACKET, "]", sl, sc, current_line, current_col); break;
            case '(': advance(); tokens.emplace_back(mitscript::TokenKind::LPAREN, "(", sl, sc, current_line, current_col); break;
            case ')': advance(); tokens.emplace_back(mitscript::TokenKind::RPAREN, ")", sl, sc, current_line, current_col); break;
            case ',': advance(); tokens.emplace_back(mitscript::TokenKind::COMMA, ",", sl, sc, current_line, current_col); break;
            case ':': advance(); tokens.emplace_back(mitscript::TokenKind::COLON, ":", sl, sc, current_line, current_col); break;
            case ';': advance(); tokens.emplace_back(mitscript::TokenKind::SEMICOLON, ";", sl, sc, current_line, current_col); break;
            case '.': advance(); tokens.emplace_back(mitscript::TokenKind::DOT, ".", sl, sc, current_line, current_col); break;
            case '*': advance(); tokens.emplace_back(mitscript::TokenKind::MULT, "*", sl, sc, current_line, current_col); break;
            case '/': advance(); tokens.emplace_back(mitscript::TokenKind::DIV, "/", sl, sc, current_line, current_col); break;
            case '+': advance(); tokens.emplace_back(mitscript::TokenKind::ADD, "+", sl, sc, current_line, current_col); break;
            case '-': advance(); tokens.emplace_back(mitscript::TokenKind::SUB, "-", sl, sc, current_line, current_col); break;
            case '!': advance(); tokens.emplace_back(mitscript::TokenKind::BANG, "!", sl, sc, current_line, current_col); break;
            case '&': advance(); tokens.emplace_back(mitscript::TokenKind::AMP, "&", sl, sc, current_line, current_col); break;
            case '|': advance(); tokens.emplace_back(mitscript::TokenKind::BAR, "|", sl, sc, current_line, current_col); break;
            case '<':
                if (peek(1) == '=') {
                    advance(); advance();
                    tokens.emplace_back(mitscript::TokenKind::LE, "<=", sl, sc, current_line, current_col);
                } else {
                    advance();
                    tokens.emplace_back(mitscript::TokenKind::LT, "<", sl, sc, current_line, current_col);
                }
                break;
            case '>':
                if (peek(1) == '=') {
                    advance(); advance();
                    tokens.emplace_back(mitscript::TokenKind::GE, ">=", sl, sc, current_line, current_col);
                } else {
                    advance();
                    tokens.emplace_back(mitscript::TokenKind::GT, ">", sl, sc, current_line, current_col);
                }
                break;
            case '=':
                if (peek(1) == '=') {
                    advance(); advance();
                    tokens.emplace_back(mitscript::TokenKind::EQEQ, "==", sl, sc, current_line, current_col);
                } else {
                    advance();
                    tokens.emplace_back(mitscript::TokenKind::ASSIGN, "=", sl, sc, current_line, current_col);
                }
                break;
            default: {
                std::string error_msg;
                if ((unsigned char)c >= 32 && (unsigned char)c <= 126) {
                    error_msg = "illegal character '" + std::string(1, c) + "'";
                } else {
                    std::ostringstream oss;
                    oss << "illegal character 0x" << std::hex << std::uppercase << (int)(unsigned char)c;
                    error_msg = oss.str();
                }
                advance();
                tokens.push_back(make_error(error_msg, sl, sc, current_line, current_col));
                break;
            }
        }

    }

    // tokens.emplace_back(mitscript::TokenKind::EOF_TOKEN, "", current_line, current_col, current_line, current_col);
    return tokens;
}
