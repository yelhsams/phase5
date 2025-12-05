#pragma once

#include "./cfg.hpp"
#include <iosfwd>

namespace mitscript::CFG {

void prettyprint(const FunctionCFG& fn, std::ostream& os, int indent = 0);

} // namespace mitscript::CFG
