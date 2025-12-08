#pragma once
#include "./token.hpp"
#include <optional>
#include <string>
#include <vector>

namespace mitscript
{
    class Lexer
    {
    public:
        Lexer(const std::string &file_contents);

        std::vector<Token> lex();

    private:
        std::string input;
        const char *data;
        size_t size;
        size_t pos;
        int current_line;
        int current_col;

        void skip_ws_and_comments(const char *&p, const char *end, int &line, int &col);
        mitscript::Token lex_string(const char *&p, const char *end, int &line, int &col, int start_line, int start_col);
        mitscript::Token lex_number(const char *&p, const char *end, int &line, int &col, int start_line, int start_col);
        mitscript::Token lex_identifier_or_keyword(const char *&p, const char *end, int &line, int &col, int start_line, int start_col);
        mitscript::Token make_error(const std::string &error_msg, int start_line, int start_col, int end_line, int end_col);
    };

}
