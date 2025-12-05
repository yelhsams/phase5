/*
Test: Long chain with repeated GCs and changing roots

What this tests:
- Repeated allocations and collections on a long chain to catch list unlinking bugs.
- Changing the root position trims the live suffix; previous prefixes are collected correctly.
- No crashes or corruption across multiple GC invocations.
*/

#include "gc.hpp"
#include "objects.hpp"

#include <iostream>
#include <vector>

size_t Object::allocated = 0;
size_t Object::deallocated = 0;

int main() {
  CollectedHeap heap;

  const int N = 2000;
  Integer* head = heap.allocate<Integer>(0);
  Integer* cur = head;
  for (int i = 1; i < N; ++i) {
    cur->rhs = heap.allocate<Integer>(i);
    cur = (Integer*)cur->rhs;
  }

  if (Object::getAlive() != N) {
    std::cerr << "Expected " << N << " alive after allocation but got " << Object::getAlive() << std::endl;
    return 1;
  }

  // Move the root forward in steps; after each GC only the suffix remains.
  Integer* root = head;
  for (int step = 0; step < 5; ++step) {
    // Advance root by N/10 each time
    int advance = (N / 10);
    for (int k = 0; k < advance && root; ++k) root = (Integer*)root->rhs;

    std::vector<Object*> roots;
    if (root) roots.push_back(root);
    heap.gc(roots.begin(), roots.end());

    // Count remaining by walking from root
    int count = 0;
    for (Integer* p = root; p != nullptr; p = (Integer*)p->rhs) ++count;
    if (Object::getAlive() != count) {
      std::cerr << "Alive count mismatch: expected " << count << ", got " << Object::getAlive() << std::endl;
      return 1;
    }
    if (root && root->val < 0) {
      std::cerr << "Value corruption detected" << std::endl;
      return 1;
    }
  }

  // Finally drop roots; should collect all.
  std::vector<Object*> roots;
  heap.gc(roots.begin(), roots.end());
  if (Object::getAlive() != 0) {
    std::cerr << "Expected 0 alive at end but got " << Object::getAlive() << std::endl;
    return 1;
  }

  std::cout << "All tests passed!" << std::endl;
  return 0;
}
