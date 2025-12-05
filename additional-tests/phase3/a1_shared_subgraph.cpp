/*
Test: Shared subgraph and root set changes

What this tests:
- Objects reachable via multiple parents (diamond/shared subgraph) are only kept once and not double-freed.
- Changing the root set between GCs correctly collects now-unreachable parents while preserving the shared child.
- Field values of kept objects remain intact across GCs.
*/

#include "gc.hpp"
#include "objects.hpp"

#include <iostream>
#include <string>
#include <vector>

size_t Object::allocated = 0;
size_t Object::deallocated = 0;

int main() {
  CollectedHeap heap;

  // Create two parents that share a common child.
  Integer* r1 = heap.allocate<Integer>(1);
  Integer* r2 = heap.allocate<Integer>(2);
  Integer* shared = heap.allocate<Integer>(42);
  Integer* tail = heap.allocate<Integer>(99);

  // r1 ->rhs-> shared ->rhs-> tail
  r1->rhs = shared;
  shared->rhs = tail;
  // r2 ->lhs-> shared
  r2->lhs = shared;

  if (Object::getAlive() != 4) {
    std::cerr << "Expected 4 alive objects after allocation but got " << Object::getAlive() << std::endl;
    return 1;
  }

  // Keep both parents initially; shared should not be double-counted.
  {
    std::vector<Object*> roots = {r1, r2};
    heap.gc(roots.begin(), roots.end());
    if (Object::getAlive() != 4) {
      std::cerr << "Expected 4 alive after GC with two roots but got " << Object::getAlive() << std::endl;
      return 1;
    }
  }

  // Drop r1 from the root set; r1 should be collected. shared and tail remain via r2.
  {
    std::vector<Object*> roots = {r2};
    heap.gc(roots.begin(), roots.end());
    if (Object::getAlive() != 3) {
      std::cerr << "Expected 3 alive after dropping r1 but got " << Object::getAlive() << std::endl;
      return 1;
    }
    // Sanity check values are preserved on live objects
    if (r2->val != 2 || ((Integer*)r2->lhs)->val != 42 || ((Integer*)((Integer*)r2->lhs)->rhs)->val != 99) {
      std::cerr << "Live object values corrupted" << std::endl;
      return 1;
    }
  }

  // Finally drop all roots; everything should be collected.
  {
    std::vector<Object*> roots;
    heap.gc(roots.begin(), roots.end());
    if (Object::getAlive() != 0) {
      std::cerr << "Expected 0 alive at end but got " << Object::getAlive() << std::endl;
      return 1;
    }
  }

  std::cout << "All tests passed!" << std::endl;
  return 0;
}
