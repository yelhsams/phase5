/*
Test: Self-cycle and GC idempotence

What this tests:
- An object that references itself (self-cycle) remains alive when rooted.
- Running GC multiple times without changing the root set is safe (idempotent) and keeps counts stable.
- Removing the root allows collection of the entire self-cycle.
*/

#include "gc.hpp"
#include "objects.hpp"

#include <iostream>
#include <vector>

size_t Object::allocated = 0;
size_t Object::deallocated = 0;

int main() {
  CollectedHeap heap;

  Integer* self = heap.allocate<Integer>(7);
  self->lhs = self; // self-cycle
  self->rhs = self; // both fields point to itself

  if (Object::getAlive() != 1) {
    std::cerr << "Expected 1 alive after allocation but got " << Object::getAlive() << std::endl;
    return 1;
  }

  // Rooted: should remain alive across multiple GCs.
  std::vector<Object*> roots = {self};
  for (int i = 0; i < 3; ++i) {
    heap.gc(roots.begin(), roots.end());
    if (Object::getAlive() != 1) {
      std::cerr << "Expected 1 alive during rooted GCs but got " << Object::getAlive() << std::endl;
      return 1;
    }
  }
  if (self->val != 7) {
    std::cerr << "Value changed across GC runs" << std::endl;
    return 1;
  }

  // Unroot and collect; self-cycle should be reclaimed.
  roots.clear();
  heap.gc(roots.begin(), roots.end());
  if (Object::getAlive() != 0) {
    std::cerr << "Expected 0 alive after unrooting but got " << Object::getAlive() << std::endl;
    return 1;
  }

  std::cout << "All tests passed!" << std::endl;
  return 0;
}
