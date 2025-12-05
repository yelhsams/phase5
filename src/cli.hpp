#pragma once

#include <iostream>
#include <vector>
#include <string>

enum class CommandKind { SCAN, PARSE, COMPILE, INTERPRET, VM, DERBY };

struct Command {
  CommandKind kind;
  std::istream *input_stream;
  std::ostream *output_stream;
  std::string input_filename;
  std::string output_filename;
  size_t mem;
  std::vector<std::string> opt;

  Command()
      : input_stream(nullptr), output_stream(nullptr), input_filename(""), output_filename(""), mem(4), opt() {}

  ~Command() {
    if (input_stream && input_stream != &std::cin) {
      delete input_stream;
    }
    if (output_stream && output_stream != &std::cout) {
      delete output_stream;
    }
  }
};

Command cli_parse(int argc, char **argv);
