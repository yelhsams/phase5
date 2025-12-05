// t8.cpp - Multiple independent subgraphs with selective collection
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

    // Create 5 independent chains, each with 10 nodes
    std::vector<Integer *> roots;
    const int NUM_CHAINS = 5;
    const int CHAIN_LENGTH = 10;

    for (int i = 0; i < NUM_CHAINS; ++i)
    {
        Integer *root = heap.allocate<Integer>(i * 100);
        roots.push_back(root);
        Integer *current = root;

        for (int j = 1; j < CHAIN_LENGTH; ++j)
        {
            current->rhs = heap.allocate<Integer>(i * 100 + j);
            current = (Integer *)current->rhs;
        }
    }

    if (Object::getAlive() != NUM_CHAINS * CHAIN_LENGTH)
    {
        std::cerr << "Expected " << (NUM_CHAINS * CHAIN_LENGTH) << " alive objects but got " << Object::getAlive() << std::endl;
        return 1;
    }

    // Keep only chains 0, 2, 4 in rootset
    std::vector<Object *> rootset = {roots[0], roots[2], roots[4]};
    heap.gc(rootset.begin(), rootset.end());

    // Should have 3 chains * 10 nodes = 30 objects
    if (Object::getAlive() != 30)
    {
        std::cerr << "Expected 30 alive objects after selective GC but got " << Object::getAlive() << std::endl;
        return 1;
    }

    // Verify surviving chains are intact
    for (int idx : {0, 2, 4})
    {
        Integer *current = roots[idx];
        for (int j = 0; j < CHAIN_LENGTH; ++j)
        {
            if (current->val != idx * 100 + j)
            {
                std::cerr << "Chain " << idx << " corrupted at position " << j << std::endl;
                return 1;
            }
            current = (Integer *)current->rhs;
        }
    }

    // Remove all roots
    rootset.clear();
    heap.gc(rootset.begin(), rootset.end());

    if (Object::getAlive() != 0)
    {
        std::cerr << "Expected 0 alive objects but got " << Object::getAlive() << std::endl;
        return 1;
    }

    return 0;
}
