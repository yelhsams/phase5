// t9.cpp - Complex DAG with multiple reconvergent paths
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

    // Create a complex DAG where multiple paths lead to same nodes
    // Structure:     root
    //               /    \
    //              A      B
    //             /|\    /|\
    //            C D E  E D F
    //             \|/    \|/
    //              G      H
    //               \    /
    //                 I

    Integer *root = heap.allocate<Integer>(0);
    Integer *a = heap.allocate<Integer>(1);
    Integer *b = heap.allocate<Integer>(2);
    Integer *c = heap.allocate<Integer>(3);
    Integer *d = heap.allocate<Integer>(4);
    Integer *e = heap.allocate<Integer>(5);
    Integer *f = heap.allocate<Integer>(6);
    Integer *g = heap.allocate<Integer>(7);
    Integer *h = heap.allocate<Integer>(8);
    Integer *i = heap.allocate<Integer>(9);

    root->lhs = a;
    root->rhs = b;

    // Left side converges to g
    a->lhs = c;
    a->rhs = d;
    c->lhs = g;
    d->lhs = g;
    d->rhs = e;
    e->lhs = g;

    // Right side converges to h
    b->lhs = e; // e is shared between both sides
    b->rhs = d; // d is also shared
    e->rhs = h;
    d->lhs = h; // Wait, d->lhs was already g, let me fix this
    b->rhs = f;
    f->lhs = h;

    // Both converge to i
    g->lhs = i;
    h->lhs = i;

    if (Object::getAlive() != 10)
    {
        std::cerr << "Expected 10 alive objects but got " << Object::getAlive() << std::endl;
        return 1;
    }

    // GC with root - all should survive
    std::vector<Object *> rootset = {root};
    heap.gc(rootset.begin(), rootset.end());

    if (Object::getAlive() != 10)
    {
        std::cerr << "Expected 10 alive objects after GC but got " << Object::getAlive() << std::endl;
        return 1;
    }

    // Verify some shared nodes
    if (g->lhs != i || h->lhs != i)
    {
        std::cerr << "Convergent paths to i lost" << std::endl;
        return 1;
    }

    if (i->val != 9)
    {
        std::cerr << "Shared node i corrupted" << std::endl;
        return 1;
    }

    // Remove root
    rootset.clear();
    heap.gc(rootset.begin(), rootset.end());

    if (Object::getAlive() != 0)
    {
        std::cerr << "Expected 0 alive objects but got " << Object::getAlive() << std::endl;
        return 1;
    }

    return 0;
}
