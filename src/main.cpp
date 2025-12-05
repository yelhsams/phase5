#include "cli.hpp"

#include "bytecode/opt_constprop.hpp"
#include "bytecode/opt_deadcode.hpp"
#include "bytecode/opt_inline.hpp"
#include "bytecode/opt_licm.hpp"
#include "bytecode/opt_peephole.hpp"
#include "bytecode/parser.hpp"
#include "bytecode/prettyprinter.hpp"
#include "mitscript-compiler/bytecode-converter.hpp"
#include "mitscript-compiler/constant-propagation.hpp"
#include "mitscript-compiler/converter.hpp"
#include "mitscript-interpreter/interpreter.hpp"
#include "mitscript-interpreter/lexer.hpp"
#include "mitscript-interpreter/parser.hpp"
#include "vm/interpreter.hpp"
#include <algorithm>
#include <iostream>

static std::string read_istream(std::istream &is)
{
  return std::string(std::istreambuf_iterator<char>(is),
                     std::istreambuf_iterator<char>());
}

static bool has_opt(const Command &cmd, const std::string &name)
{
  auto present = [&](const std::string &needle)
  {
    return std::find(cmd.opt.begin(), cmd.opt.end(), needle) != cmd.opt.end();
  };
  return present(name);
}

static std::string token_kind_name(const mitscript::Token &t)
{
  switch (t.kind)
  {
  case mitscript::TokenKind::STRING:
    return "STRINGLITERAL";
  case mitscript::TokenKind::INT:
    return "INTLITERAL";
  case mitscript::TokenKind::IDENTIFIER:
    return "IDENTIFIER";
  case mitscript::TokenKind::TRUE:
    return "BOOLEANLITERAL";
  case mitscript::TokenKind::FALSE:
    return "BOOLEANLITERAL";
  case mitscript::TokenKind::EOF_TOKEN:
    return "EOF";
  default:
    return "";
  }
}

int main(int argc, char **argv)
{
  Command command = cli_parse(argc, argv);

  std::string contents = read_istream(*command.input_stream);
  std::string input_filename = command.input_filename;
  std::string output_filename = command.output_filename;

  bool had_error = false;
  switch (command.kind)
  {
  case CommandKind::SCAN:
  {
    try
    {
      mitscript::Lexer lexer(contents);
      std::vector<mitscript::Token> tokens = lexer.lex();

      std::ostream &out = *command.output_stream;

      // for (const auto &token : tokens)
      // {
      //   if (token.kind == mitscript::TokenKind::ERROR)
      //   {
      //     had_error = true;
      //     std::cerr << "Error from lexer at line " << token.start_line
      //               << ", column " << token.start_col << ": " << token.text
      //               << "\n";
      //     continue;
      //   }

      //   const std::string kind_name = token_kind_name(token);

      //   if (token.kind == mitscript::TokenKind::EOF_TOKEN)
      //   {
      //     out << token.start_line << ' ' << kind_name << '\n';
      //   }
      //   else if (!kind_name.empty())
      //   {
      //     out << token.start_line << ' ' << kind_name << ' ' << token.text
      //         << '\n';
      //   }
      //   else
      //   {
      //     out << token.start_line << ' ' << token.text << '\n';
      //   }
      // }
      break;
    }
    catch (const std::exception &e)
    {
      had_error = true;
      std::cerr << e.what() << "\n";
    }
    break;
  }
  case CommandKind::PARSE:
  {
    try
    {
      mitscript::Lexer lexer(contents);
      std::vector<mitscript::Token> tokens = lexer.lex();

      mitscript::Parser parser(tokens);
      auto ast = parser.parse();

      std::cout << "Parse successful\n";
    }
    catch (const std::exception &e)
    {
      had_error = true;
      std::cerr << e.what() << "\n";
    }
    break;
  }

  case CommandKind::COMPILE:
  {
    try
    {
      mitscript::Lexer lexer(contents);
      std::vector<mitscript::Token> tokens = lexer.lex();

      mitscript::Parser parser(tokens);
      auto ast = parser.parse();

      mitscript::CFG::FunctionCFG cfg;
      CFGBuilder cfg_builder(cfg, /*moduleScope=*/true);
      ast->accept(&cfg_builder);

      // Optional optimization: constant propagation / folding.
      if (has_opt(command, "constprop"))
      {
        mitscript::analysis::ConstantPropagation cp(cfg);
        cp.run();
        cp.rewrite();
      }

      BytecodeConverter bc;
      bytecode::Function *bytecode = bc.convert(cfg, /*is_toplevel=*/true);
      bytecode::prettyprint(bytecode, *command.output_stream);
    }
    catch (const std::exception &e)
    {
      std::cout << "Compilation error:\n";
      had_error = true;
      std::cerr << e.what() << "\n";
    }

    break;
  }
  case CommandKind::DERBY:
  {
    try
    {
      // Compile source to bytecode and immediately execute on the VM.
      mitscript::Lexer lexer(contents);
      std::vector<mitscript::Token> tokens = lexer.lex();

      mitscript::Parser parser(tokens);
      auto ast = parser.parse();

      mitscript::CFG::FunctionCFG cfg;
      CFGBuilder cfg_builder(cfg, /*moduleScope=*/true);
      ast->accept(&cfg_builder);

      // Optional optimization: constant propagation / folding.
      if (has_opt(command, "constprop") || has_opt(command, "all"))
      {
        // if (true) {
        mitscript::analysis::ConstantPropagation cp(cfg);
        cp.run();
        cp.rewrite();
      }

      BytecodeConverter bc;
      bytecode::Function *bytecode = bc.convert(cfg, /*is_toplevel=*/true);

      // Optional optimization: constant propagation on bytecode
      if (has_opt(command, "bytecode-constprop") || has_opt(command, "all"))
      {
        bytecode::opt_constprop::constant_propagate(bytecode);
      }

      // Optional optimization: dead code elimination (runs after constant
      // propagation)
      if (has_opt(command, "deadcode") || has_opt(command, "all"))
      {
        bytecode::opt_deadcode::eliminate_dead_code(bytecode);
      }

      // Optional optimization: peephole (constant folding)
      if (has_opt(command, "peephole") || has_opt(command, "all"))
      {
        bytecode::opt_peephole::peephole_optimize(bytecode);
      }

      if (has_opt(command, "inline"))
      {
        bytecode::opt_inline::inline_functions(bytecode);
      }
      if (has_opt(command, "licm"))
      {
        bytecode::opt_licm::run(bytecode);
      }
      vm::VM vm(command.mem);
      vm.run(bytecode);
    }
    catch (const std::exception &e)
    {
      had_error = true;
      std::cerr << e.what() << "\n";
    }
    break;
  }
  case CommandKind::INTERPRET:
  {
    try
    {
      mitscript::Lexer lexer(contents);
      std::vector<mitscript::Token> tokens = lexer.lex();

      mitscript::Parser parser(tokens);
      auto ast = parser.parse();

      mitscript::interpret(*ast);
    }
    catch (const std::exception &e)
    {
      had_error = true;
      std::cerr << e.what() << "\n";
    }
    break;
  }

  case CommandKind::VM:
  {
    try
    {
      // Parse bytecode
      bytecode::Function *bytecode_func = bytecode::parse(contents);

      size_t max_mem_mb = command.mem;

      // Optional optimization: constant propagation on bytecode
      if (has_opt(command, "bytecode-constprop") || has_opt(command, "all"))
      {
        bytecode::opt_constprop::constant_propagate(bytecode_func);
      }

      // Optional optimization: dead code elimination (runs after constant
      // propagation)
      if (has_opt(command, "deadcode") || has_opt(command, "all"))
      {
        bytecode::opt_deadcode::eliminate_dead_code(bytecode_func);
      }

      // Optional optimization: peephole (constant folding)
      if (has_opt(command, "peephole") || has_opt(command, "all"))
      {
        bytecode::opt_peephole::peephole_optimize(bytecode_func);
      }

      if (has_opt(command, "inline") || has_opt(command, "all"))
      {
        bytecode::opt_inline::inline_functions(bytecode_func);
      }
      if (has_opt(command, "licm") || has_opt(command, "all"))
      {
        bytecode::opt_licm::run(bytecode_func);
      }

      // Create VM and execute
      vm::VM vm(max_mem_mb);
      vm.run(bytecode_func);

      // Cleanup
      delete bytecode_func;
    }
    catch (const std::runtime_error &e)
    {
      // Catch lexer errors early and return
      had_error = true;
      std::cerr << e.what() << "\n";
      return had_error ? 1 : 0;
    }
    catch (const vm::InsufficientStackException &e)
    {
      had_error = true;
      std::cerr << e.what() << "\n";
    }
    catch (const vm::UninitializedVariableException &e)
    {
      had_error = true;
      std::cerr << e.what() << "\n";
    }
    catch (const vm::IllegalCastException &e)
    {
      had_error = true;
      std::cerr << e.what() << "\n";
    }
    catch (const vm::IllegalArithmeticException &e)
    {
      had_error = true;
      std::cerr << e.what() << "\n";
    }
    catch (const vm::RuntimeException &e)
    {
      had_error = true;
      std::cerr << e.what() << "\n";
    }
    catch (const std::exception &e)
    {
      had_error = true;
      std::cerr << "Error: " << e.what() << "\n";
    }
    break;
  }
  }

  return had_error ? 1 : 0;
}
