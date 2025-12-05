// t5.cpp - Binary tree structure
#include "gc.hpp"
#include "objects.hpp"

#include <vector>
#include <iostream>
#include <string>

size_t Object::allocated = 0;
size_t Object::deallocated = 0;

int main()
{
    CollectedHeap heap;

    // Create a complete binary tree of depth 4 (15 nodes)
    Integer *root = heap.allocate<Integer>(0);
    std::vector<Integer *> nodes = {root};

    for (int i = 0; i < 7; ++i)
    {
        Integer *left = heap.allocate<Integer>(2 * i + 1);
        Integer *right = heap.allocate<Integer>(2 * i + 2);
        nodes[i]->lhs = left;
        nodes[i]->rhs = right;
        nodes.push_back(left);
        nodes.push_back(right);
    }

    if (Object::getAlive() != 15)
    {
        std::cerr << "Expected 15 alive objects but got " << Object::getAlive() << std::endl;
        return 1;
    }

    // GC with root in rootset - all should survive
    std::vector<Object *> rootset = {root};
    heap.gc(rootset.begin(), rootset.end());

    if (Object::getAlive() != 15)
    {
        std::cerr << "Expected 15 alive objects after GC but got " << Object::getAlive() << std::endl;
        return 1;
    }

    // Verify tree structure is intact
    if (root->val != 0 ||
        ((Integer *)root->lhs)->val != 1 ||
        ((Integer *)root->rhs)->val != 2)
    {
        std::cerr << "Tree structure corrupted" << std::endl;
        return 1;
    }

    // Remove root from rootset - all should be collected
    rootset.clear();
    heap.gc(rootset.begin(), rootset.end());

    if (Object::getAlive() != 0)
    {
        std::cerr << "Expected 0 alive objects but got " << Object::getAlive() << std::endl;
        return 1;
    }

    return 0;
}
