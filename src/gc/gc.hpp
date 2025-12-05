
#pragma once
#include <type_traits>
#include <vector>
#include <utility>
#include <cstddef>
#include <tuple>
#include <cstdint>
#include <unordered_set>
#include <unordered_map>
#include "lrucache.hpp"

class CollectedHeap;

// Any object that inherits from collectable can be created and tracked by the
// garbage collector.
class Collectable {
 public:
  virtual ~Collectable() = default;

 private:
  // Any private fields you add to the Collectable class will be accessible by
  // the CollectedHeap (since it is declared as friend below).  You can think of
  // these fields as the header for the object, which will include metadata that
  // is useful for the garbage collector.
  bool marked_ = false;

  // Doubly-linked list pointers for generation-specific lists
  Collectable* next_ = nullptr;
  Collectable* prev_ = nullptr;
  std::size_t size_ = 0;

  // Generational metadata
  uint8_t generation_ = 0;      // 0 = young, 1+ = old
  bool in_remembered_ = false;  // tracked in remembered set when old → young
  uint8_t survival_count_ = 0;  // number of GC cycles survived (for tenure threshold)

 protected:
  /*
  The mark phase of the garbage collector needs to follow all pointers from the
  collectable objects, check if those objects have been marked, and if they
  have not, mark them and follow their pointers.  The simplest way to implement
  this is to require that collectable objects implement a follow method that
  calls heap.markSuccessors(...) on all collectable objects that this object
  points to.  markSuccessors() is the one responsible for checking if the
  object is marked and marking it.
  */
  virtual void follow(CollectedHeap& heap) = 0;

  friend class CollectedHeap;
};


/*
  This class keeps track of the garbage collected heap. The class must:
  - provide and implement method(s) for allocating objects that will be
    supported by garbage collection
  - keep track of all currently allocated objects
  - provide and implement a method that performs mark and sweep to deallocate
    objects that are not reachable from a given set of objects

  GENERATIONAL GC DESIGN:
  - Young generation (nursery): Recently allocated objects, collected frequently
  - Old generation (tenured): Long-lived objects, collected less often
  - Objects are promoted to old gen after surviving TENURE_THRESHOLD minor GCs
  - Write barrier tracks old→young references in remembered set
  - Minor GC: Only scans young generation + remembered set (fast)
  - Full GC: Scans entire heap (slower but thorough)
*/
class CollectedHeap {
 public:
  // Tuning parameters for generational GC
  static constexpr uint8_t TENURE_THRESHOLD = 2;     // Survive 2 minor GCs before promotion
  static constexpr size_t YOUNG_GEN_THRESHOLD = 64 * 1024;  // 64KB triggers minor GC
  static constexpr size_t MINOR_GC_RATIO = 4;        // 4 minor GCs before considering full GC

  CollectedHeap() = default;

  ~CollectedHeap() {
    // Clean up any remaining objects
    Collectable* cur = young_head_;
    while (cur) {
      Collectable* next = cur->next_;
      delete cur;
      cur = next;
    }
    cur = old_head_;
    while (cur) {
      Collectable* next = cur->next_;
      delete cur;
      cur = next;
    }
    delete allocation_cache;
  }

  mitscript::LRUCache<int>* allocation_cache = new mitscript::LRUCache<int>(1000);

  // Remembered set for old objects that may point to young ones (using set for O(1) contains check)
  std::unordered_set<Collectable*> remembered_set_;

  // Reverse map: object pointer -> cache key (for O(1) cache invalidation)
  std::unordered_map<Collectable*, int> cache_reverse_map_;

  bool is_young(Collectable* obj) const { return obj && obj->generation_ == 0; }
  bool is_old(Collectable* obj) const { return obj && obj->generation_ > 0; }

  // Statistics accessors for adaptive policy
  size_t young_bytes() const { return young_allocated_bytes_; }
  size_t old_bytes() const { return old_allocated_bytes_; }
  size_t total_bytes() const { return young_allocated_bytes_ + old_allocated_bytes_; }
  size_t young_objects() const { return young_objects_count_; }
  size_t old_objects() const { return old_objects_count_; }
  size_t minor_gc_count() const { return minor_gc_count_; }
  size_t full_gc_count() const { return full_gc_count_; }

  void remember(Collectable* obj) {
    if (!obj || obj->in_remembered_ || !is_old(obj)) return;
    remembered_set_.insert(obj);
    obj->in_remembered_ = true;
  }

  void write_barrier(Collectable* owner, Collectable* child) {
    if (!owner || !child) return;
    if (is_old(owner) && is_young(child)) {
      remember(owner);
    }
  }

  /*
  T must be a subclass of Collectable.  Before returning the
  object, it should be registered so that it can be deallocated later.
  All new objects are allocated in the young generation.
  */
  template <typename T, typename... Args>
  T* allocate(Args&&... args) {
    static_assert(std::is_base_of_v<Collectable, T>, "T must derive from Collectable");
    static_assert(std::is_constructible_v<T, Args...>, "T must be constructible with Args...");

    T* obj = new T(std::forward<Args>(args)...);

    // Insert into young generation list
    obj->next_ = young_head_;
    obj->prev_ = nullptr;
    if (young_head_) young_head_->prev_ = obj;
    young_head_ = obj;

    obj->size_ = sizeof(T);
    obj->generation_ = 0;
    obj->in_remembered_ = false;
    obj->survival_count_ = 0;

    young_allocated_bytes_ += obj->size_;
    young_objects_count_ += 1;

    return obj;
  }

  /*
  This is the method that is called by the follow(...) method of a Collectable
  object.  This is how a Collectable object lets the garbage collector know
  about other Collectable objects pointed to by itself.
  */
  void markSuccessors(Collectable* next) {
    if (!next) return;
    if (next->marked_) return;
    next->marked_ = true;
    next->follow(*this);
  }

  // Mark only young objects reachable from roots (used during minor GC)
  void markYoungSuccessors(Collectable* next) {
    if (!next) return;
    if (next->marked_) return;
    next->marked_ = true;
    // Continue following all references (old objects may point to young)
    next->follow(*this);
  }

  /*
  Determines if minor GC should be used based on heuristics.
  Returns true if minor GC is recommended, false for full GC.
  */
  bool should_minor_gc() const {
    // If we haven't done enough minor GCs, prefer minor
    if (minor_gc_count_ < MINOR_GC_RATIO * (full_gc_count_ + 1)) {
      return true;
    }
    // If old generation is growing too large relative to young, do full GC
    if (old_allocated_bytes_ > young_allocated_bytes_ * 4) {
      return false;
    }
    return true;
  }

  /*
  The gc method should be periodically invoked by your VM (or by other methods
  in CollectedHeap) whenever the VM decides it is time to reclaim memory.  This
  method triggers the mark and sweep process over the provided root set.
  Uses adaptive policy to choose between minor and full GC.
  */
  template <typename Iterator>
  void gc(Iterator begin, Iterator end) {
    if (should_minor_gc()) {
      minor_gc(begin, end);
    } else {
      full_gc(begin, end);
    }
  }

  /*
  Minor GC: Only collects young generation objects.
  - Marks from roots
  - Also marks from remembered set (old objects pointing to young)
  - Only sweeps young generation
  - Promotes surviving young objects to old generation
  */
  template <typename Iterator>
  void minor_gc(Iterator begin, Iterator end) {
    minor_gc_count_++;

    // Mark phase: mark from roots
    for (auto it = begin; it != end; ++it) {
      markYoungSuccessors(*it);
    }

    // Also mark from remembered set (old objects that point to young objects)
    for (Collectable* obj : remembered_set_) {
      if (obj->marked_) continue;  // Already processed
      obj->marked_ = true;
      obj->follow(*this);
    }

    // Sweep phase: only sweep young generation
    Collectable* cur = young_head_;
    Collectable* new_young_head = nullptr;
    Collectable* new_young_tail = nullptr;

    std::vector<Collectable*> to_promote;

    while (cur) {
      Collectable* next = cur->next_;

      if (!cur->marked_) {
        // Object is unreachable - delete it
        invalidate_cache_entry(cur);
        young_allocated_bytes_ -= cur->size_;
        young_objects_count_ -= 1;
        delete cur;
      } else {
        // Object survived - check for promotion
        cur->marked_ = false;
        cur->survival_count_++;

        if (cur->survival_count_ >= TENURE_THRESHOLD) {
          // Promote to old generation
          to_promote.push_back(cur);
        } else {
          // Keep in young generation - rebuild list
          cur->prev_ = new_young_tail;
          cur->next_ = nullptr;
          if (new_young_tail) {
            new_young_tail->next_ = cur;
          } else {
            new_young_head = cur;
          }
          new_young_tail = cur;
        }
      }
      cur = next;
    }

    young_head_ = new_young_head;

    // Promote surviving objects to old generation
    for (Collectable* obj : to_promote) {
      promote_to_old(obj);
    }

    // Clear marks on old generation objects (they may have been marked via remembered set)
    cur = old_head_;
    while (cur) {
      cur->marked_ = false;
      cur = cur->next_;
    }

    // Clean up remembered set: remove entries for objects no longer pointing to young
    clean_remembered_set();
  }

  /*
  Full GC: Collects both young and old generations.
  - Marks from roots across entire heap
  - Sweeps both generations
  - All surviving young objects are promoted
  */
  template <typename Iterator>
  void full_gc(Iterator begin, Iterator end) {
    full_gc_count_++;

    // Mark phase: mark from roots
    for (auto it = begin; it != end; ++it) {
      markSuccessors(*it);
    }

    // Sweep young generation
    Collectable* cur = young_head_;
    while (cur) {
      Collectable* next = cur->next_;
      if (!cur->marked_) {
        invalidate_cache_entry(cur);
        young_allocated_bytes_ -= cur->size_;
        young_objects_count_ -= 1;
        delete cur;
      } else {
        cur->marked_ = false;
        // Promote all surviving young objects to old during full GC
        promote_to_old(cur);
      }
      cur = next;
    }
    young_head_ = nullptr;

    // Sweep old generation
    cur = old_head_;
    Collectable* new_old_head = nullptr;
    Collectable* new_old_tail = nullptr;

    while (cur) {
      Collectable* next = cur->next_;
      if (!cur->marked_) {
        // Remove from remembered set if present
        if (cur->in_remembered_) {
          remembered_set_.erase(cur);
        }
        invalidate_cache_entry(cur);
        old_allocated_bytes_ -= cur->size_;
        old_objects_count_ -= 1;
        delete cur;
      } else {
        cur->marked_ = false;
        cur->in_remembered_ = false;

        // Rebuild old list
        cur->prev_ = new_old_tail;
        cur->next_ = nullptr;
        if (new_old_tail) {
          new_old_tail->next_ = cur;
        } else {
          new_old_head = cur;
        }
        new_old_tail = cur;
      }
      cur = next;
    }

    old_head_ = new_old_head;
    remembered_set_.clear();
  }

 private:
  // Separate lists for young and old generations
  Collectable* young_head_ = nullptr;
  Collectable* old_head_ = nullptr;

  // Statistics for young generation
  std::size_t young_allocated_bytes_ = 0;
  std::size_t young_objects_count_ = 0;

  // Statistics for old generation
  std::size_t old_allocated_bytes_ = 0;
  std::size_t old_objects_count_ = 0;

  // GC cycle counts for adaptive policy
  std::size_t minor_gc_count_ = 0;
  std::size_t full_gc_count_ = 0;

  // Move an object from young to old generation
  void promote_to_old(Collectable* obj) {
    // Update statistics
    young_allocated_bytes_ -= obj->size_;
    young_objects_count_ -= 1;
    old_allocated_bytes_ += obj->size_;
    old_objects_count_ += 1;

    // Mark as old
    obj->generation_ = 1;
    obj->survival_count_ = 0;

    // Insert at head of old list
    obj->next_ = old_head_;
    obj->prev_ = nullptr;
    if (old_head_) old_head_->prev_ = obj;
    old_head_ = obj;
  }

  // O(1) cache invalidation using reverse map
  void invalidate_cache_entry(Collectable* obj) {
    auto it = cache_reverse_map_.find(obj);
    if (it != cache_reverse_map_.end()) {
      allocation_cache->remove(it->second);
      cache_reverse_map_.erase(it);
    }
  }

  // Clean remembered set by removing entries that no longer point to young objects
  void clean_remembered_set() {
    // After a minor GC, some old objects may no longer point to young objects
    // This is called periodically to avoid remembered set bloat
    // For now, we clear any object that doesn't have young references
    // (This is a simplified version - a more sophisticated implementation
    // would track this more precisely during mutation)

    // Only clean periodically to avoid overhead
    if (minor_gc_count_ % 8 != 0) return;

    std::vector<Collectable*> to_remove;
    for (Collectable* obj : remembered_set_) {
      // We'd need to re-scan to know if it still points to young
      // For simplicity, we keep all entries and let them expire naturally
      // A more sophisticated approach would track reference counts
    }
  }
};
