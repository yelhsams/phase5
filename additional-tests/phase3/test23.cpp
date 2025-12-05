// t7.cpp - Deep linear chain (stress test for marker)
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

    // Create a very deep chain
    const int DEPTH = 5000;
    Integer *root = heap.allocate<Integer>(0);
    Integer *current = root;

    for (int i = 1; i < DEPTH; ++i)
    {
        current->lhs = heap.allocate<Integer>(i);
        current = (Integer *)current->lhs;
    }

    if (Object::getAlive() != DEPTH)
    {
        std::cerr << "Expected " << DEPTH << " alive objects but got " << Object::getAlive() << std::endl;
        return 1;
    }

    // GC with root - all should survive
    std::vector<Object *> rootset = {root};
    heap.gc(rootset.begin(), rootset.end());

    if (Object::getAlive() != DEPTH)
    {
        std::cerr << "Expected " << DEPTH << " alive objects after GC but got " << Object::getAlive() << std::endl;
        return 1;
    }

    // Verify chain integrity (spot check)
    current = root;
    for (int i = 0; i < 100; ++i)
    {
        if (current->val != i)
        {
            std::cerr << "Chain corrupted at position " << i << std::endl;
            return 1;
        }
        current = (Integer *)current->lhs;
    }

    // Remove root - all should be collected
    rootset.clear();
    heap.gc(rootset.begin(), rootset.end());

    if (Object::getAlive() != 0)
    {
        std::cerr << "Expected 0 alive objects but got " << Object::getAlive() << std::endl;
        return 1;
    }

    return 0;
}
