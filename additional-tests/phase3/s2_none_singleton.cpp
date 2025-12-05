/*
Test: NoneValue singleton behavior

What this tests:
- NoneValue::instance() returns the same pointer every time.
- APIs that produce None (RecordValue::get_entry, NoneConstant visit path) return the canonical singleton.
*/

#include <cassert>
#include <iostream>

#include "../../src/mitscript-interpreter/interpreter.cpp"

using namespace mitscript;

int main() {
  auto* n1 = NoneValue::instance();
  auto* n2 = NoneValue::instance();
  assert(n1 == n2);

  CollectedHeap heap;

  // RecordValue get_entry for missing key should return None singleton
  std::unordered_map<std::string, Value*> m;
  auto* rec = heap.allocate<RecordValue>(m);
  Value* v = rec->get_entry("missing", &heap);
  assert(dynamic_cast<NoneValue*>(v) && v == NoneValue::instance());

  // Interpreter path for NoneConstant returns singleton
  NoneConstant nc;
  Interpreter interp;
  interp.visit(&nc);
  assert(dynamic_cast<NoneValue*>(interp.rval_) && interp.rval_ == NoneValue::instance());

  std::cout << "All tests passed!" << std::endl;
  return 0;
}
