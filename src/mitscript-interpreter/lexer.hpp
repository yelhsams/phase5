#pragma once
#include "./token.hpp"
#include <optional>
#include <string>
#include <vector>


namespace mitscript {
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
        std::optional<Token> lex_chunk();
        std::string getNextContiguousString();
        void skip_ws_and_comments();
        std::optional<mitscript::Token> lex_string(int start_line, int start_col);
        std::optional<mitscript::Token> lex_number(int start_line, int start_col);
        std::optional<mitscript::Token> lex_identifier_or_keyword(int start_line, int start_col);
        mitscript::Token make_error(const std::string &error_msg, int start_line, int start_col, int end_line, int end_col);
        void advance();


    };



}
