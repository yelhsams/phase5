#pragma once

#include "./types.hpp"
#include <iostream>

namespace bytecode {

class PrettyPrinter {
public:
  PrettyPrinter();
  void print(const Function *function, std::ostream &os);

private:
  void indent();
  void unindent();
  std::ostream &print_indent(std::ostream &os);
  void print(const std::string &name, const std::vector<std::string> &names,
             std::ostream &os);
  void print(const Constant *constant, std::ostream &os);
  void print(const Instruction &inst, std::ostream &os);
  void print(const InstructionList *ilist, std::ostream &os);
  std::string unescape(const std::string &str);

  size_t indent_;
};

void prettyprint(const Function *function, std::ostream &os);

} // namespace bytecode
