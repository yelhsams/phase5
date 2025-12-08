#include "./lexer.hpp"
#include "./token.hpp"

#include <cctype>
#include <iostream>
#include <optional>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <set>




static const std::unordered_map<std::string, mitscript::TokenKind> string_to_token = {
    // Keywords
    {"global", mitscript::TokenKind::GLOBAL},
    {"if", mitscript::TokenKind::IF},
    {"else", mitscript::TokenKind::ELSE},
    {"while", mitscript::TokenKind::WHILE},
    {"return", mitscript::TokenKind::RETURN},
    {"fun", mitscript::TokenKind::FUN},
    {"true", mitscript::TokenKind::TRUE},
    {"false", mitscript::TokenKind::FALSE},
    {"None", mitscript::TokenKind::NONE},

    // Punctuation & Delimiters
    {"{", mitscript::TokenKind::LBRACE},
    {"}", mitscript::TokenKind::RBRACE},
    {"[", mitscript::TokenKind::LBRACKET},
    {"]", mitscript::TokenKind::RBRACKET},
    {"(", mitscript::TokenKind::LPAREN},
    {")", mitscript::TokenKind::RPAREN},
    {",", mitscript::TokenKind::COMMA},
    {":", mitscript::TokenKind::COLON},
    {";", mitscript::TokenKind::SEMICOLON},
    {".", mitscript::TokenKind::DOT},
    {"=", mitscript::TokenKind::ASSIGN},

    // Operators
    {"*", mitscript::TokenKind::MULT},
    {"/", mitscript::TokenKind::DIV},
    {"+", mitscript::TokenKind::ADD},
    {"-", mitscript::TokenKind::SUB},
    {"<", mitscript::TokenKind::LT},
    {"<=", mitscript::TokenKind::LE},
    {">=", mitscript::TokenKind::GE},
    {">", mitscript::TokenKind::GT},
    {"==", mitscript::TokenKind::EQEQ},
    {"!", mitscript::TokenKind::BANG},
    {"&", mitscript::TokenKind::AMP},
    {"|", mitscript::TokenKind::BAR}
};

void mitscript::Lexer::skip_ws_and_comments() {
    for (;;) {
        while (!is_eof() && std::isspace(static_cast<unsigned char>(peek()))) {
            advance();
        }

        if (!is_eof() && peek() == '/' && rest.size() > 1 && rest[1] == '/') {
            advance(); advance();
            while (!is_eof() && peek() != '\n' && peek() != '\r') {
                advance();
            }
            continue;
        }

        break;
    }
}
mitscript::Token mitscript::Lexer::make_error(const std::string &error_msg, int start_line, int start_col, int end_line, int end_col) {
    return mitscript::Token(mitscript::TokenKind::ERROR, error_msg, start_line, start_col, end_line, end_col);
}

bool isInt(const std::string &s) {
    if (s.empty()) {
        return false;
    }
    for (char c : s) {
        if (!std::isdigit(c)) {
            return false;
        }
    }
    return true;
}

mitscript::Lexer::Lexer(const std::string &file_contents)
    : rest(file_contents), current_line(1), current_col(1) {}

char mitscript::Lexer::peek() const {
    if (is_eof()) {
        return '\0';
    }
    return this->rest[0];
}

bool mitscript::Lexer::is_eof() const {
    return this->rest.empty();
}
std::string mitscript::Lexer::getNextContiguousString() {

    if (is_eof()) {
        return "";
    }

    std::string result;
    while (!is_eof() && (std::isalnum(peek()) || peek() == '_')) {
        result += peek();
        advance();
    }
    return result;
}

void mitscript::Lexer::advance() {
//     if (!is_eof()) {
//         char c = this->rest[0];
//         this->rest = this->rest.substr(1);

//     if (rest[0] == '\r') {
//         // CRLF -> one newline
//         if (rest.size() >= 2 && rest[1] == '\n') {
//             rest.erase(0, 2);
//         } else {
//             // lone CR -> also a newline
//             rest.erase(0, 1);
//     }
//         ++current_line;
//         current_col = 1;
//         return;
//     }


//     if (c == '\n') {
//         ++this->current_line;
//         this->current_col = 1;
//     } else {
//         ++this->current_col;
//     }
// }

    if (is_eof()) return;

    char c = this -> rest[0];
    rest.erase(0, 1);
    if (c == '\r') {
        if (rest.size() >= 1 && rest[0] == '\n') {
            rest.erase(0, 1);
        }
        current_line++;
        current_col = 1;
        return;
    }

    if (c == '\n') {
        current_line++;
        current_col = 1;
    } else {
        current_col++;
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

    while (!is_eof()) {
        char c = peek();

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
            advance();
            if (is_eof()) {
                std::string error_msg = "Unterminated string literal starting at line " +
                    std::to_string(start_line) + ", column " + std::to_string(start_col);
                return make_error(error_msg, start_line, start_col, current_line, current_col);
            }
            char e = peek();
            switch (e) {
                case '"':
                case '\\':
                case 'n':
                case 't':
                    string_literal.push_back('\\');
                    string_literal.push_back(e);
                    advance();
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
        advance();
    }

    // EOF before closing quote
    std::string error_msg = "Unterminated string literal starting at line " +
        std::to_string(start_line) + ", column " + std::to_string(start_col);
    return make_error(error_msg, start_line, start_col, current_line, current_col);
}


std::optional<mitscript::Token> mitscript::Lexer::lex_number(int start_line, int start_col) {
    std::string number_literal;
    while (!is_eof() && std::isdigit(peek())) {
        number_literal += peek();
        advance();
    }
    if (isInt(number_literal)) {
        return mitscript::Token(mitscript::TokenKind::INT, number_literal, start_line, start_col, this->current_line, this->current_col);
    } else {
        std::string error_msg = "Invalid integer literal '" + number_literal + "'";
        return make_error(error_msg, start_line, start_col, this->current_line, this->current_col);
    }
}

std::optional<mitscript::Token> mitscript::Lexer::lex_identifier_or_keyword(int start_line, int start_col) {
    std::string identifier = getNextContiguousString();
    auto keyword_it = string_to_token.find(identifier);
    if (keyword_it != string_to_token.end()) {
        return mitscript::Token(keyword_it->second, identifier, start_line, start_col, this->current_line, this->current_col);
    } else {
        return mitscript::Token(mitscript::TokenKind::IDENTIFIER, identifier, start_line, start_col, this->current_line, this->current_col);
    }



}

std::vector<mitscript::Token> mitscript::Lexer::lex() {
    std::vector<mitscript::Token> tokens;
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
        }

         else if (std::isdigit(c)) {
            auto token = lex_number(sl, sc);
            tokens.push_back(*token);
        }

        else if (std::isalpha(c) || c == '_') {
            auto token = lex_identifier_or_keyword(sl, sc);
            tokens.push_back(*token);
        }

        else {
            if (rest.size() >= 2) {
                std::string next_two_chars = rest.substr(0, 2);
                auto two_char_op = string_to_token.find(next_two_chars);
                if (two_char_op != string_to_token.end()) {
                    advance();
                    advance();
                    tokens.emplace_back(two_char_op->second, next_two_chars, sl, sc, current_line, current_col);
                    continue;
                }
            }

            std::string one(1, c);
            auto one_char_op = string_to_token.find(one);
            if (one_char_op != string_to_token.end()) {
                advance();
                tokens.emplace_back(one_char_op->second, one, sl, sc, current_line, current_col);
                continue;
            }

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
        }

    }

    // tokens.emplace_back(mitscript::TokenKind::EOF_TOKEN, "", current_line, current_col, current_line, current_col);
    return tokens;
}
