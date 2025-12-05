/*
Test: BooleanValue singleton behavior

What this tests:
- true_instance() returns the same pointer every time.
- false_instance() returns the same pointer every time.
- from(true/false) return the canonical singletons.
- Operator lambdas (LT, EQ, AND, OR) produce canonical Boolean singletons.
*/

#include <cassert>
#include <iostream>

// Include interpreter implementation to access mitscript::BooleanValue and evaluate_lambdas
#include "../../src/mitscript-interpreter/interpreter.cpp"

using namespace mitscript;

int main() {
  // Direct singleton access
  auto* t1 = BooleanValue::true_instance();
  auto* t2 = BooleanValue::true_instance();
  auto* f1 = BooleanValue::false_instance();
  auto* f2 = BooleanValue::false_instance();
  assert(t1 == t2);
  assert(f1 == f2);
  assert(BooleanValue::from(true) == t1);
  assert(BooleanValue::from(false) == f1);
  assert(t1 != f1);

  CollectedHeap heap;
  // Using operator lambdas
  // 1 < 2 => true singleton
  Value* vlt = evaluate_lambdas.at(BinOp::LT)(
      heap.allocate<IntegerValue>(1),
      heap.allocate<IntegerValue>(2),
      &heap);
  auto* blt = dynamic_cast<BooleanValue*>(vlt);
  assert(blt && blt == BooleanValue::true_instance());

  // 2 == 2 => true singleton
  Value* veq = evaluate_lambdas.at(BinOp::EQ)(
      heap.allocate<IntegerValue>(2),
      heap.allocate<IntegerValue>(2),
      &heap);
  auto* beq = dynamic_cast<BooleanValue*>(veq);
  assert(beq && beq == BooleanValue::true_instance());

  // true & false => false singleton
  Value* vand = evaluate_lambdas.at(BinOp::AND)(
      BooleanValue::true_instance(),
      BooleanValue::false_instance(),
      &heap);
  auto* band = dynamic_cast<BooleanValue*>(vand);
  assert(band && band == BooleanValue::false_instance());

  // false | true => true singleton
  Value* vor = evaluate_lambdas.at(BinOp::OR)(
      BooleanValue::false_instance(),
      BooleanValue::true_instance(),
      &heap);
  auto* bor = dynamic_cast<BooleanValue*>(vor);
  assert(bor && bor == BooleanValue::true_instance());

  std::cout << "All tests passed!" << std::endl;
  return 0;
}
