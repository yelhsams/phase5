#pragma once

#include <cstddef>
#include <type_traits>
#include <unordered_map>
#include <string>
#include <vector>

namespace mitscript {

// Forward declare to avoid including interpreter implementation
class Value;

// Restrict allowed key types for LRUCache
template <class T> struct is_cacheable : std::false_type {};
template <> struct is_cacheable<int> : std::true_type {};
template <> struct is_cacheable<std::string> : std::true_type {};

template <class T>
struct ListNode {
	T key;
	Value* value;
	ListNode<T>* prev;
	ListNode<T>* next;
	ListNode(const T& k, Value* v) : key(k), value(v), prev(nullptr), next(nullptr) {}
};

template <class T>
class LRUCache {
	static_assert(is_cacheable<T>::value, "Type not allowed for LRUCache");

 public:
	explicit LRUCache(std::size_t capacity) : capacity_(capacity) {}

	Value* get(const T& key) {
		auto it = map_.find(key);
		if (it == map_.end()) return nullptr;
		moveToHead(it->second);
		return it->second->value;
	}

	void insert(const T& key, Value* value) {
		auto it = map_.find(key);
		if (it != map_.end()) {
			it->second->value = value;
			moveToHead(it->second);
			return;
		}
		auto* node = new ListNode<T>(key, value);
		// Insert at head as MRU
		node->next = head_;
		node->prev = nullptr;
		if (head_) head_->prev = node;
		head_ = node;
		if (!tail_) tail_ = node;
		map_[key] = node;

		if (map_.size() > capacity_) evictLRU();
	}

	~LRUCache() {
		auto* p = head_;
		while (p) {
			auto* nxt = p->next;
			delete p;
			p = nxt;
		}
	}

	std::vector<T> keys() const {
		std::vector<T> ks;
		ks.reserve(map_.size());
		for (const auto& kv : map_) ks.push_back(kv.first);
		return ks;
	}

	void remove(const T& key) {
        auto it = map_.find(key);
        if (it == map_.end()) return;
        auto* node = it->second;
        // unlink node
        if (node->prev) node->prev->next = node->next;
        else            head_ = node->next;
        if (node->next) node->next->prev = node->prev;
        else            tail_ = node->prev;
        map_.erase(it);
        delete node;
    }


 private:
	std::size_t capacity_;
	std::unordered_map<T, ListNode<T>*> map_;
	ListNode<T>* head_ = nullptr;
	ListNode<T>* tail_ = nullptr;

	void moveToHead(ListNode<T>* node) {
		if (node == head_) return;
		// unlink
		if (node->prev) node->prev->next = node->next;
		if (node->next) node->next->prev = node->prev;
		if (node == tail_) tail_ = node->prev;
		// push front
		node->prev = nullptr;
		node->next = head_;
		if (head_) head_->prev = node;
		head_ = node;
		if (!tail_) tail_ = node;
	}

	void evictLRU() {
		if (!tail_) return;
		auto* victim = tail_;
		// unlink tail
		tail_ = victim->prev;
		if (tail_) tail_->next = nullptr; else head_ = nullptr;
		map_.erase(victim->key);
		delete victim;
	}
};

} // namespace mitscript
