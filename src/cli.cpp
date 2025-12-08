#include "cli.hpp"

#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>

void print_help(char *argv0) {
    std::cout << "Usage: " << argv0 << " [SUBCOMMAND] [input_file] [OPTIONS]\n";
    std::cout << "\n";
    std::cout << "POSITIONALS:\n";
    std::cout << "  input_file TEXT             Path to input file, use '-' for stdin\n";
    std::cout << "\n";
    std::cout << "OPTIONS:\n";
    std::cout << "  -h,     --help              Print this help message and exit\n";
    std::cout << "  -o,     --output TEXT       Path to output file, use '-' for stdout\n";
    std::cout << "  -m,     --mem UINT          Memory limit in MB -- Enabled for VM/derby subcommands\n";
    std::cout << "  -O,     --opt TEXT          Comma-separated list of optimizations (e.g. 'jit')\n";
    std::cout << "\n";
    std::cout << "SUBCOMMANDS:\n";
    std::cout << "  scan\n";
    std::cout << "  parse\n";
    std::cout << "  compile\n";
    std::cout << "  interpret\n";
    std::cout << "  vm\n";
    std::cout << "  derby\n";
}

void cli_parse_internal(Command &c, int argc, char **argv) {
  std::string input_file = "-";
  std::string output_file = "-";
  size_t mem = 4;
  std::vector<std::string> opt;
  CommandKind kind;

  if (argc < 2) {
    print_help(argv[0]);
    exit(1);
  }

  std::string subcommand = argv[1];

  if (subcommand == "scan") {
    kind = CommandKind::SCAN;
  } else if (subcommand == "parse") {
    kind = CommandKind::PARSE;
  } else if (subcommand == "compile") {
    kind = CommandKind::COMPILE;
  } else if (subcommand == "interpret") {
    kind = CommandKind::INTERPRET;
  } else if (subcommand == "vm") {
    kind = CommandKind::VM;
  } else if (subcommand == "derby") {
    kind = CommandKind::DERBY;
  } else if (subcommand == "-h" || subcommand == "--help") {
    print_help(argv[0]);
    exit(0);
  } else {
    std::cerr << "Error: Unknown subcommand '" << subcommand << "'\n";
    print_help(argv[0]);
    exit(1);
  }

  bool input_file_set = false;
  for (int i = 2; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      print_help(argv[0]);
      exit(0);
    } else if (arg == "-o" || arg == "--output") {
      if (i + 1 < argc) {
        output_file = argv[++i];
      } else {
        std::cerr << "Error: -o/--output requires a value\n";
        exit(1);
      }
    } else if (arg == "-m" || arg == "--mem" || arg == "-mem") {
      if (i + 1 < argc) {
        mem = std::stoul(argv[++i]);
      } else {
        std::cerr << "Error: -m/--mem requires a value\n";
        exit(1);
      }
    } else if (arg.rfind("--mem=", 0) == 0) {
      mem = std::stoul(arg.substr(6));
    } else if (arg == "-O" || arg == "--opt") {
      if (i + 1 < argc) {
        std::string opt_str = argv[++i];
        std::stringstream ss(opt_str);
        std::string item;
        while (std::getline(ss, item, ',')) {
          // Trim whitespace from each item
          item.erase(0, item.find_first_not_of(" \t\n\r"));
          item.erase(item.find_last_not_of(" \t\n\r") + 1);
          if (!item.empty()) {
            opt.push_back(item);
          }
        }
      } else {
        std::cerr << "Error: -O/--opt requires a value\n";
        exit(1);
      }
    } else if (arg.rfind("--opt=", 0) == 0) {
      std::string opt_str = arg.substr(6);
      std::stringstream ss(opt_str);
      std::string item;
      while (std::getline(ss, item, ',')) {
        item.erase(0, item.find_first_not_of(" \t\n\r"));
        item.erase(item.find_last_not_of(" \t\n\r") + 1);
        if (!item.empty()) opt.push_back(item);
      }
    } else if (!input_file_set) {
      input_file = arg;
      input_file_set = true;
    } else if (arg.rfind("-", 0) == 0) {
      // Ignore unknown flags to stay lenient with wrapper scripts.
      continue;
    } else {
      // Ignore extra positionals.
      continue;
    }
  }

  if (input_file != "-" && !std::filesystem::exists(input_file)) {
    std::cerr << "Error: Input file '" << input_file << "' does not exist"
              << std::endl;
    exit(1);
  }

  c.input_stream =
      (input_file == "-") ? &std::cin : new std::ifstream(input_file);
  c.output_stream =
      (output_file == "-") ? &std::cout : new std::ofstream(output_file);
  c.input_filename = input_file;
  c.kind = kind;
  c.mem = mem;
  c.opt = opt;
}

Command cli_parse(int argc, char **argv) {
  Command command;
  cli_parse_internal(command, argc, argv);
  return command;
}
