// t6.cpp - Partial collection with shared references
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

    // Create a diamond pattern: A -> B,C -> D
    Integer *a = heap.allocate<Integer>(1);
    Integer *b = heap.allocate<Integer>(2);
    Integer *c = heap.allocate<Integer>(3);
    Integer *d = heap.allocate<Integer>(4);

    a->lhs = b;
    a->rhs = c;
    b->lhs = d;
    c->lhs = d; // d has two parents

    // Create unreachable objects
    Integer *orphan1 = heap.allocate<Integer>(100);
    Integer *orphan2 = heap.allocate<Integer>(200);
    orphan1->lhs = orphan2;

    if (Object::getAlive() != 6)
    {
        std::cerr << "Expected 6 alive objects but got " << Object::getAlive() << std::endl;
        return 1;
    }

    // GC with only 'a' in rootset - should keep a,b,c,d and collect orphans
    std::vector<Object *> rootset = {a};
    heap.gc(rootset.begin(), rootset.end());

    if (Object::getAlive() != 4)
    {
        std::cerr << "Expected 4 alive objects after GC but got " << Object::getAlive() << std::endl;
        return 1;
    }

    // Verify shared object 'd' is still reachable through both paths
    if (b->lhs != d || c->lhs != d)
    {
        std::cerr << "Shared reference lost" << std::endl;
        return 1;
    }

    if (d->val != 4)
    {
        std::cerr << "Shared object corrupted" << std::endl;
        return 1;
    }

    return 0;
}
