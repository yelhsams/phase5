
#pragma once
#include "lrucache.hpp"
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

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
  Collectable *next_ = nullptr;
  Collectable *prev_ = nullptr;
  std::size_t size_ = 0;

  // Simple generational metadata
  uint8_t generation_ = 0;     // 0 = young, 1 = old (expandable)
  bool in_remembered_ = false; // tracked in remembered set when old → young

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
  virtual void follow(CollectedHeap &heap) = 0;

  friend class CollectedHeap;
};

/*
  This class keeps track of the garbage collected heap. The class must:
  - provide and implement method(s) for allocating objects that will be
    supported by garbage collection
  - keep track of all currently allocated objects
  - provide and implement a method that performs mark and sweep to deallocate
    objects that are not reachable from a given set of objects
*/
class CollectedHeap {
public:
  CollectedHeap() = default;
  mitscript::LRUCache<int> *allocation_cache =
      new mitscript::LRUCache<int>(1000);

  // Remembered set for old objects that may point to young ones
  std::vector<Collectable *> remembered_;

  // Simple heuristics to decide between minor and full collection
  std::size_t young_bytes_ = 0;
  std::size_t promoted_bytes_since_full_ = 0;
  std::size_t minor_since_full_ = 0;

  // Tunable knobs for GC pacing
  static constexpr std::size_t kYoungTargetBytes = 512 * 1024; // 512 KB
  static constexpr std::size_t kPromotionFullThreshold = 2 * 1024 * 1024;
  static constexpr std::size_t kFullThresholdBytes = 8 * 1024 * 1024;
  static constexpr std::size_t kMaxMinorBeforeFull = 6;

  bool is_young(Collectable *obj) const { return obj && obj->generation_ == 0; }
  bool is_old(Collectable *obj) const { return obj && obj->generation_ > 0; }

  void remember(Collectable *obj) {
    if (!obj || obj->in_remembered_ || !is_old(obj))
      return;
    remembered_.push_back(obj);
    obj->in_remembered_ = true;
  }

  void write_barrier(Collectable *owner, Collectable *child) {
    if (!owner || !child)
      return;
    if (is_old(owner) && is_young(child)) {
      remember(owner);
    }
  }
  /*
  T must be a subclass of Collectable.  Before returning the
  object, it should be registered so that it can be deallocated later.
  */
  template <typename T, typename... Args> T *allocate(Args &&...args) {
    static_assert(std::is_base_of_v<Collectable, T>,
                  "T must derive from Collectable");
    static_assert(std::is_constructible_v<T, Args...>,
                  "T must be constructible with Args...");

    // if constexpr (sizeof...(Args) == 1) {
    //   using First = std::decay_t<std::tuple_element_t<0,
    //   std::tuple<Args...>>>; if constexpr (std::is_same_v<First, int> &&
    //   std::is_convertible_v<T*, mitscript::Value*>) {
    //     auto tup = std::forward_as_tuple(std::forward<Args>(args)...);
    //     int key = std::get<0>(tup);
    //     if (auto* cached = allocation_cache->get(key)) {
    //       return static_cast<T*>(cached);
    //     }
    //     T* obj = new T(std::forward<Args>(args)...);
    //     obj->next_ = head_;
    //     obj->prev_ = nullptr;
    //     if (head_) head_->prev_ = obj;
    //     head_ = obj;
    //     obj->size_ = sizeof(T);
    //     allocated_bytes_ += obj->size_;
    //     objects_allocated_ += 1;
    //     allocation_cache->insert(key, static_cast<mitscript::Value*>(obj));
    //     return obj;
    //   }
    // }

    T *obj = new T(std::forward<Args>(args)...);

    // Inserting at head
    obj->next_ = head_;
    obj->prev_ = nullptr;
    if (head_)
      head_->prev_ = obj;
    head_ = obj;
    obj->size_ = sizeof(T);
    obj->generation_ = 0;
    obj->in_remembered_ = false;
    allocated_bytes_ += obj->size_;
    objects_allocated_ += 1;
    young_bytes_ += obj->size_;
    return obj;
  }

  /*
  This is the method that is called by the follow(...) method of a Collectable
  object.  This is how a Collectable object lets the garbage collector know
  about other Collectable objects pointed to by itself.
  */
  void markSuccessors(Collectable *next) {
    if (!next)
      return;

    if (next->marked_)
      return;
    next->marked_ = true;
    next->follow(*this);
  }

  /*
  The gc method should be periodically invoked by your VM (or by other methods
  in CollectedHeap) whenever the VM decides it is time to reclaim memory.  This
  method triggers the mark and sweep process over the provided root set.
  */
  template <typename Iterator> void gc(Iterator begin, Iterator end) {
    bool force_full = allocated_bytes_ >= kFullThresholdBytes ||
                      promoted_bytes_since_full_ >= kPromotionFullThreshold ||
                      minor_since_full_ >= kMaxMinorBeforeFull;

    if (!force_full && young_bytes_ > 0) {
      minor_gc(begin, end);
      minor_since_full_++;
      // If nursery remains large after a minor collection, fall back to full
      if (young_bytes_ > kYoungTargetBytes ||
          promoted_bytes_since_full_ >= kPromotionFullThreshold) {
        force_full = true;
      }
    } else {
      force_full = true;
    }

    if (force_full) {
      full_gc(begin, end);
      minor_since_full_ = 0;
      promoted_bytes_since_full_ = 0;
    }
  }

  template <typename Iterator> void minor_gc(Iterator begin, Iterator end) {
    std::vector<Collectable *> new_remembered;
    new_remembered.reserve(remembered_.size());

    for (auto it = begin; it != end; ++it) {
      markSuccessors(*it);
    }

    // Remembered old objects are treated as roots to preserve young
    // successors without forcing a full-heap traversal.
    for (Collectable *obj : remembered_) {
      if (obj) {
        markSuccessors(obj);
      }
    }

    Collectable *cur = head_;
    while (cur) {
      Collectable *next = cur->next_;
      if (!cur->marked_) {
        if (is_young(cur)) {
          if (cur->prev_)
            cur->prev_->next_ = next;
          else
            head_ = next;

          if (next)
            next->prev_ = cur->prev_;

          while (allocation_cache->removeValue(
              reinterpret_cast<mitscript::Value *>(cur))) {
          }
          allocated_bytes_ -= cur->size_;
          objects_allocated_ -= 1;
          young_bytes_ -= cur->size_;
          delete cur;
        }
        // unreachable old objects collected only in full GC
      } else {
        cur->marked_ = false;
        if (is_young(cur)) {
          cur->generation_ = 1;
          promoted_bytes_since_full_ += cur->size_;
          young_bytes_ -= cur->size_;
        } else if (cur->in_remembered_) {
          new_remembered.push_back(cur);
        }
      }
      cur = next;
    }

    remembered_.swap(new_remembered);
  }

  template <typename Iterator> void full_gc(Iterator begin, Iterator end) {
    for (auto it = begin; it != end; ++it) {
      markSuccessors(*it);
    }

    Collectable *cur = head_;
    while (cur) {
      Collectable *next = cur->next_;
      if (!cur->marked_) {

        if (cur->prev_)
          cur->prev_->next_ = next;

        else
          head_ = next;

        if (next)
          next->prev_ = cur->prev_;

        // Purge any cache entries that point to this object (compare addresses)
        while (allocation_cache->removeValue(
            reinterpret_cast<mitscript::Value *>(cur))) {
        }
        allocated_bytes_ -= cur->size_;
        objects_allocated_ -= 1;
        delete cur;

      } else {
        cur->marked_ = false;
        cur->generation_ = 1;
        cur->in_remembered_ = false;
      }
      cur = next;
    }
    remembered_.clear();
    young_bytes_ = 0;
    promoted_bytes_since_full_ = 0;
    minor_since_full_ = 0;
  }

private:
  Collectable *head_ = nullptr;
  std::size_t allocated_bytes_ = 0;
  std::size_t objects_allocated_ = 0;
};
