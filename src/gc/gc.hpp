#pragma once
#include <type_traits>
#include <vector>
#include <utility>
#include <cstddef>
#include <tuple>
#include <cstdint>
#include "lrucache.hpp"

class CollectedHeap;

// Any object that inherits from collectable can be created and tracked by the
// garbage collector.
class Collectable {
 public:
  virtual ~Collectable() = default;

 private:
  // Header fields used by the GC
  bool marked_ = false;
  Collectable* next_ = nullptr;
  Collectable* prev_ = nullptr;
  std::size_t size_ = 0;

  // Simple generational metadata
  uint8_t generation_ = 0;      // 0 = young, 1 = old (expandable)
  bool in_remembered_ = false;  // tracked in remembered set when old → young

 protected:
  /*
    The mark phase of the garbage collector needs to follow all pointers from the
    collectable objects, check if those objects have been marked, and if they
    have not, mark them and follow their pointers. Implementations of follow(...)
    in subclasses must call heap.markSuccessors(child_ptr) on each Collectable*
    field.
  */
  virtual void follow(CollectedHeap& heap) = 0;

  friend class CollectedHeap;
};


/*
  This class keeps track of the garbage collected heap. The class must:
  - provide and implement method(s) for allocating objects that will be
    supported by garbage collection
  - keep track of all currently allocated objects
  - provide and implement methods that perform mark and sweep to deallocate
    objects that are not reachable from a given set of objects
*/
class CollectedHeap {
 public:
  CollectedHeap() = default;
  mitscript::LRUCache<int>* allocation_cache =
      new mitscript::LRUCache<int>(1000);

  // Remembered set for old objects that may point to young ones
  std::vector<Collectable*> remembered_;

  bool is_young(Collectable* obj) const {
    return obj && obj->generation_ == 0;
  }
  bool is_old(Collectable* obj) const {
    return obj && obj->generation_ > 0;
  }

  void remember(Collectable* obj) {
    if (!obj || obj->in_remembered_ || !is_old(obj)) return;
    remembered_.push_back(obj);
    obj->in_remembered_ = true;
  }

  // Write barrier: call this whenever an old object gets a reference
  // to a (potentially) young object.
  void write_barrier(Collectable* owner, Collectable* child) {
    if (!owner || !child) return;
    if (is_old(owner) && is_young(child)) {
      remember(owner);
    }
  }

  /*
    T must be a subclass of Collectable. Before returning the
    object, it should be registered so that it can be deallocated later.
  */
  template <typename T, typename... Args>
  T* allocate(Args&&... args) {
    static_assert(std::is_base_of_v<Collectable, T>,
                  "T must derive from Collectable");
    static_assert(std::is_constructible_v<T, Args...>,
                  "T must be constructible with Args...");

    T* obj = new T(std::forward<Args>(args)...);

    // Insert at head of global list
    obj->next_ = head_;
    obj->prev_ = nullptr;
    if (head_) head_->prev_ = obj;
    head_ = obj;

    obj->size_ = sizeof(T);
    obj->generation_ = 0;       // newly allocated → young
    obj->in_remembered_ = false;
    obj->marked_ = false;

    allocated_bytes_ += obj->size_;
    objects_allocated_ += 1;
    return obj;
  }

  /*
    This is the method that is called by the follow(...) method of a Collectable
    object. This is how a Collectable object lets the garbage collector know
    about other Collectable objects pointed to by itself.

    IMPORTANT: unlike your original version, markSuccessors no longer calls
    follow() directly (no recursion). It just marks and pushes onto an explicit
    mark stack. The GC drivers (gc(), full_gc(), minor_gc()) will drain this
    stack.
  */
  void markSuccessors(Collectable* next) {
    if (!next) return;
    if (next->marked_) return;
    next->marked_ = true;
    mark_stack_.push_back(next);
  }

  /*
    The gc method should be periodically invoked by your VM (or by other methods
    in CollectedHeap) whenever the VM decides it is time to reclaim memory.
    This default implementation triggers a full mark-sweep.
  */
  template <typename Iterator>
  void gc(Iterator begin, Iterator end) {
    full_gc(begin, end);
  }

  /*
    Minor GC: only collects unreachable YOUNG objects.
    Old unreachable objects are left for full GC.
    Semantics match your original code, but using an explicit mark stack.
  */
  template <typename Iterator>
  void minor_gc(Iterator begin, Iterator end) {
    // 1) Mark: roots and remembered old objects
    clear_mark_stack();

    for (auto it = begin; it != end; ++it) {
      markSuccessors(*it);
    }
    for (Collectable* obj : remembered_) {
      markSuccessors(obj);
    }

    process_mark_stack();

    // 2) Sweep: walk entire list, but only delete unreachable young objects.
    Collectable* cur = head_;
    while (cur) {
      Collectable* next = cur->next_;
      if (!cur->marked_) {
        if (is_young(cur)) {
          // unlink from list
          if (cur->prev_) cur->prev_->next_ = next;
          else head_ = next;
          if (next) next->prev_ = cur->prev_;

          purge_from_cache(cur);

          allocated_bytes_ -= cur->size_;
          objects_allocated_ -= 1;
          delete cur;
        }
        // unreachable old objects collected only in full GC
      } else {
        // live object: clear mark bit for next GC
        cur->marked_ = false;
        if (is_young(cur)) {
          // promoted to old after surviving a minor GC
          cur->generation_ = 1;
        }
      }
      cur = next;
    }
    // We do NOT clear remembered_ here; still needed for future minor GCs.
  }

  /*
    Full GC: collects unreachable objects from BOTH young and old generations.
    Semantics match your original full_gc, but with an explicit mark stack.
  */
  template <typename Iterator>
  void full_gc(Iterator begin, Iterator end) {
    // 1) Mark from roots
    clear_mark_stack();

    for (auto it = begin; it != end; ++it) {
      markSuccessors(*it);
    }

    process_mark_stack();

    // 2) Sweep entire list: delete all unreachable objects, reset metadata
    Collectable* cur = head_;
    while (cur) {
      Collectable* next = cur->next_;
      if (!cur->marked_) {
        // unreachable (young or old) → delete
        if (cur->prev_) cur->prev_->next_ = next;
        else head_ = next;
        if (next) next->prev_ = cur->prev_;

        purge_from_cache(cur);

        allocated_bytes_ -= cur->size_;
        objects_allocated_ -= 1;
        delete cur;
      } else {
        // live object: clear mark and treat as old
        cur->marked_ = false;
        cur->generation_ = 1;
        cur->in_remembered_ = false;
      }
      cur = next;
    }
    remembered_.clear();
  }

 private:
  // Explicit mark stack to avoid recursive marking
  void clear_mark_stack() {
    mark_stack_.clear();
    mark_stack_.shrink_to_fit();  // optional; remove if you want to avoid reallocation
  }

  void process_mark_stack() {
    // Non-recursive DFS over object graph
    while (!mark_stack_.empty()) {
      Collectable* obj = mark_stack_.back();
      mark_stack_.pop_back();
      // follow() will call markSuccessors() for children
      obj->follow(*this);
    }
  }

  // Remove any cache entries that point to this object (address-based)
  void purge_from_cache(Collectable* cur) {
    if (!cur || !allocation_cache) return;
    for (const auto& k : allocation_cache->keys()) {
      auto* cached = allocation_cache->get(k);
      if (reinterpret_cast<void*>(cached) ==
          reinterpret_cast<void*>(cur)) {
        allocation_cache->remove(k);
      }
    }
  }

  Collectable* head_ = nullptr;
  std::size_t allocated_bytes_ = 0;
  std::size_t objects_allocated_ = 0;

  // Mark stack used during marking phase
  std::vector<Collectable*> mark_stack_;
};
