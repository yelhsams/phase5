#pragma once

#include "bytecode/instructions.hpp"
#include "bytecode/types.hpp"
#include "gc/gc.hpp"
#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

//
namespace vm {

// Forward declarations
class Value;
class Record;
class Reference;
class Closure;

// Lightweight tagged value used for VM stack/locals to avoid heap boxing.
struct TaggedValue {
  enum class Kind : uint8_t { None, Boolean, Integer, HeapPtr };
  Kind kind;
  union {
    int32_t i;
    bool b;
    Value *ptr;
  };

  static TaggedValue none() {
    TaggedValue tv;
    tv.kind = Kind::None;
    tv.i = 0;
    return tv;
  }
  static TaggedValue from_int(int32_t v) {
    TaggedValue tv;
    tv.kind = Kind::Integer;
    tv.i = v;
    return tv;
  }
  static TaggedValue from_bool(bool v) {
    TaggedValue tv;
    tv.kind = Kind::Boolean;
    tv.b = v;
    return tv;
  }
  static TaggedValue from_heap(Value *p) {
    TaggedValue tv;
    tv.kind = Kind::HeapPtr;
    tv.ptr = p;
    return tv;
  }
};

// Exceptions
class UninitializedVariableException : public std::exception {
  std::string msg;

public:
  explicit UninitializedVariableException(const std::string &message)
      : msg("UninitializedVariableException: " + message) {}
  const char *what() const noexcept override { return msg.c_str(); }
};

class IllegalCastException : public std::exception {
  std::string msg;

public:
  explicit IllegalCastException(const std::string &message)
      : msg("IllegalCastException: " + message) {}
  const char *what() const noexcept override { return msg.c_str(); }
};

class IllegalArithmeticException : public std::exception {
  std::string msg;

public:
  explicit IllegalArithmeticException(const std::string &message)
      : msg("IllegalArithmeticException: " + message) {}
  const char *what() const noexcept override { return msg.c_str(); }
};

class InsufficientStackException : public std::exception {
  std::string msg;

public:
  explicit InsufficientStackException(const std::string &message)
      : msg("InsufficientStackException: " + message) {}
  const char *what() const noexcept override { return msg.c_str(); }
};

class RuntimeException : public std::exception {
  std::string msg;

public:
  explicit RuntimeException(const std::string &message)
      : msg("RuntimeException: " + message) {}
  const char *what() const noexcept override { return msg.c_str(); }
};

// Runtime value types that inherit from Collectable
class Value : public Collectable {
public:
  enum class Type {
    None,
    Boolean,
    Integer,
    String,
    Record,
    Function,
    Closure,
    Reference
  };

  const Type tag;

  explicit Value(Type t) : tag(t) {}

  Type type() const { return tag; }
  virtual std::string toString() const = 0;
  virtual ~Value() = default;

  // Helper methods for type checking
  bool isNone() const { return tag == Type::None; }
  bool isBoolean() const { return tag == Type::Boolean; }
  bool isInteger() const { return tag == Type::Integer; }
  bool isString() const { return tag == Type::String; }
  bool isRecord() const { return tag == Type::Record; }
  bool isFunction() const { return tag == Type::Function; }
  bool isClosure() const { return tag == Type::Closure; }
  bool isReference() const { return tag == Type::Reference; }
};

class None : public Value {
public:
  None() : Value(Type::None) {}
  std::string toString() const override { return "None"; }

protected:
  void follow(CollectedHeap &) override {
    // None has no references
  }
};

class Boolean : public Value {
public:
  bool value;
  Boolean(bool v) : Value(Type::Boolean), value(v) {}
  std::string toString() const override { return value ? "true" : "false"; }

protected:
  void follow(CollectedHeap &) override {
    // Boolean has no references
  }
};

class Integer : public Value {
public:
  int32_t value; // Changed to int32_t per spec
  Integer(int32_t v) : Value(Type::Integer), value(v) {}
  std::string toString() const override { return std::to_string(value); }

protected:
  void follow(CollectedHeap &) override {
    // Integer has no references
  }
};

class String : public Value {
public:
  std::string value;
  String(const std::string &v) : Value(Type::String), value(v) {}
  std::string toString() const override { return value; }

protected:
  void follow(CollectedHeap &) override {
    // String has no references
  }
};

class Record : public Value {
public:
  std::unordered_map<std::string, Value *> fields;
  bool dense_mode = true;            // start as dense integer array
  std::vector<TaggedValue> dense;    // fast path storage for int keys
  std::vector<Value *> named_fields; // indexed by integer field id
  // std::map<int64_t, Value *> indices;
  // size_t next_index = 0;

  Record() : Value(Type::Record) {}

  std::string toString() const override {
    auto tagged_to_string_local = [](const TaggedValue &tv) -> std::string {
      switch (tv.kind) {
      case TaggedValue::Kind::None:
        return "None";
      case TaggedValue::Kind::Boolean:
        return tv.b ? "true" : "false";
      case TaggedValue::Kind::Integer:
        return std::to_string(tv.i);
      case TaggedValue::Kind::HeapPtr:
        return tv.ptr ? tv.ptr->toString() : "None";
      }
      return "None";
    };

    std::string result = "{";

    std::vector<std::pair<std::string, std::string>> entries;

    if (dense_mode) {
      for (size_t i = 0; i < dense.size(); ++i) {
        const TaggedValue &tv = dense[i];
        if (tv.kind == TaggedValue::Kind::None)
          continue;
        entries.emplace_back(std::to_string(i), tagged_to_string_local(tv));
      }
    }

    for (const auto &pair : fields) {
      entries.emplace_back(pair.first, pair.second->toString());
    }

    std::sort(entries.begin(), entries.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });

    for (const auto &entry : entries) {
      result += entry.first + ":" + entry.second + " ";
    }
    result += "}";
    return result;
  }

protected:
  void follow(CollectedHeap &heap) override {
    // Mark dense storage
    if (dense_mode) {
      for (const auto &tv : dense) {
        if (tv.kind == TaggedValue::Kind::HeapPtr && tv.ptr) {
          heap.markSuccessors(tv.ptr);
        }
      }
    }

    for (Value *val : named_fields) {
      if (val) {
        heap.markSuccessors(val);
      }
    }

    for (auto &[name, val] : fields) {
      heap.markSuccessors(val);
    }
    // for (auto &[idx, val] : indices) {
    //   heap.markSuccessors(val);
    // }
  }
};

class Function : public Value {
public:
  bytecode::Function *func;
  Function(bytecode::Function *f) : Value(Type::Function), func(f) {}
  std::string toString() const override { return "FUNCTION"; }

protected:
  void follow(CollectedHeap &) override {
    // Function pointer to bytecode - no GC references
  }
};

class Reference : public Value {
public:
  Value *cell; // Points to the heap-allocated cell containing the actual value
  Reference(Value *c) : Value(Type::Reference), cell(c) {}
  std::string toString() const override {
    throw RuntimeException("Cannot toString a Reference");
  }

protected:
  void follow(CollectedHeap &heap) override { heap.markSuccessors(cell); }
};

class Closure : public Value {
public:
  bytecode::Function *function;
  std::vector<Value *> free_var_refs;

  Closure(bytecode::Function *func, std::vector<Value *> refs)
      : Value(Type::Closure), function(func), free_var_refs(std::move(refs)) {}
  std::string toString() const override { return "FUNCTION"; }

protected:
  void follow(CollectedHeap &heap) override {
    for (Value *ref : free_var_refs) {
      heap.markSuccessors(ref);
    }
  }
};

// Stack frame
struct Frame {
  std::vector<TaggedValue> locals;
  std::set<size_t> ref_locals; // Indices that are reference variables
  std::unordered_map<std::string, Value *>
      local_refs; // Map from var name to Reference object
  std::vector<TaggedValue> stack;
  size_t sp = 0; // stack pointer
  size_t pc;

  // Pointer to the current function and its free refs
  bytecode::Function *func;
  const std::vector<Value *> *free_refs;

  Frame(size_t local_count, bytecode::Function *f,
        const std::vector<Value *> *fr)
      : locals(local_count, TaggedValue::none()), pc(0), func(f),
        free_refs(fr) {
    stack.reserve(256);
  }
};

// Virtual Machine
class VM {
private:
  bool jit_enabled = false;
  CollectedHeap heap;
  std::vector<TaggedValue> globals_by_id;
  std::vector<bool> globals_initialized;
  std::unordered_map<std::string, size_t> global_name_to_slot;
  std::unordered_map<bytecode::Function *, std::vector<size_t>>
      global_slot_cache;
  std::unordered_map<std::string, size_t> field_name_to_slot;
  std::unordered_map<bytecode::Function *, std::vector<size_t>>
      field_slot_cache;
  static constexpr size_t kInvalidSlot = std::numeric_limits<size_t>::max();
  size_t max_heap_bytes;
  std::unordered_map<bytecode::Function *, int>
      native_functions; // Map function to native ID
  std::unordered_map<bytecode::Constant *, Value *> constant_cache;

  // For GC - track current execution state
  std::vector<Frame *> call_stack;

  // Allocation tracking for GC trigger
  size_t curr_heap_bytes = 0;

  // Singletons for commonly-used immutable values
  Value *none_singleton = nullptr;
  Value *bool_true_singleton = nullptr;
  Value *bool_false_singleton = nullptr;

  struct JitBlock {
    using EntryPoint = TaggedValue (*)(VM *, bytecode::Function *,
                                       const std::vector<TaggedValue> *,
                                       const std::vector<Value *> *);

    EntryPoint entry = nullptr;
    size_t code_size = 0;
    void *memory = nullptr;
  };

  std::unordered_map<bytecode::Function *, JitBlock> jitted_functions;

  // Wrapper for heap allocation that triggers GC periodically
  template <typename T, typename... Args> T *allocate(Args &&...args) {
    curr_heap_bytes += sizeof(T);
    if (curr_heap_bytes >= max_heap_bytes) {
      maybe_gc();
      curr_heap_bytes = 0;
    }
    return heap.allocate<T>(std::forward<Args>(args)...);
  }

  TaggedValue pop(Frame &frame) {
    if (frame.sp == 0) {
      throw InsufficientStackException("Cannot pop from empty stack");
    }
    frame.sp--;
    return frame.stack[frame.sp];
  }

  void push(Frame &frame, TaggedValue v) {
    if (frame.sp == frame.stack.size()) {
      frame.stack.push_back(v);
    } else {
      frame.stack[frame.sp] = v;
    }
    frame.sp++;
  }

  int32_t get_int(Value *v) {
    if (v->tag == Value::Type::Integer)
      return static_cast<Integer *>(v)->value;
    throw IllegalCastException("Expected integer");
  }

  int32_t get_int(const TaggedValue &tv) {
    switch (tv.kind) {
    case TaggedValue::Kind::Integer:
      return tv.i;
    case TaggedValue::Kind::HeapPtr:
      return get_int(tv.ptr);
    default:
      throw IllegalCastException("Expected integer");
    }
  }

  bool get_bool(Value *v) {
    if (v->tag == Value::Type::Boolean)
      return static_cast<Boolean *>(v)->value;
    throw IllegalCastException("Expected boolean");
  }

  bool get_bool(const TaggedValue &tv) {
    switch (tv.kind) {
    case TaggedValue::Kind::Boolean:
      return tv.b;
    case TaggedValue::Kind::HeapPtr:
      return get_bool(tv.ptr);
    default:
      throw IllegalCastException("Expected boolean");
    }
  }

  std::string get_string(Value *v) {
    if (v->tag == Value::Type::String)
      return static_cast<String *>(v)->value;
    throw IllegalCastException("Expected string");
  }

  std::string tagged_to_string(const TaggedValue &tv) {
    switch (tv.kind) {
    case TaggedValue::Kind::None:
      return "None";
    case TaggedValue::Kind::Boolean:
      return tv.b ? "true" : "false";
    case TaggedValue::Kind::Integer:
      return std::to_string(tv.i);
    case TaggedValue::Kind::HeapPtr:
      return tv.ptr->toString();
    }
    return "None";
  }

  TaggedValue tagged_from_value(Value *v) {
    if (!v)
      return TaggedValue::none();
    switch (v->tag) {
    case Value::Type::None:
      return TaggedValue::none();
    case Value::Type::Boolean:
      return TaggedValue::from_bool(static_cast<Boolean *>(v)->value);
    case Value::Type::Integer:
      return TaggedValue::from_int(static_cast<Integer *>(v)->value);
    case Value::Type::String:
    case Value::Type::Record:
    case Value::Type::Function:
    case Value::Type::Closure:
    case Value::Type::Reference:
      return TaggedValue::from_heap(v);
    }
    return TaggedValue::none();
  }

  Value *box_tagged(const TaggedValue &tv) {
    switch (tv.kind) {
    case TaggedValue::Kind::None:
      return none_singleton;
    case TaggedValue::Kind::Boolean:
      return tv.b ? bool_true_singleton : bool_false_singleton;
    case TaggedValue::Kind::Integer:
      return allocate<Integer>(tv.i);
    case TaggedValue::Kind::HeapPtr:
      return tv.ptr;
    }
    return none_singleton;
  }

  bool tagged_to_int(const TaggedValue &tv, int32_t &out) const {
    switch (tv.kind) {
    case TaggedValue::Kind::Integer:
      out = tv.i;
      return true;
    case TaggedValue::Kind::HeapPtr:
      if (tv.ptr && tv.ptr->tag == Value::Type::Integer) {
        out = static_cast<Integer *>(tv.ptr)->value;
        return true;
      }
      return false;
    default:
      return false;
    }
  }

  void degrade_record_to_map(Record *rec) {
    if (!rec || !rec->dense_mode)
      return;
    for (size_t i = 0; i < rec->dense.size(); ++i) {
      const TaggedValue &tv = rec->dense[i];
      if (tv.kind == TaggedValue::Kind::None)
        continue;
      Value *boxed = box_tagged(tv);
      std::string key = std::to_string(i);
      rec->fields[key] = boxed;
      heap.write_barrier(rec, boxed);
    }
    rec->dense.clear();
    rec->dense_mode = false;
  }

  bool record_try_dense_store(Record *rec, const TaggedValue &idx_tv,
                              const TaggedValue &val_tv) {
    if (!rec || !rec->dense_mode)
      return false;
    int32_t idx = -1;
    if (!tagged_to_int(idx_tv, idx) || idx < 0) {
      // Non-integer key degrades to generic map mode.
      if (!(idx_tv.kind == TaggedValue::Kind::Integer ||
            (idx_tv.kind == TaggedValue::Kind::HeapPtr && idx_tv.ptr &&
             idx_tv.ptr->tag == Value::Type::Integer))) {
        degrade_record_to_map(rec);
      }
      return false;
    }

    size_t uidx = static_cast<size_t>(idx);
    if (uidx >= rec->dense.size()) {
      rec->dense.resize(uidx + 1, TaggedValue::none());
    }
    rec->dense[uidx] = val_tv;
    if (val_tv.kind == TaggedValue::Kind::HeapPtr && val_tv.ptr) {
      heap.write_barrier(rec, val_tv.ptr);
    }
    return true;
  }

  bool record_try_dense_load(Record *rec, const TaggedValue &idx_tv,
                             TaggedValue &out) {
    if (!rec || !rec->dense_mode)
      return false;
    int32_t idx = -1;
    if (!tagged_to_int(idx_tv, idx) || idx < 0) {
      // Only degrade on non-integer keys.
      if (!(idx_tv.kind == TaggedValue::Kind::Integer ||
            (idx_tv.kind == TaggedValue::Kind::HeapPtr && idx_tv.ptr &&
             idx_tv.ptr->tag == Value::Type::Integer))) {
        degrade_record_to_map(rec);
      }
      return false;
    }
    size_t uidx = static_cast<size_t>(idx);
    if (uidx < rec->dense.size()) {
      out = rec->dense[uidx];
    } else {
      out = TaggedValue::none();
    }
    return true;
  }

  TaggedValue record_map_load(Record *rec, const TaggedValue &idx_tv) {
    if (!rec)
      throw IllegalCastException("Expected record");
    std::string key;
    if (idx_tv.kind == TaggedValue::Kind::Integer) {
      key = std::to_string(idx_tv.i);
    } else if (idx_tv.kind == TaggedValue::Kind::HeapPtr && idx_tv.ptr &&
               idx_tv.ptr->tag == Value::Type::Integer) {
      key = std::to_string(static_cast<Integer *>(idx_tv.ptr)->value);
    } else if (idx_tv.kind == TaggedValue::Kind::HeapPtr && idx_tv.ptr &&
               idx_tv.ptr->tag == Value::Type::String) {
      key = static_cast<String *>(idx_tv.ptr)->value;
      size_t slot = ensure_field_slot(key);
      if (slot < rec->named_fields.size()) {
        if (Value *named = rec->named_fields[slot]) {
          return tagged_from_value(named);
        }
      }
    } else {
      throw IllegalCastException("Invalid index type");
    }

    auto it = rec->fields.find(key);
    if (it != rec->fields.end())
      return tagged_from_value(it->second);
    return TaggedValue::none();
  }

  void record_map_store(Record *rec, const TaggedValue &idx_tv,
                        const TaggedValue &val_tv) {
    if (!rec)
      throw IllegalCastException("Expected record");
    std::string key;
    if (idx_tv.kind == TaggedValue::Kind::Integer) {
      key = std::to_string(idx_tv.i);
    } else if (idx_tv.kind == TaggedValue::Kind::HeapPtr && idx_tv.ptr &&
               idx_tv.ptr->tag == Value::Type::Integer) {
      key = std::to_string(static_cast<Integer *>(idx_tv.ptr)->value);
    } else if (idx_tv.kind == TaggedValue::Kind::HeapPtr && idx_tv.ptr &&
               idx_tv.ptr->tag == Value::Type::String) {
      key = static_cast<String *>(idx_tv.ptr)->value;
    } else {
      throw IllegalCastException("Invalid index type");
    }

    Value *boxed = box_tagged(val_tv);
    rec->fields[key] = boxed;
    heap.write_barrier(rec, boxed);

    // Keep named_fields in sync for string keys to preserve fast path lookups.
    if (idx_tv.kind == TaggedValue::Kind::HeapPtr && idx_tv.ptr &&
        idx_tv.ptr->tag == Value::Type::String) {
      size_t slot = ensure_field_slot(key);
      if (slot >= rec->named_fields.size()) {
        rec->named_fields.resize(slot + 1, nullptr);
      }
      rec->named_fields[slot] = boxed;
    }
  }

  size_t ensure_global_slot(const std::string &name) {
    auto it = global_name_to_slot.find(name);
    if (it != global_name_to_slot.end()) {
      return it->second;
    }
    size_t slot = global_name_to_slot.size();
    global_name_to_slot[name] = slot;
    if (slot >= globals_by_id.size()) {
      globals_by_id.resize(slot + 1, TaggedValue::none());
      globals_initialized.resize(slot + 1, false);
    }
    return slot;
  }

  size_t get_global_slot(bytecode::Function *func, size_t name_idx) {
    if (name_idx >= func->names_.size()) {
      throw RuntimeException("Global name index out of range");
    }
    auto &slots = global_slot_cache[func];
    if (slots.size() < func->names_.size()) {
      slots.resize(func->names_.size(), kInvalidSlot);
    }
    size_t slot = slots[name_idx];
    if (slot == kInvalidSlot) {
      slot = ensure_global_slot(func->names_[name_idx]);
      slots[name_idx] = slot;
    }
    return slot;
  }

  size_t ensure_field_slot(const std::string &name) {
    auto it = field_name_to_slot.find(name);
    if (it != field_name_to_slot.end()) {
      return it->second;
    }
    size_t slot = field_name_to_slot.size();
    field_name_to_slot[name] = slot;
    return slot;
  }

  size_t get_field_slot(bytecode::Function *func, size_t name_idx) {
    if (name_idx >= func->names_.size()) {
      throw RuntimeException("Field name index out of range");
    }
    auto &slots = field_slot_cache[func];
    if (slots.size() < func->names_.size()) {
      slots.resize(func->names_.size(), kInvalidSlot);
    }
    size_t slot = slots[name_idx];
    if (slot == kInvalidSlot) {
      slot = ensure_field_slot(func->names_[name_idx]);
      slots[name_idx] = slot;
    }
    return slot;
  }

  void translate_stack_to_reg(bytecode::Function *func) {
    using bytecode::Operation;
    struct RegAlloc {
      uint16_t next;
      uint16_t max_used;
      uint16_t fresh() {
        uint16_t r = next++;
        if (r > max_used)
          max_used = r;
        return r;
      }
    };

    uint16_t initial = static_cast<uint16_t>(func->local_vars_.size());
    RegAlloc alloc{initial, static_cast<uint16_t>(initial ? initial - 1 : 0)};
    std::vector<uint16_t> vstack;
    std::vector<size_t> pc_to_out(func->instructions.size() + 1, 0);
    struct Fixup {
      size_t out_idx;
      size_t target_pc;
    };
    std::vector<Fixup> fixups;
    std::vector<bytecode::RegisterInstruction> out;

    auto ensure_reg_count = [&](uint16_t idx) {
      if (idx > alloc.max_used)
        alloc.max_used = idx;
    };
    auto require_stack = [&](size_t need) {
      if (vstack.size() < need) {
        throw RuntimeException("Translate: stack underflow");
      }
    };
    ensure_reg_count(static_cast<uint16_t>(
        func->local_vars_.size() ? func->local_vars_.size() - 1 : 0));

    for (size_t pc = 0; pc < func->instructions.size(); ++pc) {
      pc_to_out[pc] = out.size();
      const auto &in = func->instructions[pc];
      switch (in.operation) {
      case Operation::LoadConst: {
        uint16_t dst = alloc.fresh();
        out.push_back({Operation::LoadConst, dst, 0, 0, in.operand0.value()});
        vstack.push_back(dst);
        break;
      }
      case Operation::LoadFunc: {
        uint16_t dst = alloc.fresh();
        out.push_back({Operation::LoadFunc, dst, 0, 0, in.operand0.value()});
        vstack.push_back(dst);
        break;
      }
      case Operation::LoadLocal: {
        uint16_t reg = static_cast<uint16_t>(in.operand0.value());
        if (reg >= func->local_vars_.size()) {
          throw RuntimeException(
              "Translate: local variable index out of range");
        }
        ensure_reg_count(reg);
        vstack.push_back(reg);
        break;
      }
      case Operation::StoreLocal: {
        require_stack(1);
        uint16_t val = vstack.back();
        vstack.pop_back();
        uint16_t dst = static_cast<uint16_t>(in.operand0.value());
        if (dst >= func->local_vars_.size()) {
          throw RuntimeException(
              "Translate: local variable index out of range");
        }
        ensure_reg_count(dst);
        out.push_back({Operation::StoreLocal, dst, val, 0, 0});
        break;
      }
      case Operation::Add:
      case Operation::Sub:
      case Operation::Mul:
      case Operation::Div:
      case Operation::Gt:
      case Operation::Geq:
      case Operation::Eq:
      case Operation::And:
      case Operation::Or: {
        require_stack(2);
        uint16_t right = vstack.back();
        vstack.pop_back();
        uint16_t left = vstack.back();
        vstack.pop_back();
        uint16_t dst = alloc.fresh();
        out.push_back({in.operation, dst, left, right, 0});
        vstack.push_back(dst);
        break;
      }
      case Operation::Neg:
      case Operation::Not: {
        require_stack(1);
        uint16_t val = vstack.back();
        vstack.pop_back();
        uint16_t dst = alloc.fresh();
        out.push_back({in.operation, dst, val, 0, 0});
        vstack.push_back(dst);
        break;
      }
      case Operation::Goto: {
        int64_t target_pc = static_cast<int64_t>(pc) + in.operand0.value();
        if (target_pc < 0 ||
            target_pc > static_cast<int64_t>(func->instructions.size())) {
          throw RuntimeException("Translate: branch target out of range");
        }
        out.push_back({Operation::Goto, 0, 0, 0, 0});
        fixups.push_back({out.size() - 1, static_cast<size_t>(target_pc)});
        break;
      }
      case Operation::If: {
        uint16_t cond = vstack.back();
        vstack.pop_back();
        int64_t target_pc = static_cast<int64_t>(pc) + in.operand0.value();
        if (target_pc < 0 ||
            target_pc > static_cast<int64_t>(func->instructions.size())) {
          throw RuntimeException("Translate: branch target out of range");
        }
        out.push_back({Operation::If, 0, cond, 0, 0});
        fixups.push_back({out.size() - 1, static_cast<size_t>(target_pc)});
        break;
      }
      case Operation::Dup: {
        require_stack(1);
        uint16_t v = vstack.back();
        vstack.push_back(v);
        break;
      }
      case Operation::Swap: {
        require_stack(2);
        uint16_t a = vstack.back();
        vstack.pop_back();
        uint16_t b = vstack.back();
        vstack.pop_back();
        vstack.push_back(a);
        vstack.push_back(b);
        break;
      }
      case Operation::Pop: {
        require_stack(1);
        vstack.pop_back();
        break;
      }
      case Operation::Call: {
        int32_t arg_count = in.operand0.value();
        if (arg_count < 0 ||
            vstack.size() < static_cast<size_t>(arg_count + 1)) {
          throw RuntimeException("Translate: stack underflow");
        }
        std::vector<uint16_t> args;
        args.reserve(arg_count);
        for (int i = 0; i < arg_count; ++i) {
          args.push_back(vstack.back());
          vstack.pop_back();
        }
        std::reverse(args.begin(), args.end());
        uint16_t callee = vstack.back();
        vstack.pop_back();
        uint16_t arg_start = alloc.fresh();
        uint16_t first_arg = arg_start;
        // ensure contiguous
        if (arg_count > 0) {
          ensure_reg_count(static_cast<uint16_t>(arg_start + arg_count - 1));
          for (int i = 0; i < arg_count; ++i) {
            out.push_back({Operation::StoreLocal,
                           static_cast<uint16_t>(arg_start + i), args[i], 0,
                           0});
          }
        }
        uint16_t dst = alloc.fresh();
        out.push_back({Operation::Call, dst, callee, first_arg, arg_count});
        vstack.push_back(dst);
        break;
      }
      case Operation::Return: {
        require_stack(1);
        uint16_t ret = vstack.back();
        vstack.pop_back();
        out.push_back({Operation::Return, 0, ret, 0, 0});
        break;
      }
      case Operation::LoadGlobal: {
        uint16_t dst = alloc.fresh();
        out.push_back({Operation::LoadGlobal, dst, 0, 0, in.operand0.value()});
        vstack.push_back(dst);
        break;
      }
      case Operation::StoreGlobal: {
        require_stack(1);
        uint16_t val = vstack.back();
        vstack.pop_back();
        out.push_back({Operation::StoreGlobal, 0, val, 0, in.operand0.value()});
        break;
      }
      case Operation::PushReference: {
        uint16_t dst = alloc.fresh();
        out.push_back(
            {Operation::PushReference, dst, 0, 0, in.operand0.value()});
        vstack.push_back(dst);
        break;
      }
      case Operation::LoadReference: {
        require_stack(1);
        uint16_t ref = vstack.back();
        vstack.pop_back();
        uint16_t dst = alloc.fresh();
        out.push_back({Operation::LoadReference, dst, ref, 0, 0});
        vstack.push_back(dst);
        break;
      }
      case Operation::StoreReference: {
        require_stack(2);
        uint16_t val = vstack.back();
        vstack.pop_back();
        uint16_t ref = vstack.back();
        vstack.pop_back();
        out.push_back({Operation::StoreReference, 0, val, ref, 0});
        break;
      }
      case Operation::AllocRecord: {
        uint16_t dst = alloc.fresh();
        out.push_back({Operation::AllocRecord, dst, 0, 0, 0});
        vstack.push_back(dst);
        break;
      }
      case Operation::FieldLoad: {
        require_stack(1);
        uint16_t rec = vstack.back();
        vstack.pop_back();
        uint16_t dst = alloc.fresh();
        out.push_back({Operation::FieldLoad, dst, rec, 0, in.operand0.value()});
        vstack.push_back(dst);
        break;
      }
      case Operation::FieldStore: {
        require_stack(2);
        uint16_t val = vstack.back();
        vstack.pop_back();
        uint16_t rec = vstack.back();
        vstack.pop_back();
        out.push_back(
            {Operation::FieldStore, 0, val, rec, in.operand0.value()});
        break;
      }
      case Operation::IndexLoad: {
        require_stack(2);
        uint16_t idx = vstack.back();
        vstack.pop_back();
        uint16_t rec = vstack.back();
        vstack.pop_back();
        uint16_t dst = alloc.fresh();
        out.push_back({Operation::IndexLoad, dst, rec, idx, 0});
        vstack.push_back(dst);
        break;
      }
      case Operation::IndexStore: {
        require_stack(3);
        uint16_t val = vstack.back();
        vstack.pop_back();
        uint16_t idx = vstack.back();
        vstack.pop_back();
        uint16_t rec = vstack.back();
        vstack.pop_back();
        out.push_back({Operation::IndexStore, rec, val, idx, 0});
        break;
      }
      case Operation::AllocClosure: {
        int32_t free_count = in.operand0.value();
        if (free_count < 0 ||
            vstack.size() < static_cast<size_t>(free_count + 1)) {
          throw RuntimeException("Translate: stack underflow");
        }
        std::vector<uint16_t> refs;
        refs.reserve(free_count);
        for (int i = 0; i < free_count; ++i) {
          refs.push_back(vstack.back());
          vstack.pop_back();
        }
        std::reverse(refs.begin(), refs.end());
        uint16_t func_reg = vstack.back();
        vstack.pop_back();

        uint16_t base = alloc.fresh();
        if (free_count > 0) {
          ensure_reg_count(static_cast<uint16_t>(base + free_count - 1));
          for (int i = 0; i < free_count; ++i) {
            out.push_back({Operation::StoreLocal,
                           static_cast<uint16_t>(base + i), refs[i], 0, 0});
          }
        }
        uint16_t dst = alloc.fresh();
        out.push_back(
            {Operation::AllocClosure, dst, base, func_reg, free_count});
        vstack.push_back(dst);
        break;
      }
      default:
        // Unhandled opcodes can be added as needed.
        break;
      }
    }
    pc_to_out[func->instructions.size()] = out.size();

    for (const auto &fx : fixups) {
      if (fx.target_pc >= pc_to_out.size()) {
        throw RuntimeException("Translate: branch target out of range");
      }
      int32_t rel = static_cast<int32_t>(pc_to_out[fx.target_pc] - fx.out_idx);
      out[fx.out_idx].imm = rel;
    }

    func->register_count = alloc.max_used + 1;
    func->reg_instructions = std::move(out);
  }

  void translate_function_tree(bytecode::Function *func) {
    if (!func)
      return;
    if (func->reg_instructions.empty()) {
      translate_stack_to_reg(func);
    }
    for (auto *child : func->functions_) {
      translate_function_tree(child);
    }
  }

  // Recursively translate a function and all nested functions to the
  // register-based representation. Skip functions that have already been
  // translated so repeated calls are cheap.
  void ensure_reg_translation(bytecode::Function *func) {
    if (!func || !func->reg_instructions.empty())
      return;

    for (auto *child : func->functions_) {
      ensure_reg_translation(child);
    }

    translate_stack_to_reg(func);
  }

  static TaggedValue
  reg_entry_trampoline(VM *vm, bytecode::Function *func,
                       const std::vector<TaggedValue> *args,
                       const std::vector<Value *> *free_refs) {
    return vm->execute_function_reg(func, *args, *free_refs);
  }

  JitBlock &compile_to_native(bytecode::Function *func) {
    auto it = jitted_functions.find(func);
    if (it != jitted_functions.end()) {
      return it->second;
    }

    ensure_reg_translation(func);

    // Simple stub that tail-jumps into the register-based interpreter entry
    // point. Arguments are forwarded using the standard System V calling
    // convention used by the helper signature.
    constexpr size_t kCodeSize = 12; // movabs rax, imm64; jmp rax

    size_t page_size = static_cast<size_t>(getpagesize());
#ifdef MAP_ANON
    const int map_anon_flag = MAP_ANON;
#else
    const int map_anon_flag = MAP_ANONYMOUS;
#endif
    void *mem = mmap(nullptr, page_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | map_anon_flag, -1, 0);
    if (mem == MAP_FAILED) {
      throw RuntimeException("Failed to allocate executable memory for JIT");
    }

    uint8_t *code = static_cast<uint8_t *>(mem);
    code[0] = 0x48; // REX.W prefix for 64-bit operand size
    code[1] = 0xB8; // MOV rax, imm64
    *reinterpret_cast<uint64_t *>(code + 2) =
        reinterpret_cast<uint64_t>(&VM::reg_entry_trampoline);
    code[10] = 0xFF; // JMP rax
    code[11] = 0xE0;

    JitBlock block;
    block.entry = reinterpret_cast<JitBlock::EntryPoint>(code);
    block.code_size = kCodeSize;
    block.memory = mem;

    auto [insert_it, _] = jitted_functions.emplace(func, block);
    return insert_it->second;
  }

  void ensure_jit_translation(bytecode::Function *func) {
    if (!func)
      return;
    compile_to_native(func);
    for (auto *child : func->functions_) {
      ensure_jit_translation(child);
    }
  }

  TaggedValue execute_function_reg(bytecode::Function *func,
                                   const std::vector<TaggedValue> &args,
                                   const std::vector<Value *> &free_refs) {
    auto it = native_functions.find(func);
    if (it != native_functions.end()) {
      return tagged_from_value(call_native(it->second, args));
    }

    if (args.size() != func->parameter_count_) {
      throw RuntimeException("Argument count mismatch");
    }

    Frame frame(func->register_count, func, &free_refs);
    call_stack.push_back(&frame);

    for (size_t i = 0; i < args.size(); ++i) {
      frame.locals[i] = args[i];
    }

    // References for local_reference_vars: assume register index matches
    // local_vars_ order
    for (const auto &var_name : func->local_reference_vars_) {
      auto it_ref = std::find(func->local_vars_.begin(),
                              func->local_vars_.end(), var_name);
      if (it_ref != func->local_vars_.end()) {
        size_t var_idx = std::distance(func->local_vars_.begin(), it_ref);
        TaggedValue initial_val = frame.locals[var_idx];
        Value *boxed_initial = box_tagged(initial_val);
        auto ref = allocate<Reference>(boxed_initial);
        frame.local_refs[var_name] = ref;
        frame.ref_locals.insert(var_idx);
        frame.locals[var_idx] = TaggedValue::from_heap(ref->cell);
      }
    }

    const auto &instructions = func->reg_instructions;
    if (instructions.empty()) {
      call_stack.pop_back();
      bool is_main = call_stack.empty();
      if (!is_main && !func->instructions.empty()) {
        throw RuntimeException("Function must end with a return statement");
      }
      return TaggedValue::none();
    }

    const bytecode::RegisterInstruction *ip = instructions.data();
    const bytecode::RegisterInstruction *end = ip + instructions.size();
    std::vector<TaggedValue> temp_args_local;
    std::vector<Value *> temp_refs_local;

    static void *dispatch_table[] = {
        &&op_LoadConstR,      // LoadConst
        &&op_LoadFuncR,       // LoadFunc
        &&op_LoadLocalR,      // LoadLocal (used as move)
        &&op_StoreLocalR,     // StoreLocal (used as move)
        &&op_LoadGlobalR,     // LoadGlobal
        &&op_StoreGlobalR,    // StoreGlobal
        &&op_PushReferenceR,  // PushReference
        &&op_LoadReferenceR,  // LoadReference
        &&op_StoreReferenceR, // StoreReference
        &&op_AllocRecordR,    // AllocRecord
        &&op_FieldLoadR,      // FieldLoad
        &&op_FieldStoreR,     // FieldStore
        &&op_IndexLoadR,      // IndexLoad
        &&op_IndexStoreR,     // IndexStore
        &&op_AllocClosureR,   // AllocClosure
        &&op_CallR,           // Call
        &&op_ReturnR,         // Return
        &&op_AddR,            // Add
        &&op_SubR,            // Sub
        &&op_MulR,            // Mul
        &&op_DivR,            // Div
        &&op_NegR,            // Neg
        &&op_GtR,             // Gt
        &&op_GeqR,            // Geq
        &&op_EqR,             // Eq
        &&op_AndR,            // And
        &&op_OrR,             // Or
        &&op_NotR,            // Not
        &&op_GotoR,           // Goto
        &&op_IfR,             // If
        &&op_DupR,            // Dup (unused)
        &&op_SwapR,           // Swap (unused)
        &&op_PopR             // Pop (unused)
    };

#define DISPATCH_REG() goto *dispatch_table[static_cast<int>(ip->op)]

    TaggedValue ret_val = TaggedValue::none();
    bool returned = false;
    DISPATCH_REG();

  op_LoadConstR: {
    uint16_t dst = ip->dst;
    int32_t cidx = ip->imm;
    if (cidx < 0 || static_cast<size_t>(cidx) >= func->constants_.size()) {
      throw RuntimeException("LoadConst: constant index out of range");
    }
    frame.locals[dst] = constant_to_tagged(func->constants_[cidx]);
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_LoadFuncR: {
    uint16_t dst = ip->dst;
    int32_t findex = ip->imm;
    if (findex < 0 || static_cast<size_t>(findex) >= func->functions_.size()) {
      throw RuntimeException("LoadFunc: function index out of range");
    }
    auto f = func->functions_[findex];
    frame.locals[dst] = TaggedValue::from_heap(allocate<Function>(f));
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_LoadLocalR: {
    if (ip->src1 >= frame.locals.size()) {
      throw RuntimeException("LoadLocal: local variable index out of range");
    }
    TaggedValue v = frame.locals[ip->src1];
    frame.locals[ip->dst] = v;
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_StoreLocalR: {
    if (ip->src1 >= frame.locals.size()) {
      throw RuntimeException("StoreLocal: local variable index out of range");
    }
    TaggedValue val = frame.locals[ip->src1];
    uint16_t dst = ip->dst;
    if (frame.ref_locals.find(dst) != frame.ref_locals.end()) {
      if (dst >= func->local_vars_.size()) {
        throw RuntimeException(
            "StoreLocal: local variable name index out of range");
      }
      const std::string &var_name = func->local_vars_[dst];
      auto ref_it = frame.local_refs.find(var_name);
      if (ref_it != frame.local_refs.end()) {
        auto ref = static_cast<Reference *>(ref_it->second);
        ref->cell = box_tagged(val);
        heap.write_barrier(ref, ref->cell);
      }
    }
    frame.locals[dst] = val;
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_LoadGlobalR: {
    size_t idx = static_cast<size_t>(ip->imm);
    if (idx >= func->names_.size()) {
      throw RuntimeException("LoadGlobal: name index out of range");
    }
    size_t slot = get_global_slot(func, idx);
    if (slot >= globals_initialized.size() || !globals_initialized[slot]) {
      throw UninitializedVariableException("Undefined global at slot " +
                                           std::to_string(slot));
    }
    frame.locals[ip->dst] = globals_by_id[slot];
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_StoreGlobalR: {
    size_t idx = static_cast<size_t>(ip->imm);
    if (idx >= func->names_.size()) {
      throw RuntimeException("StoreGlobal: name index out of range");
    }
    size_t slot = get_global_slot(func, idx);
    globals_by_id[slot] = frame.locals[ip->src1];
    if (slot >= globals_initialized.size()) {
      globals_initialized.resize(slot + 1, false);
    }
    globals_initialized[slot] = true;
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_PushReferenceR: {
    int32_t idx = ip->imm;
    Value *ref = nullptr;
    if (idx < static_cast<int32_t>(func->local_reference_vars_.size())) {
      const std::string &var_name = func->local_reference_vars_[idx];
      ref = frame.local_refs[var_name];
    } else {
      int32_t free_idx = idx - func->local_reference_vars_.size();
      if (free_idx < 0 || free_idx >= static_cast<int32_t>(free_refs.size())) {
        throw RuntimeException(
            "PushReference: free variable index out of range");
      }
      ref = free_refs[free_idx];
    }
    frame.locals[ip->dst] = TaggedValue::from_heap(ref);
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_LoadReferenceR: {
    TaggedValue ref_tv = frame.locals[ip->src1];
    if (ref_tv.kind != TaggedValue::Kind::HeapPtr ||
        ref_tv.ptr->tag != Value::Type::Reference)
      throw IllegalCastException("Expected reference");
    auto ref = static_cast<Reference *>(ref_tv.ptr);
    frame.locals[ip->dst] = tagged_from_value(ref->cell);
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_StoreReferenceR: {
    TaggedValue val_tv = frame.locals[ip->src1];
    TaggedValue ref_tv = frame.locals[ip->src2];
    if (ref_tv.kind != TaggedValue::Kind::HeapPtr ||
        ref_tv.ptr->tag != Value::Type::Reference)
      throw IllegalCastException("Expected reference");
    auto ref = static_cast<Reference *>(ref_tv.ptr);
    ref->cell = box_tagged(val_tv);
    heap.write_barrier(ref, ref->cell);
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_AllocRecordR: {
    frame.locals[ip->dst] = TaggedValue::from_heap(allocate<Record>());
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_FieldLoadR: {
    TaggedValue rec_tv = frame.locals[ip->src1];
    if (rec_tv.kind != TaggedValue::Kind::HeapPtr ||
        rec_tv.ptr->tag != Value::Type::Record)
      throw IllegalCastException("Expected record");
    auto rec = static_cast<Record *>(rec_tv.ptr);
    size_t idx = static_cast<size_t>(ip->imm);
    if (idx >= func->names_.size()) {
      throw RuntimeException("FieldLoad: name index out of range");
    }
    size_t slot = get_field_slot(func, idx);
    if (slot >= rec->named_fields.size()) {
      rec->named_fields.resize(slot + 1, nullptr);
    }
    Value *field_val = rec->named_fields[slot];
    frame.locals[ip->dst] =
        field_val ? tagged_from_value(field_val) : TaggedValue::none();
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_FieldStoreR: {
    TaggedValue val_tv = frame.locals[ip->src1];
    TaggedValue rec_tv = frame.locals[ip->src2];
    if (rec_tv.kind != TaggedValue::Kind::HeapPtr ||
        rec_tv.ptr->tag != Value::Type::Record)
      throw IllegalCastException("Expected record");
    auto rec = static_cast<Record *>(rec_tv.ptr);
    size_t idx = static_cast<size_t>(ip->imm);
    if (idx >= func->names_.size()) {
      throw RuntimeException("FieldStore: name index out of range");
    }
    Value *boxed = box_tagged(val_tv);
    size_t slot = get_field_slot(func, idx);
    if (slot >= rec->named_fields.size()) {
      rec->named_fields.resize(slot + 1, nullptr);
    }
    rec->named_fields[slot] = boxed;
    rec->fields[func->names_[idx]] = boxed;
    heap.write_barrier(rec, boxed);
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_IndexLoadR: {
    TaggedValue rec_tv = frame.locals[ip->src1];
    TaggedValue idx_tv = frame.locals[ip->src2];
    if (rec_tv.kind != TaggedValue::Kind::HeapPtr ||
        rec_tv.ptr->tag != Value::Type::Record)
      throw IllegalCastException("Expected record");
    auto rec = static_cast<Record *>(rec_tv.ptr);
    TaggedValue result = TaggedValue::none();
    if (!record_try_dense_load(rec, idx_tv, result)) {
      result = record_map_load(rec, idx_tv);
    }
    frame.locals[ip->dst] = result;
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_IndexStoreR: {
    TaggedValue val_tv = frame.locals[ip->src1];
    TaggedValue idx_tv = frame.locals[ip->src2];
    TaggedValue rec_tv = frame.locals[ip->dst];
    if (rec_tv.kind != TaggedValue::Kind::HeapPtr ||
        rec_tv.ptr->tag != Value::Type::Record)
      throw IllegalCastException("Expected record");
    auto rec = static_cast<Record *>(rec_tv.ptr);
    if (!record_try_dense_store(rec, idx_tv, val_tv)) {
      record_map_store(rec, idx_tv, val_tv);
    }
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_AllocClosureR: {
    int32_t free_count = ip->imm;
    temp_refs_local.clear();
    temp_refs_local.reserve(free_count);
    for (int i = 0; i < free_count; ++i) {
      TaggedValue tv = frame.locals[ip->src1 + i];
      if (tv.kind != TaggedValue::Kind::HeapPtr ||
          tv.ptr->tag != Value::Type::Reference)
        throw IllegalCastException("Expected reference");
      temp_refs_local.push_back(tv.ptr);
    }
    TaggedValue func_tv = frame.locals[ip->src2];
    if (func_tv.kind != TaggedValue::Kind::HeapPtr ||
        func_tv.ptr->tag != Value::Type::Function)
      throw IllegalCastException("Expected function");
    auto f = static_cast<Function *>(func_tv.ptr);
    Value *closure_val = allocate<Closure>(f->func, temp_refs_local);
    for (Value *ref : temp_refs_local) {
      heap.write_barrier(closure_val, ref);
    }
    frame.locals[ip->dst] = TaggedValue::from_heap(closure_val);
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_AddR: {
    TaggedValue left = frame.locals[ip->src1];
    TaggedValue right = frame.locals[ip->src2];

    auto is_string = [](const TaggedValue &tv) {
      return tv.kind == TaggedValue::Kind::HeapPtr &&
             tv.ptr->tag == Value::Type::String;
    };

    if ((left.kind == TaggedValue::Kind::Integer ||
         (left.kind == TaggedValue::Kind::HeapPtr &&
          left.ptr->tag == Value::Type::Integer)) &&
        (right.kind == TaggedValue::Kind::Integer ||
         (right.kind == TaggedValue::Kind::HeapPtr &&
          right.ptr->tag == Value::Type::Integer))) {
      auto li = get_int(left);
      auto ri = get_int(right);
      frame.locals[ip->dst] = TaggedValue::from_int(li + ri);
    } else if (is_string(left) || is_string(right)) {
      frame.locals[ip->dst] = TaggedValue::from_heap(
          allocate<String>(tagged_to_string(left) + tagged_to_string(right)));
    } else {
      throw IllegalCastException("Invalid operand types for add");
    }

    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_SubR: {
    TaggedValue left = frame.locals[ip->src1];
    TaggedValue right = frame.locals[ip->src2];
    if ((left.kind == TaggedValue::Kind::Integer ||
         (left.kind == TaggedValue::Kind::HeapPtr &&
          left.ptr->tag == Value::Type::Integer)) &&
        (right.kind == TaggedValue::Kind::Integer ||
         (right.kind == TaggedValue::Kind::HeapPtr &&
          right.ptr->tag == Value::Type::Integer))) {
      auto li = get_int(left);
      auto ri = get_int(right);
      frame.locals[ip->dst] = TaggedValue::from_int(li - ri);
    } else {
      throw IllegalCastException("Invalid operand types for subtract");
    }
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_MulR: {
    TaggedValue left = frame.locals[ip->src1];
    TaggedValue right = frame.locals[ip->src2];
    if ((left.kind == TaggedValue::Kind::Integer ||
         (left.kind == TaggedValue::Kind::HeapPtr &&
          left.ptr->tag == Value::Type::Integer)) &&
        (right.kind == TaggedValue::Kind::Integer ||
         (right.kind == TaggedValue::Kind::HeapPtr &&
          right.ptr->tag == Value::Type::Integer))) {
      auto li = get_int(left);
      auto ri = get_int(right);
      frame.locals[ip->dst] = TaggedValue::from_int(li * ri);
    } else {
      throw IllegalCastException("Invalid operand types for multiply");
    }
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_DivR: {
    TaggedValue left = frame.locals[ip->src1];
    TaggedValue right = frame.locals[ip->src2];
    if ((left.kind == TaggedValue::Kind::Integer ||
         (left.kind == TaggedValue::Kind::HeapPtr &&
          left.ptr->tag == Value::Type::Integer)) &&
        (right.kind == TaggedValue::Kind::Integer ||
         (right.kind == TaggedValue::Kind::HeapPtr &&
          right.ptr->tag == Value::Type::Integer))) {
      auto li = get_int(left);
      auto ri = get_int(right);
      if (ri == 0)
        throw IllegalArithmeticException("Division by zero");
      frame.locals[ip->dst] = TaggedValue::from_int(li / ri);
    } else {
      throw IllegalCastException("Invalid operand types for divide");
    }
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_NegR: {
    TaggedValue left = frame.locals[ip->src1];
    if (left.kind == TaggedValue::Kind::Integer ||
        (left.kind == TaggedValue::Kind::HeapPtr &&
         left.ptr->tag == Value::Type::Integer)) {
      auto li = get_int(left);
      frame.locals[ip->dst] = TaggedValue::from_int(-li);
    } else {
      throw IllegalCastException("Invalid operand types for negate");
    }
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_GtR: {
    TaggedValue left = frame.locals[ip->src1];
    TaggedValue right = frame.locals[ip->src2];
    if ((left.kind == TaggedValue::Kind::Integer ||
         (left.kind == TaggedValue::Kind::HeapPtr &&
          left.ptr->tag == Value::Type::Integer)) &&
        (right.kind == TaggedValue::Kind::Integer ||
         (right.kind == TaggedValue::Kind::HeapPtr &&
          right.ptr->tag == Value::Type::Integer))) {
      frame.locals[ip->dst] =
          TaggedValue::from_bool(get_int(left) > get_int(right));
    } else {
      throw IllegalCastException("Invalid operand types for greater than");
    }
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_GeqR: {
    TaggedValue left = frame.locals[ip->src1];
    TaggedValue right = frame.locals[ip->src2];
    if ((left.kind == TaggedValue::Kind::Integer ||
         (left.kind == TaggedValue::Kind::HeapPtr &&
          left.ptr->tag == Value::Type::Integer)) &&
        (right.kind == TaggedValue::Kind::Integer ||
         (right.kind == TaggedValue::Kind::HeapPtr &&
          right.ptr->tag == Value::Type::Integer))) {
      frame.locals[ip->dst] =
          TaggedValue::from_bool(get_int(left) >= get_int(right));
    } else {
      throw IllegalCastException("Invalid operand types for greater or equal");
    }
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_EqR: {
    TaggedValue left = frame.locals[ip->src1];
    TaggedValue right = frame.locals[ip->src2];
    frame.locals[ip->dst] = TaggedValue::from_bool(values_equal(left, right));
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_AndR: {
    TaggedValue left = frame.locals[ip->src1];
    TaggedValue right = frame.locals[ip->src2];
    if ((left.kind == TaggedValue::Kind::Boolean ||
         (left.kind == TaggedValue::Kind::HeapPtr &&
          left.ptr->tag == Value::Type::Boolean)) &&
        (right.kind == TaggedValue::Kind::Boolean ||
         (right.kind == TaggedValue::Kind::HeapPtr &&
          right.ptr->tag == Value::Type::Boolean))) {
      frame.locals[ip->dst] =
          TaggedValue::from_bool(get_bool(left) && get_bool(right));
    } else {
      throw IllegalCastException("Invalid operand types for and");
    }
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_OrR: {
    TaggedValue left = frame.locals[ip->src1];
    TaggedValue right = frame.locals[ip->src2];
    if ((left.kind == TaggedValue::Kind::Boolean ||
         (left.kind == TaggedValue::Kind::HeapPtr &&
          left.ptr->tag == Value::Type::Boolean)) &&
        (right.kind == TaggedValue::Kind::Boolean ||
         (right.kind == TaggedValue::Kind::HeapPtr &&
          right.ptr->tag == Value::Type::Boolean))) {
      frame.locals[ip->dst] =
          TaggedValue::from_bool(get_bool(left) || get_bool(right));
    } else {
      throw IllegalCastException("Invalid operand types for or");
    }
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_NotR: {
    TaggedValue val = frame.locals[ip->src1];
    if (val.kind == TaggedValue::Kind::Boolean ||
        (val.kind == TaggedValue::Kind::HeapPtr &&
         val.ptr->tag == Value::Type::Boolean)) {
      frame.locals[ip->dst] = TaggedValue::from_bool(!get_bool(val));
    } else {
      throw IllegalCastException("Invalid operand types for not");
    }
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_GotoR: {
    ip += ip->imm;
    if (ip < instructions.data() || ip >= end) {
      throw RuntimeException("Goto: target out of range");
    }
    DISPATCH_REG();
  }

  op_IfR: {
    TaggedValue cond = frame.locals[ip->src1];
    if (cond.kind != TaggedValue::Kind::Boolean &&
        !(cond.kind == TaggedValue::Kind::HeapPtr &&
          cond.ptr->tag == Value::Type::Boolean))
      throw IllegalCastException("Invalid operand types for if");
    if (get_bool(cond)) {
      ip += ip->imm;
    } else {
      ++ip;
    }
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_CallR: {
    uint16_t dst = ip->dst;
    uint16_t callee_reg = ip->src1;
    uint16_t arg_start = ip->src2;
    int32_t arg_count = ip->imm;

    TaggedValue callee_tv = frame.locals[callee_reg];
    if (callee_tv.kind != TaggedValue::Kind::HeapPtr)
      throw IllegalCastException("Expected callable");

    Value *callee = callee_tv.ptr;
    temp_args_local.clear();
    temp_args_local.reserve(arg_count);
    for (int i = 0; i < arg_count; ++i) {
      temp_args_local.push_back(frame.locals[arg_start + i]);
    }

    TaggedValue result;
    if (callee->tag == Value::Type::Closure) {
      auto closure = static_cast<Closure *>(callee);
      result = execute_function(closure->function, temp_args_local,
                                closure->free_var_refs);
    } else if (callee->tag == Value::Type::Function) {
      auto func_ptr_local = static_cast<Function *>(callee);
      result = execute_function(func_ptr_local->func, temp_args_local, {});
    } else {
      throw IllegalCastException("Expected closure or function");
    }
    frame.locals[dst] = result;

    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_DupR: {
    frame.locals[ip->dst] = frame.locals[ip->src1];
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_SwapR: {
    std::swap(frame.locals[ip->dst], frame.locals[ip->src1]);
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_PopR: {
    frame.locals[ip->dst] = TaggedValue::none();
    ++ip;
    if (ip == end)
      goto function_epilogue_reg;
    DISPATCH_REG();
  }

  op_ReturnR: {
    ret_val = frame.locals[ip->src1];
    returned = true;
    goto function_epilogue_reg;
  }

  function_epilogue_reg:
    call_stack.pop_back();
    bool is_main = call_stack.empty();
    if (!returned && !func->instructions.empty()) {
      if (is_main) {
        return TaggedValue::none();
      }
      throw RuntimeException("Function must end with a return statement");
    }
    return ret_val;

#undef DISPATCH_REG
  }

  // Convert bytecode constant to runtime value
  TaggedValue constant_to_tagged(bytecode::Constant *c) {
    auto it = constant_cache.find(c);
    if (it != constant_cache.end()) {
      return TaggedValue::from_heap(it->second);
    }

    if (dynamic_cast<bytecode::Constant::None *>(c)) {
      return TaggedValue::none();
    } else if (auto ic = dynamic_cast<bytecode::Constant::Integer *>(c)) {
      return TaggedValue::from_int(static_cast<int32_t>(ic->value));
    } else if (auto sc = dynamic_cast<bytecode::Constant::String *>(c)) {
      Value *v = allocate<String>(sc->value);
      constant_cache[c] = v;
      return TaggedValue::from_heap(v);
    } else if (auto bc = dynamic_cast<bytecode::Constant::Boolean *>(c)) {
      return TaggedValue::from_bool(bc->value);
    }
    throw IllegalCastException("Unknown constant type");
  }

  Value *call_native(int func_id, const std::vector<TaggedValue> &args) {
    if (func_id == 0) { // print
      if (args.size() != 1)
        throw RuntimeException("print expects 1 argument");
      print_value(args[0]);
      std::cout << std::endl;
      return none_singleton;
    } else if (func_id == 1) { // input
      std::string line;
      std::getline(std::cin, line);
      return allocate<String>(line);
    } else if (func_id == 2) { // intcast
      if (args.size() != 1)
        throw RuntimeException("intcast expects 1 argument");
      const TaggedValue &arg = args[0];
      if (arg.kind == TaggedValue::Kind::HeapPtr &&
          arg.ptr->tag == Value::Type::String) {
        auto s = static_cast<String *>(arg.ptr);
        try {
          int32_t val = std::atoi(s->value.c_str());
          return allocate<Integer>(val);
        } catch (...) {
          throw IllegalCastException("Cannot cast string to int");
        }
      }
      throw IllegalCastException("Cannot cast to int");
    }
    throw UninitializedVariableException("Unknown native function");
  }

  void print_value(Value *v) { std::cout << v->toString(); }
  void print_value(const TaggedValue &tv) { std::cout << tagged_to_string(tv); }

  void maybe_gc() {
    // Collect root set: globals + all stack frames' locals + all operand stacks
    std::vector<Collectable *> roots;

    // Add globals
    for (size_t i = 0; i < globals_by_id.size(); ++i) {
      if (i >= globals_initialized.size() || !globals_initialized[i])
        continue;
      const TaggedValue &val = globals_by_id[i];
      if (val.kind == TaggedValue::Kind::HeapPtr && val.ptr)
        roots.push_back(val.ptr);
    }

    // Cached constants
    for (auto &[c, val] : constant_cache) {
      if (val)
        roots.push_back(val);
    }

    // Add singletons as roots
    if (none_singleton)
      roots.push_back(none_singleton);
    if (bool_true_singleton)
      roots.push_back(bool_true_singleton);
    if (bool_false_singleton)
      roots.push_back(bool_false_singleton);

    // Add all locals from all frames in call stack
    for (Frame *frame : call_stack) {
      for (const TaggedValue &local : frame->locals) {
        if (local.kind == TaggedValue::Kind::HeapPtr && local.ptr)
          roots.push_back(local.ptr);
      }

      // Add operand stack contents
      for (size_t i = 0; i < frame->sp; ++i) {
        if (frame->stack[i].kind == TaggedValue::Kind::HeapPtr &&
            frame->stack[i].ptr)
          roots.push_back(frame->stack[i].ptr);
      }

      // Add reference objects
      for (auto &[name, ref] : frame->local_refs) {
        if (ref)
          roots.push_back(ref);
      }
    }

    // Run GC
    heap.gc(roots.begin(), roots.end());
  }

  // helper function for optimization execute_function
  bool stack_empty(const Frame &frame) const { return frame.sp == 0; }

  size_t stack_size(const Frame &frame) const { return frame.sp; }

  TaggedValue stack_peek(Frame &frame) {
    if (frame.sp == 0)
      throw InsufficientStackException("Cannot peek empty stack");
    return frame.stack[frame.sp - 1];
  }

  TaggedValue execute_function(bytecode::Function *func,
                               const std::vector<TaggedValue> &args,
                               const std::vector<Value *> &free_refs) {
    if (jit_enabled) {
      JitBlock &jit_block = compile_to_native(func);
      if (jit_block.entry) {
        return jit_block.entry(this, func, &args, &free_refs);
      }
    }
    // Handle native functions - if this function is a native function, call
    // it
    auto it = native_functions.find(func);
    if (it != native_functions.end()) {
      return tagged_from_value(call_native(it->second, args));
    }

    if (args.size() != func->parameter_count_) {
      throw RuntimeException("Argument count mismatch");
    }

    Frame frame(func->local_vars_.size(), func, &free_refs);
    call_stack.push_back(&frame);

    // Initialize parameters
    for (size_t i = 0; i < args.size(); ++i) {
      frame.locals[i] = args[i];
    }

    // Create references for local_reference_vars
    for (const auto &var_name : func->local_reference_vars_) {
      auto it_ref = std::find(func->local_vars_.begin(),
                              func->local_vars_.end(), var_name);
      if (it_ref != func->local_vars_.end()) {
        size_t var_idx = std::distance(func->local_vars_.begin(), it_ref);

        // Get initial value (parameter value, or None if not a parameter)
        TaggedValue initial_val = frame.locals[var_idx];
        Value *boxed_initial = box_tagged(initial_val);

        // Create a Reference pointing to this value
        Value *ref = allocate<Reference>(boxed_initial);

        // Store reference for PushReference
        frame.local_refs[var_name] = ref;

        // Mark this local as a reference variable
        frame.ref_locals.insert(var_idx);

        // Store the value in the local slot (same as ref->cell)
        frame.locals[var_idx] = initial_val;
      }
    }

    using bytecode::Operation;

    auto *func_ptr = func;
    const std::vector<Value *> &free_refs_local = free_refs;
    auto &instructions = func_ptr->instructions;

    if (instructions.empty()) {
      call_stack.pop_back();
      bool is_main = call_stack.empty();
      if (!is_main) {
        throw RuntimeException("Function must end with a return statement");
      }
      return TaggedValue::none();
    }

    // Temporary reusable buffers for ops that gather vectors
    std::vector<Value *> temp_refs;
    std::vector<TaggedValue> temp_args;
    bool returned_flag = false;

    const bytecode::Instruction *ip = instructions.data();
    const bytecode::Instruction *end = ip + instructions.size();

    // GCC/Clang extension: labels-as-values for computed goto dispatch
    static void *dispatch_table[] = {&&op_LoadConst,
                                     &&op_LoadFunc,
                                     &&op_LoadLocal,
                                     &&op_StoreLocal,
                                     &&op_LoadGlobal,
                                     &&op_StoreGlobal,
                                     &&op_PushReference,
                                     &&op_LoadReference,
                                     &&op_StoreReference,
                                     &&op_AllocRecord,
                                     &&op_FieldLoad,
                                     &&op_FieldStore,
                                     &&op_IndexLoad,
                                     &&op_IndexStore,
                                     &&op_AllocClosure,
                                     &&op_Call,
                                     &&op_Return,
                                     &&op_Add,
                                     &&op_Sub,
                                     &&op_Mul,
                                     &&op_Div,
                                     &&op_Neg,
                                     &&op_Gt,
                                     &&op_Geq,
                                     &&op_Eq,
                                     &&op_And,
                                     &&op_Or,
                                     &&op_Not,
                                     &&op_Goto,
                                     &&op_If,
                                     &&op_Dup,
                                     &&op_Swap,
                                     &&op_Pop};

#define DISPATCH() goto *dispatch_table[static_cast<int>(ip->operation)]

    DISPATCH();

  op_LoadConst: {
    size_t idx = ip->operand0.value();
    if (idx >= func_ptr->constants_.size()) {
      throw RuntimeException("LoadConst: constant index out of range");
    }
    TaggedValue v = constant_to_tagged(func_ptr->constants_[idx]);
    push(frame, v);

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_LoadFunc: {
    size_t idx = ip->operand0.value();
    if (idx >= func_ptr->functions_.size()) {
      throw RuntimeException("LoadFunc: function index out of range");
    }
    auto f = func_ptr->functions_[idx];
    push(frame, TaggedValue::from_heap(allocate<Function>(f)));

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_LoadLocal: {
    size_t idx = ip->operand0.value();
    if (idx >= frame.locals.size()) {
      throw RuntimeException("LoadLocal: local variable index out of range");
    }
    TaggedValue local = frame.locals[idx];
    push(frame, local);

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_StoreLocal: {
    size_t idx = ip->operand0.value();
    if (idx >= frame.locals.size()) {
      throw RuntimeException("StoreLocal: local variable index out of range");
    }
    TaggedValue val = pop(frame);
    if (frame.ref_locals.find(idx) != frame.ref_locals.end()) {
      if (idx >= func_ptr->local_vars_.size()) {
        throw RuntimeException(
            "StoreLocal: local variable name index out of range");
      }
      const std::string &var_name = func_ptr->local_vars_[idx];
      auto ref_it = frame.local_refs.find(var_name);
      if (ref_it != frame.local_refs.end()) {
        auto ref = static_cast<Reference *>(ref_it->second);
        ref->cell = box_tagged(val);
        heap.write_barrier(ref, ref->cell);
      }
      frame.locals[idx] = val;
    } else {
      frame.locals[idx] = val;
    }

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_LoadGlobal: {
    size_t idx = ip->operand0.value();
    if (idx >= func_ptr->names_.size()) {
      throw RuntimeException("LoadGlobal: name index out of range");
    }
    size_t slot = get_global_slot(func_ptr, idx);
    if (slot >= globals_initialized.size() || !globals_initialized[slot]) {
      throw UninitializedVariableException("Undefined global at slot " +
                                           std::to_string(slot));
    }
    push(frame, globals_by_id[slot]);

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_StoreGlobal: {
    size_t idx = ip->operand0.value();
    if (idx >= func_ptr->names_.size()) {
      throw RuntimeException("StoreGlobal: name index out of range");
    }
    TaggedValue v = pop(frame);
    size_t slot = get_global_slot(func_ptr, idx);
    globals_by_id[slot] = v;
    if (slot >= globals_initialized.size()) {
      globals_initialized.resize(slot + 1, false);
    }
    globals_initialized[slot] = true;

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_PushReference: {
    int32_t idx = ip->operand0.value();
    Value *ref;

    if (idx < static_cast<int32_t>(func_ptr->local_reference_vars_.size())) {
      const std::string &var_name = func_ptr->local_reference_vars_[idx];
      ref = frame.local_refs[var_name];
    } else {
      int32_t free_idx = idx - func_ptr->local_reference_vars_.size();
      if (free_idx < 0 ||
          free_idx >= static_cast<int32_t>(free_refs_local.size())) {
        throw RuntimeException(
            "PushReference: free variable index out of range");
      }
      ref = free_refs_local[free_idx];
    }
    push(frame, TaggedValue::from_heap(ref));

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_LoadReference: {
    TaggedValue ref_val = pop(frame);
    if (ref_val.kind != TaggedValue::Kind::HeapPtr ||
        ref_val.ptr->tag != Value::Type::Reference)
      throw IllegalCastException("Expected reference");
    auto ref = static_cast<Reference *>(ref_val.ptr);
    push(frame, tagged_from_value(ref->cell));

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_StoreReference: {
    TaggedValue val = pop(frame);
    TaggedValue ref_val = pop(frame);
    if (ref_val.kind != TaggedValue::Kind::HeapPtr ||
        ref_val.ptr->tag != Value::Type::Reference)
      throw IllegalCastException("Expected reference");
    auto ref = static_cast<Reference *>(ref_val.ptr);
    ref->cell = box_tagged(val);
    heap.write_barrier(ref, ref->cell);

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_AllocRecord:
    push(frame, TaggedValue::from_heap(allocate<Record>()));

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();

  op_FieldLoad: {
    TaggedValue rec_val = pop(frame);
    if (rec_val.kind != TaggedValue::Kind::HeapPtr ||
        rec_val.ptr->tag != Value::Type::Record)
      throw IllegalCastException("Expected record");
    auto rec = static_cast<Record *>(rec_val.ptr);
    size_t idx = ip->operand0.value();
    if (idx >= func_ptr->names_.size()) {
      throw RuntimeException("FieldLoad: name index out of range");
    }
    size_t slot = get_field_slot(func_ptr, idx);
    if (slot >= rec->named_fields.size()) {
      rec->named_fields.resize(slot + 1, nullptr);
    }
    Value *field_val = rec->named_fields[slot];
    push(frame, field_val ? tagged_from_value(field_val) : TaggedValue::none());

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_FieldStore: {
    TaggedValue val = pop(frame);
    TaggedValue rec_val = pop(frame);
    if (rec_val.kind != TaggedValue::Kind::HeapPtr ||
        rec_val.ptr->tag != Value::Type::Record)
      throw IllegalCastException("Expected record");
    auto rec = static_cast<Record *>(rec_val.ptr);
    size_t idx = ip->operand0.value();
    if (idx >= func_ptr->names_.size()) {
      throw RuntimeException("FieldStore: name index out of range");
    }
    Value *boxed = box_tagged(val);
    size_t slot = get_field_slot(func_ptr, idx);
    if (slot >= rec->named_fields.size()) {
      rec->named_fields.resize(slot + 1, nullptr);
    }
    rec->named_fields[slot] = boxed;
    rec->fields[func_ptr->names_[idx]] = boxed;
    heap.write_barrier(rec, boxed);

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_IndexLoad: {
    TaggedValue idx_val = pop(frame);
    TaggedValue rec_val = pop(frame);
    if (rec_val.kind != TaggedValue::Kind::HeapPtr ||
        rec_val.ptr->tag != Value::Type::Record)
      throw IllegalCastException("Expected record");
    auto rec = static_cast<Record *>(rec_val.ptr);

    TaggedValue result = TaggedValue::none();
    if (!record_try_dense_load(rec, idx_val, result)) {
      result = record_map_load(rec, idx_val);
    }
    push(frame, result);

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_IndexStore: {
    TaggedValue val = pop(frame);
    TaggedValue idx_val = pop(frame);
    TaggedValue rec_val = pop(frame);
    if (rec_val.kind != TaggedValue::Kind::HeapPtr ||
        rec_val.ptr->tag != Value::Type::Record)
      throw IllegalCastException("Expected record");
    auto rec = static_cast<Record *>(rec_val.ptr);

    if (!record_try_dense_store(rec, idx_val, val)) {
      record_map_store(rec, idx_val, val);
    }

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_AllocClosure: {
    int32_t free_count = ip->operand0.value();
    temp_refs.clear();
    temp_refs.reserve(free_count);
    for (int i = 0; i < free_count; ++i) {
      TaggedValue tv = pop(frame);
      if (tv.kind != TaggedValue::Kind::HeapPtr ||
          tv.ptr->tag != Value::Type::Reference)
        throw IllegalCastException("Expected reference");
      temp_refs.push_back(tv.ptr);
    }
    std::reverse(temp_refs.begin(), temp_refs.end());

    TaggedValue func_val = pop(frame);
    if (func_val.kind != TaggedValue::Kind::HeapPtr ||
        func_val.ptr->tag != Value::Type::Function)
      throw IllegalCastException("Expected function");
    auto f = static_cast<Function *>(func_val.ptr);

    for (Value *ref : temp_refs) {
      push(frame, TaggedValue::from_heap(ref));
    }
    push(frame, func_val);

    Value *closure_val = allocate<Closure>(f->func, temp_refs);

    pop(frame); // Remove duplicate func
    for (size_t i = 0; i < temp_refs.size(); ++i) {
      pop(frame);
    }

    push(frame, TaggedValue::from_heap(closure_val));

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_Call: {
    int32_t arg_count = ip->operand0.value();
    temp_args.clear();
    temp_args.reserve(arg_count);
    for (int i = 0; i < arg_count; ++i)
      temp_args.push_back(pop(frame));
    std::reverse(temp_args.begin(), temp_args.end());

    TaggedValue closure_val = pop(frame);

    if (closure_val.kind == TaggedValue::Kind::HeapPtr &&
        closure_val.ptr->tag == Value::Type::Closure) {
      auto closure = static_cast<Closure *>(closure_val.ptr);
      push(frame, execute_function(closure->function, temp_args,
                                   closure->free_var_refs));
    } else if (closure_val.kind == TaggedValue::Kind::HeapPtr &&
               closure_val.ptr->tag == Value::Type::Function) {
      auto func_ptr_local = static_cast<Function *>(closure_val.ptr);
      push(frame, execute_function(func_ptr_local->func, temp_args, {}));
    } else {
      throw IllegalCastException("Expected closure or function");
    }

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_Return: {
    TaggedValue ret_val = pop(frame);

    frame.sp = 0;

    push(frame, ret_val);
    returned_flag = true;
    goto function_epilogue;
  }

  op_Add: {
    TaggedValue right = pop(frame);
    TaggedValue left = pop(frame);

    auto is_string = [](const TaggedValue &tv) {
      return tv.kind == TaggedValue::Kind::HeapPtr &&
             tv.ptr->tag == Value::Type::String;
    };

    if ((left.kind == TaggedValue::Kind::Integer ||
         (left.kind == TaggedValue::Kind::HeapPtr &&
          left.ptr->tag == Value::Type::Integer)) &&
        (right.kind == TaggedValue::Kind::Integer ||
         (right.kind == TaggedValue::Kind::HeapPtr &&
          right.ptr->tag == Value::Type::Integer))) {
      auto li = get_int(left);
      auto ri = get_int(right);
      push(frame, TaggedValue::from_int(li + ri));
    } else if (is_string(left) || is_string(right)) {
      push(frame, TaggedValue::from_heap(allocate<String>(
                      tagged_to_string(left) + tagged_to_string(right))));
    } else {
      throw IllegalCastException("Invalid operand types for add");
    }

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_Sub: {
    TaggedValue right = pop(frame);
    TaggedValue left = pop(frame);
    if ((left.kind == TaggedValue::Kind::Integer ||
         (left.kind == TaggedValue::Kind::HeapPtr &&
          left.ptr->tag == Value::Type::Integer)) &&
        (right.kind == TaggedValue::Kind::Integer ||
         (right.kind == TaggedValue::Kind::HeapPtr &&
          right.ptr->tag == Value::Type::Integer))) {
      auto li = get_int(left);
      auto ri = get_int(right);
      push(frame, TaggedValue::from_int(li - ri));
    } else {
      throw IllegalCastException("Invalid operand types for subtract");
    }

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_Mul: {
    TaggedValue right = pop(frame);
    TaggedValue left = pop(frame);
    if ((left.kind == TaggedValue::Kind::Integer ||
         (left.kind == TaggedValue::Kind::HeapPtr &&
          left.ptr->tag == Value::Type::Integer)) &&
        (right.kind == TaggedValue::Kind::Integer ||
         (right.kind == TaggedValue::Kind::HeapPtr &&
          right.ptr->tag == Value::Type::Integer))) {
      auto li = get_int(left);
      auto ri = get_int(right);
      push(frame, TaggedValue::from_int(li * ri));
    } else {
      throw IllegalCastException("Invalid operand types for multiply");
    }

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_Div: {
    TaggedValue right = pop(frame);
    TaggedValue left = pop(frame);
    if ((left.kind == TaggedValue::Kind::Integer ||
         (left.kind == TaggedValue::Kind::HeapPtr &&
          left.ptr->tag == Value::Type::Integer)) &&
        (right.kind == TaggedValue::Kind::Integer ||
         (right.kind == TaggedValue::Kind::HeapPtr &&
          right.ptr->tag == Value::Type::Integer))) {
      auto li = get_int(left);
      auto ri = get_int(right);
      if (ri == 0)
        throw IllegalArithmeticException("Division by zero");
      push(frame, TaggedValue::from_int(li / ri));
    } else {
      throw IllegalCastException("Invalid operand types for divide");
    }

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_Neg: {
    TaggedValue left = pop(frame);
    if (left.kind == TaggedValue::Kind::Integer ||
        (left.kind == TaggedValue::Kind::HeapPtr &&
         left.ptr->tag == Value::Type::Integer)) {
      auto li = get_int(left);
      push(frame, TaggedValue::from_int(-li));
    } else {
      throw IllegalCastException("Invalid operand types for negate");
    }

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_Gt: {
    TaggedValue right = pop(frame);
    TaggedValue left = pop(frame);
    if ((left.kind == TaggedValue::Kind::Integer ||
         (left.kind == TaggedValue::Kind::HeapPtr &&
          left.ptr->tag == Value::Type::Integer)) &&
        (right.kind == TaggedValue::Kind::Integer ||
         (right.kind == TaggedValue::Kind::HeapPtr &&
          right.ptr->tag == Value::Type::Integer))) {
      auto li = get_int(left);
      auto ri = get_int(right);
      push(frame, TaggedValue::from_bool(li > ri));
    } else {
      throw IllegalCastException("Invalid operand types for greater than");
    }

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_Geq: {
    TaggedValue right = pop(frame);
    TaggedValue left = pop(frame);
    if ((left.kind == TaggedValue::Kind::Integer ||
         (left.kind == TaggedValue::Kind::HeapPtr &&
          left.ptr->tag == Value::Type::Integer)) &&
        (right.kind == TaggedValue::Kind::Integer ||
         (right.kind == TaggedValue::Kind::HeapPtr &&
          right.ptr->tag == Value::Type::Integer))) {
      auto li = get_int(left);
      auto ri = get_int(right);
      push(frame, TaggedValue::from_bool(li >= ri));
    } else {
      throw IllegalCastException(
          "Invalid operand types for greater than or equal");
    }

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_Eq: {
    TaggedValue right = pop(frame);
    TaggedValue left = pop(frame);
    push(frame, TaggedValue::from_bool(values_equal(left, right)));

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_And: {
    TaggedValue right = pop(frame);
    TaggedValue left = pop(frame);
    if ((left.kind == TaggedValue::Kind::Boolean ||
         (left.kind == TaggedValue::Kind::HeapPtr &&
          left.ptr->tag == Value::Type::Boolean)) &&
        (right.kind == TaggedValue::Kind::Boolean ||
         (right.kind == TaggedValue::Kind::HeapPtr &&
          right.ptr->tag == Value::Type::Boolean))) {
      auto lb = get_bool(left);
      auto rb = get_bool(right);
      push(frame, TaggedValue::from_bool(lb && rb));
    } else {
      throw IllegalCastException("Invalid operand types for and");
    }

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_Or: {
    TaggedValue right = pop(frame);
    TaggedValue left = pop(frame);
    if ((left.kind == TaggedValue::Kind::Boolean ||
         (left.kind == TaggedValue::Kind::HeapPtr &&
          left.ptr->tag == Value::Type::Boolean)) &&
        (right.kind == TaggedValue::Kind::Boolean ||
         (right.kind == TaggedValue::Kind::HeapPtr &&
          right.ptr->tag == Value::Type::Boolean))) {
      auto lb = get_bool(left);
      auto rb = get_bool(right);
      push(frame, TaggedValue::from_bool(lb || rb));
    } else {
      throw IllegalCastException("Invalid operand types for or");
    }

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_Not: {
    TaggedValue left = pop(frame);
    if (left.kind == TaggedValue::Kind::Boolean ||
        (left.kind == TaggedValue::Kind::HeapPtr &&
         left.ptr->tag == Value::Type::Boolean)) {
      auto lb = get_bool(left);
      push(frame, TaggedValue::from_bool(!lb));
    } else {
      throw IllegalCastException("Invalid operand types for not");
    }

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_Goto: {
    int32_t offset = ip->operand0.value();
    ip += offset;
    if (ip < instructions.data() || ip >= end) {
      throw RuntimeException("Goto: target out of range");
    }
    DISPATCH();
  }

  op_If: {
    TaggedValue cond = pop(frame);
    if (cond.kind != TaggedValue::Kind::Boolean &&
        !(cond.kind == TaggedValue::Kind::HeapPtr &&
          cond.ptr->tag == Value::Type::Boolean))
      throw IllegalCastException("Invalid operand types for if");

    if (get_bool(cond)) {
      int32_t offset = ip->operand0.value();
      ip += offset;
    } else {
      ++ip;
    }
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_Dup: {
    TaggedValue v = pop(frame);
    push(frame, v);
    push(frame, v);

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_Swap: {
    TaggedValue a = pop(frame);
    TaggedValue b = pop(frame);
    push(frame, a);
    push(frame, b);

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();
  }

  op_Pop:
    pop(frame);

    ++ip;
    if (ip == end)
      goto function_epilogue;
    DISPATCH();

  function_epilogue:
    call_stack.pop_back();

    bool is_main = call_stack.empty();
    if (!returned_flag) {
      if (is_main) {
        return TaggedValue::none();
      }
      throw RuntimeException("Function must end with a return statement");
    }

    if (stack_empty(frame)) {
      return TaggedValue::none();
    }

    if (stack_size(frame) > 1) {
      throw RuntimeException("Function ended with invalid stack state");
    }

    return pop(frame);

#undef DISPATCH
  }

  bool values_equal(const TaggedValue &left, const TaggedValue &right) {
    auto is_none = [](const TaggedValue &tv) {
      return tv.kind == TaggedValue::Kind::None ||
             (tv.kind == TaggedValue::Kind::HeapPtr &&
              tv.ptr->tag == Value::Type::None);
    };
    auto is_bool = [](const TaggedValue &tv) {
      return tv.kind == TaggedValue::Kind::Boolean ||
             (tv.kind == TaggedValue::Kind::HeapPtr &&
              tv.ptr->tag == Value::Type::Boolean);
    };
    auto is_int = [](const TaggedValue &tv) {
      return tv.kind == TaggedValue::Kind::Integer ||
             (tv.kind == TaggedValue::Kind::HeapPtr &&
              tv.ptr->tag == Value::Type::Integer);
    };

    if (is_none(left) && is_none(right))
      return true;
    if (is_int(left) && is_int(right))
      return get_int(left) == get_int(right);
    if (is_bool(left) && is_bool(right))
      return get_bool(left) == get_bool(right);

    if (left.kind == TaggedValue::Kind::HeapPtr &&
        right.kind == TaggedValue::Kind::HeapPtr) {
      if (left.ptr->tag != right.ptr->tag)
        return false;
      switch (left.ptr->tag) {
      case Value::Type::String:
        return static_cast<String *>(left.ptr)->value ==
               static_cast<String *>(right.ptr)->value;
      case Value::Type::Record:
      case Value::Type::Function:
      case Value::Type::Closure:
      case Value::Type::Reference:
        return left.ptr == right.ptr;
      case Value::Type::None:
        return true;
      case Value::Type::Boolean:
      case Value::Type::Integer:
        // Should be covered above, but keep for completeness.
        return values_equal(tagged_from_value(left.ptr),
                            tagged_from_value(right.ptr));
      }
    }
    return false;
  }

public:
  explicit VM(size_t max_mem_mb = 10000, bool enable_jit = false)
      : jit_enabled(enable_jit), max_heap_bytes(max_mem_mb * 1024 * 1024) {
    // Bypass allocate wrapper to avoid premature GC before roots are known.
    none_singleton = heap.allocate<None>();
    bool_true_singleton = heap.allocate<Boolean>(true);
    bool_false_singleton = heap.allocate<Boolean>(false);
  }

  void run(bytecode::Function *main_func) {
    if (jit_enabled) {
      // Translate and JIT-compile the entire program to the faster native
      // entrypoints before execution. The generated stubs hand off to the
      // register-based interpreter, avoiding repeated decode overhead.
      ensure_jit_translation(main_func);
    }

    // Eagerly translate the entire function tree to the register-based
    // instruction set so we always execute the faster interpreter.
    translate_function_tree(main_func);

    // Mark first 3 functions as native with their IDs
    if (main_func->functions_.size() >= 3) {
      native_functions[main_func->functions_[0]] = 0; // print
      native_functions[main_func->functions_[1]] = 1; // input
      native_functions[main_func->functions_[2]] = 2; // intcast
    }

    execute_function(main_func, {}, {});
  }
};

} // namespace vm
