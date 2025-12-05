/*
Test: Partial collection in a binary tree structure

What this tests:
- Complex object graphs (binary tree via lhs/rhs) where only a subset is rooted.
- GC should collect unreachable subtrees while retaining reachable ones.
- Values of retained nodes remain correct.
*/

#include "gc.hpp"
#include "objects.hpp"

#include <iostream>
#include <vector>

size_t Object::allocated = 0;
size_t Object::deallocated = 0;

static Integer* make_node(CollectedHeap& heap, int v, Integer* l=nullptr, Integer* r=nullptr) {
  Integer* n = heap.allocate<Integer>(v);
  n->lhs = l;
  n->rhs = r;
  return n;
}

int main() {
  CollectedHeap heap;

  // Build a small tree
  //         10
  //       /    \
  //      5      15
  //     / \    /  \
  //    3   7  12  18
  Integer* n3 = make_node(heap, 3);
  Integer* n7 = make_node(heap, 7);
  Integer* n12 = make_node(heap, 12);
  Integer* n18 = make_node(heap, 18);
  Integer* n5 = make_node(heap, 5, n3, n7);
  Integer* n15 = make_node(heap, 15, n12, n18);
  Integer* root = make_node(heap, 10, n5, n15);

  if (Object::getAlive() != 7) {
    std::cerr << "Expected 7 alive after allocation but got " << Object::getAlive() << std::endl;
    return 1;
  }

  // Root only the right subtree via n15 (drop root and left subtree entirely)
  {
    std::vector<Object*> roots = {n15};
    heap.gc(roots.begin(), roots.end());
    if (Object::getAlive() != 3) { // n15, n12, n18
      std::cerr << "Expected 3 alive after rooting right subtree but got " << Object::getAlive() << std::endl;
      return 1;
    }
    if (n15->val != 15 || ((Integer*)n15->lhs)->val != 12 || ((Integer*)n15->rhs)->val != 18) {
      std::cerr << "Right subtree values corrupted" << std::endl;
      return 1;
    }
  }

  // Now root nothing; everything should be collected.
  {
    std::vector<Object*> roots;
    heap.gc(roots.begin(), roots.end());
    if (Object::getAlive() != 0) {
      std::cerr << "Expected 0 alive after final GC but got " << Object::getAlive() << std::endl;
      return 1;
    }
  }

  std::cout << "All tests passed!" << std::endl;
  return 0;
}
