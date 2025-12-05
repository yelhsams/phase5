#include "./ast.hpp"
#include "./interpreter.hpp"
#include "../gc/gc.hpp"
#include <unordered_set>
#include <iostream>
#include <functional>
#include <unordered_map>
#include <cstdlib>
#include <stdexcept>
#include <vector>
#include <algorithm>

struct EnumHash {
  template <class T>
  size_t operator()(T v) const noexcept { return static_cast<size_t>(v); }
};

// Helper to raise cast errors consistently
static inline void cast_error(const char* msg) {
  throw std::runtime_error(std::string("IllegalCastException -- ") + msg);
}

// Helper to raise arithmetic errors consistently
static inline void arith_error(const char* msg) {
  throw std::runtime_error(std::string("IllegalArithmeticException -- ") + msg);
}

namespace mitscript {

    class StackFrame;
    class Value;
    class BooleanValue;
    class IntegerValue;
    class StringValue;
    class RecordValue;
    class FunctionValue;
    class NoneValue;

    class Interpreter;

    class Value : public Collectable {
        public:
            virtual ~Value() {};
            virtual std::string toString() const = 0;
    };

    struct ReturnSignal {Value* value;};
    std::string str(Value*);
    // Forward declarations
    void collect_globals(Block* b, std::unordered_set<std::string>& globals);
    void collect_globals(IfStatement* if_stmt, std::unordered_set<std::string>& globals);
    void collect_globals(WhileLoop* while_stmt, std::unordered_set<std::string>& globals);
    // Forward declarations for variable collection helpers
    void collect_vars(Block* b, std::unordered_set<std::string>& vars);
    void collect_vars(IfStatement* if_stmt, std::unordered_set<std::string>& vars);
    void collect_vars(WhileLoop* while_stmt, std::unordered_set<std::string>& vars);
    void collect_vars(Assignment* assign_stmt, std::unordered_set<std::string>& vars);

    std::unordered_set<std::string> get_globals(Block* b) {
        std::unordered_set<std::string> globals;
        collect_globals(b, globals);
        return globals;
    }

    void collect_globals(Block* b, std::unordered_set<std::string>& globals) {
        for (const auto& statement_ptr : b->statements) {
            Statement* statement = statement_ptr.get();
            if (auto if_stmt = dynamic_cast<IfStatement*>(statement)) {
                collect_globals(if_stmt, globals);
            } else if (auto while_stmt = dynamic_cast<WhileLoop*>(statement)) {
                collect_globals(while_stmt, globals);
            } else if (auto global_stmt = dynamic_cast<Global*>(statement)) {
                globals.insert(global_stmt->name);
            } else if (auto block_stmt = dynamic_cast<Block*>(statement)) {
                collect_globals(block_stmt, globals);
            }
        }
    }

    void collect_globals(IfStatement* if_stmt, std::unordered_set<std::string>& globals) {
        if (if_stmt->then_block) {
            collect_globals(if_stmt->then_block.get(), globals);
        }
        if (if_stmt->else_block) {
            collect_globals(if_stmt->else_block.get(), globals);
        }
    }

    void collect_globals(WhileLoop* while_stmt, std::unordered_set<std::string>& globals) {
        collect_globals(while_stmt->body.get(), globals);
    }

    std::unordered_set<std::string> get_vars(Block* b) {
        std::unordered_set<std::string> vars;
        collect_vars(b, vars);
        return vars;
    }

    void collect_vars(Block* b, std::unordered_set<std::string>& vars) {
        for (const auto& statement_ptr : b->statements) {
            Statement* statement = statement_ptr.get();
            if (auto if_stmt = dynamic_cast<IfStatement*>(statement)) {
                collect_vars(if_stmt, vars);
            } else if (auto while_stmt = dynamic_cast<WhileLoop*>(statement)) {
                collect_vars(while_stmt, vars);
            } else if (auto assign_stmt = dynamic_cast<Assignment*>(statement)) {
                collect_vars(assign_stmt, vars);
            } else if (auto block_stmt = dynamic_cast<Block*>(statement)) {
                collect_vars(block_stmt, vars);
            }
        }
    }

    void collect_vars(IfStatement* if_stmt, std::unordered_set<std::string>& vars) {
        if (if_stmt->then_block) {
            collect_vars(if_stmt->then_block.get(), vars);
        }
        if (if_stmt->else_block) {
            collect_vars(if_stmt->else_block.get(), vars);
        }
    }

    void collect_vars(WhileLoop* while_stmt, std::unordered_set<std::string>& vars) {
        collect_vars(while_stmt->body.get(), vars);
    }

    void collect_vars(Assignment* assign_stmt, std::unordered_set<std::string>& vars) {
        if (auto var_target = dynamic_cast<Variable*>(assign_stmt->target.get())) {
            vars.insert(var_target->name);
        }
    }



    // Forward declaration of operator evaluation table (defined later)
    extern std::unordered_map<BinOp, std::function<Value*(Value*, Value*, CollectedHeap*)>, EnumHash> evaluate_lambdas;

    class BooleanValue : public Value {
        public:
            // bool value;
            // BooleanValue(bool v) : value(v) {};

            static BooleanValue* true_instance() {
                static BooleanValue true_instance(true);
                return &true_instance;
            };

            static BooleanValue* false_instance() {
                static BooleanValue false_instance(false);
                return &false_instance;
            }

            static BooleanValue* from(bool b) { return b ? true_instance() : false_instance(); }


            std::string toString() const override {
                return value_ ? "true" : "false";
            }
            // Public read accessor for truthiness
            bool value() const { return value_; }

            protected:
                void follow(CollectedHeap&) override {}
            private:
                BooleanValue(bool v) : value_(v) {};
                ~BooleanValue() override = default;
                bool value_;
    };




    class IntegerValue : public Value {
        public:
            int value;
            IntegerValue(int v) : value(v) {};
            std::string toString() const override {
                return std::to_string(value);
            }
            void follow(CollectedHeap&) override {}
    };

    class StringValue : public Value {
        public:
            std::string value;
            StringValue(std::string v) : value(v) {};
            std::string toString() const override {
                return value;
            }
            void follow(CollectedHeap&) override {}
    };

    class NoneValue: public Value {
        public:
            static NoneValue* instance() {
                static NoneValue instance;
                return &instance;
            }
            // NoneValue() {};
            std::string toString() const override { return "None"; }
            void follow(CollectedHeap&) override {}
        private:
            NoneValue() = default;
            ~NoneValue() override = default;


    };

    class RecordValue : public Value {
        public:
            std::unordered_map<std::string, Value*> record_map;
            RecordValue(std::unordered_map<std::string, Value*> map) : record_map(map) {};

            void add_entry(std::string key, Value* v) {
                record_map[key] = v;
            };

            Value* get_entry(const std::string& key, CollectedHeap*) {
                auto it = record_map.find(key);
                if (it != record_map.end()) return it->second;
                return NoneValue::instance();
            }

            std::string toString() const override {
                std::string result = "{";

                std::vector<std::pair<std::string, Value*>> var_value_list;
                for (const auto& pair: record_map) {
                    var_value_list.push_back(pair);
                }

                std::sort(var_value_list.begin(), var_value_list.end());

                for (const auto& pair : var_value_list) {
                    result += pair.first + ":" + pair.second->toString() + " ";
                }
                // if (!record_map.empty()) {
                //     result.pop_back();
                // }
                result += "}";
                return result;
            }
            void follow(CollectedHeap& heap) override {
                for (const auto& pair : record_map) {
                    heap.markSuccessors(pair.second);
                }
            }
    };

    class FunctionValue : public Value {
        public:
            StackFrame* defining_env;
            std::vector<std::string> args;
            Block* body;

            FunctionValue(StackFrame* defining_env, std::vector<std::string> x, Block* s)
                : defining_env(defining_env), args(x), body(s) {};

            std::string toString() const override {
                return "FUNCTION";
            }

            void follow(CollectedHeap& heap) override;

    };

    class NativeFunctionValue: public Value {
        public:
            using Impl = std::function<Value*(const std::vector<Value*>&, Interpreter&)>;
            std::string name;
            Impl impl;
            size_t arg_count;
            NativeFunctionValue(std::string n, size_t a, Impl f)
            : name(std::move(n)), impl(std::move(f)), arg_count(a) {}

            std::string toString() const override { return "FUNCTION"; }
            void follow(CollectedHeap&) override {}
    };

    class StackFrame: public Collectable {
        public:
            StackFrame* global_frame;
            std::unordered_map<std::string, Value*> frame;
            StackFrame* parent_frame;
            std::unordered_set<std::string> global_vars;
            CollectedHeap* heap;

            StackFrame(StackFrame* parent, CollectedHeap* heap)
                : global_frame(parent ? parent->global_frame : this),
                  parent_frame(parent), heap(heap) {
                        if (!parent) {
                            // Print Built-In Function
                        global_frame->frame["print"] = heap->allocate<NativeFunctionValue>("print", 1, [](const std::vector<Value*>& args, Interpreter& interp) -> Value* {
                            (void)interp;
                            std::string out = str(args[0]);
                            while (!out.empty() && out.back() == ' ') out.pop_back();
                            std::cout << out << std::endl;
                            return NoneValue::instance();
                        });

                            global_frame->frame["input"] = heap->allocate<NativeFunctionValue>("input", 0, [heap](const std::vector<Value*>& args, Interpreter& interp) -> Value* {
                            (void)args;
                            (void)interp;
                            std::string line;
                            std::getline(std::cin, line);
                            return heap->allocate<StringValue>(line);
                        });

                        // global_frame -> frame["intcast"] = new NativeFunctionValue("intcast", 1, [](const std::vector<Value*>& args, Interpreter& interp) -> Value* {
                        global_frame -> frame["intcast"] = heap->allocate<NativeFunctionValue>("intcast", 1, [heap](const std::vector<Value*>& args, Interpreter& interp) -> Value* {
                            (void)interp;
                            return heap->allocate<IntegerValue>(atoi(str(args[0]).c_str()));
                        });

                    }
                    };

            void follow(CollectedHeap& heap) override {
                for (const auto& pair : frame) {
                    heap.markSuccessors(pair.second);
                }
                if (parent_frame) {
                    heap.markSuccessors(parent_frame);
                }
                if (global_frame && global_frame != this) {
                    heap.markSuccessors(global_frame);
                }
            }

            void set_global(const std::string& name) {
                global_vars.insert(name);

                auto it = frame.find(name);
                if (it != frame.end()) {
                    global_frame->frame[name] = it->second;
                    frame.erase(it);
                }
                if (global_frame->frame.find(name) == global_frame->frame.end()) {
                    global_frame->frame[name] = NoneValue::instance();
                }
            }


            void set_variable(const std::string& name, Value* value) {
                StackFrame& frame_to_write = lookup_write(name);
                frame_to_write.frame[name] = value;
            }

        Value* lookup_read(std::string name) {
            if (global_vars.count(name) && global_frame != this){
                return global_frame -> lookup_read(name);
            }

            if (frame.find(name) != frame.end()) {
                return frame[name];
            }

            if (parent_frame != nullptr) {
                return parent_frame -> lookup_read(name);
            }

            throw std::runtime_error("UninitializedVariableException - Variable not found: " + name);
        };

        StackFrame& lookup_write(std::string name) {
            return global_vars.count(name) ? *global_frame : *this;
        }


    };

    class Interpreter : public Visitor {
        public:

            CollectedHeap* heap;


            Value* rval_;
            std::vector<StackFrame*> stack;
            Interpreter() : heap(new CollectedHeap()), rval_(nullptr), stack() {
                stack.push_back(heap->allocate<StackFrame>(nullptr, heap));
            }

            void visit(AST* node) {
                for (const auto& stmt : node -> statements) {
                    stmt -> accept(this);
                }

            };

            void visit(Block* node) {
                for (const auto& statement : node -> statements) {
                    statement ->accept(this);
                }
            };

            void visit(Assignment* node) {


                if (auto var_assignment = dynamic_cast<Variable*>(node -> target.get())) {
                    std::string var_name = var_assignment -> name;

                    node -> value -> accept(this);
                    Value* value = rval_;
                    stack.back() -> set_variable(var_name, value);

                } else if (auto field_deref = dynamic_cast<FieldDereference*>(node -> target.get())) {
                    auto* object = field_deref -> object.get();
                    object -> accept(this);
                    Value* target = rval_;
                    if (auto rec = dynamic_cast<RecordValue*>(target)) {

                        node -> value -> accept(this);
                        Value* value = rval_;

                        rec -> add_entry(field_deref -> field_name, value);
                    } else {
                        throw std::runtime_error("IllegalCastException -- Expected Record Type for Field Dereference");
                    }

                } else if (auto idx_expr = dynamic_cast<IndexExpression*>(node -> target.get())) {
                    idx_expr -> baseExpression -> accept(this);
                    Value* target = rval_;
                    idx_expr -> indexExpression -> accept(this);
                    Value* index = rval_;
                    if (auto rec = dynamic_cast<RecordValue*>(target)) {
                        node -> value -> accept(this);
                        Value* value = rval_;
                        rec -> add_entry(str(index), value);
                    } else {
                        throw std::runtime_error("IllegalCastException -- Expected Record Type for Index Expression");
                    }


                }

            }

            void visit(IfStatement* node) {
                node -> condition -> accept(this);
                Value* result = rval_;
                if (auto condition_result = dynamic_cast<BooleanValue*>(result)) {
                    if (condition_result->value()) {
                        node ->then_block -> accept(this);

                    } else {
                        if (node -> else_block) {
                            node -> else_block -> accept(this);
                        }
                    }

                } else {
                    throw std::runtime_error("IllegalCastException -- Condition should evaluate to a boolean");
                }


            }

            void visit(Return* node) {
                // If we are at top-level (only the global frame on stack), returning is illegal
                if (stack.size() <= 1) {
                    node->value->accept(this); // evaluate for side effects (if any)
                    throw std::runtime_error("RuntimeException -- return outside function");
                }
                node->value->accept(this);
                throw ReturnSignal{rval_};
            }

            void visit(BinaryExpression* node) {
                node -> left -> accept(this);
                Value* left_val = rval_;
                node -> right -> accept(this);
                Value* right_val = rval_;
                auto op = node -> op;

                rval_ = evaluate_lambdas.at(op)(left_val, right_val, heap);

            }

            void visit(FieldDereference* node) {
                node -> object -> accept(this);
                Value* record = rval_;
                if (auto record_val = dynamic_cast<RecordValue*>(record)) {
                    rval_ = record_val -> get_entry(node ->field_name, heap);
                } else {
                    throw std::runtime_error("IllegalCastException -- Expected Record Type for Field Dereference");
                }
            };

            void visit(Call* node) {
                node -> callee -> accept(this);
                Value* func = rval_;
                if (auto native_func_expr = dynamic_cast<NativeFunctionValue*>(func)) {

                    std::vector<Value*> evaluated_arguments;
                    for (const auto& arg : node->arguments) { arg->accept(this); evaluated_arguments.push_back(rval_); }
                    if (native_func_expr->arg_count != evaluated_arguments.size()) {

                        throw std::runtime_error("RuntimeException -- " + native_func_expr->name +
                                                " expects " + std::to_string(native_func_expr->arg_count) +
                                                " args, got " + std::to_string(node->arguments.size()));
                    }
                    rval_ = native_func_expr->impl(evaluated_arguments, *this);   // no new frame for natives
                    return;

                } else if (auto func_expr = dynamic_cast<FunctionValue*>(func)) {
                    std::vector<Value*> evaluated_arguments;
                    for (const auto& arg : node -> arguments) {
                        arg -> accept(this);
                        evaluated_arguments.push_back(rval_);
                    }

                    if (evaluated_arguments.size() != func_expr -> args.size()) {
                        throw std::runtime_error("RuntimeException -- argument count mismatch");
                    }

                    StackFrame *new_frame = heap->allocate<StackFrame>(func_expr -> defining_env, heap);
                    // StackFrame *new_frame = new StackFrame(func_expr -> defining_env);

                    // for (const auto& stmt : func_expr->body->statements) {
                    //     if (auto glob_stmt = dynamic_cast<Global*>(stmt.get())) {
                    //         new_frame->set_global(glob_stmt->name);
                    //     }
                    // }
                    auto global_vars = get_globals(func_expr -> body);
                    auto vars = get_vars(func_expr -> body);

                    for (const auto& var : global_vars) {
                        new_frame -> set_global(var);
                    }

                    for (const auto& var : vars) {
                        if (new_frame -> global_vars.count(var)) {
                            continue;
                        }
                        new_frame -> frame[var] = NoneValue::instance();

                    }

                    for (size_t i = 0; i < func_expr -> args.size(); i++) {
                        std::string param_name = func_expr ->args[i];
                        if (new_frame -> global_vars.count(param_name)) {
                            continue;
                        }
                        new_frame -> frame[param_name] = evaluated_arguments[i];
                    };

                    this -> stack.push_back(new_frame);

                    try {
                        rval_ = NoneValue::instance();
                        func_expr -> body -> accept(this);
                        rval_ = NoneValue::instance();
                    } catch (ReturnSignal rs) {
                        rval_ = rs.value;
                    }
                    this -> stack.pop_back();



                } else {
                    throw std::runtime_error("IllegalCastException -- Expected A function value for a function call");

                };
            }

            void visit(IntegerConstant* node) {
                rval_ = heap->allocate<IntegerValue>(node -> value);
            }

            void visit(NoneConstant* /*node*/) {
                rval_ = NoneValue::instance();
            }

            void visit(Global* node) {
                stack.back() -> set_global(node -> name);
            }

            void visit(CallStatement* node) {
                node -> call -> accept(this);
            }

            void visit(WhileLoop* node) {
                while (true) {
                    node -> condition -> accept(this);
                    if (auto condition_res = dynamic_cast<BooleanValue*>(rval_)) {
                        if (condition_res->value()) {
                            node -> body -> accept(this);
                        } else {
                            break;
                        }
                    } else {
                        throw std::runtime_error("IllegalCastException -- Condition must evaluate to a BooleanValue");
                    }
                }
            }

            void visit(FunctionDeclaration* node) {
                FunctionValue* new_function = heap->allocate<FunctionValue>(
                    stack.back(), node->args, node->body.get()
                );
                rval_ = new_function;

            }

            void visit(UnaryExpression* node) {
                node -> operand -> accept(this);
                if (auto operand_int = dynamic_cast<IntegerValue*>(rval_)) {
                    if (node -> op == UnOp::NEG) {
                        IntegerValue* adj_int = heap->allocate<IntegerValue>(-1 * operand_int -> value);
                        rval_ = adj_int;
                    } else {
                        throw std::runtime_error("IllegalCastException -- Only Negative operator works for integer types");
                    }
                } else if (auto operand_bool = dynamic_cast<BooleanValue*>(rval_)) {
                    if (node -> op == UnOp::NOT) {
                        rval_ = BooleanValue::from(!operand_bool->value());
                    } else {
                        throw std::runtime_error("IllegalCastException -- Only Not operator works for boolean types");
                    }
                } else {
                    throw std::runtime_error("IllegalCastException -- Unary operators only apply to Ints and Bools");
                }

            }

            void visit(IndexExpression* node) {
                node -> baseExpression -> accept(this);
                if (auto record_obj = dynamic_cast<RecordValue*>(rval_)) {
                    node -> indexExpression -> accept(this);
                    rval_ = record_obj -> get_entry(str(rval_), heap);
                } else {
                    throw std::runtime_error("IllegalCastException -- Cannot index into non record type");
                }
            }

            void visit(Record* node) {
                std::unordered_map<std::string, Value*> map;

                for (size_t i = 0; i < node -> fields.size(); i++) {
                    const auto& pair = node->fields[i];
                    std::string name = pair.first;
                    Expression* expr = pair.second.get();
                    expr -> accept(this);
                    map[name] = rval_;
                }

                rval_ = heap->allocate<RecordValue>(map);


            }

            void visit(StringConstant* node) {
                const std::string& raw = node->value;
                if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
                    std::string inner = raw.substr(1, raw.size() - 2);
                    rval_ = heap->allocate<StringValue>(inner);
                } else {
                    rval_ = heap->allocate<StringValue>(raw);
                }
            }

            void visit(BooleanConstant* node) {
                rval_ = BooleanValue::from(node->value);
            }

            void visit(Variable* node) {
                rval_ = stack.back() -> lookup_read(node -> name);
            }





    };

    // Implement after StackFrame is fully defined
    void FunctionValue::follow(CollectedHeap& heap) {
        heap.markSuccessors(defining_env);
    }

    void interpret(AST &node) {
        Interpreter interpreter;
        try {
            node.accept(&interpreter);
        } catch (const ReturnSignal &rs) {
            throw std::runtime_error("RuntimeException -- return outside function");
        }

    }

    std::string str(Value* val) {
        if (dynamic_cast<StringValue*>(val))  return val->toString();
        if (dynamic_cast<BooleanValue*>(val)) return val->toString();
        if (dynamic_cast<IntegerValue*>(val)) return val->toString();
        if (dynamic_cast<FunctionValue*>(val)) return "FUNCTION";
        if (dynamic_cast<RecordValue*>(val)) return val->toString();
        if (dynamic_cast<NoneValue*>(val))    return "None";
        return "<unknown>";
    }


    StringValue* bin_add(StringValue* l, StringValue* r, CollectedHeap* heap) {
        return heap->allocate<StringValue>(l->value + r->value);
    };

    StringValue* bin_add(StringValue* l, IntegerValue* r, CollectedHeap* heap) {
        return heap->allocate<StringValue>(str(l) + str(r));
    };

    StringValue* bin_add(IntegerValue* l, StringValue* r, CollectedHeap* heap) {
        return heap->allocate<StringValue>(str(l) + str(r));
    };

    IntegerValue* bin_add(IntegerValue* l, IntegerValue* r, CollectedHeap* heap) {
        return heap->allocate<IntegerValue>(l->value + r->value);
    };


    std::unordered_map<BinOp, std::function<Value*(Value*, Value*, CollectedHeap*)>, EnumHash> evaluate_lambdas = {
  // +
  {BinOp::ADD, [](Value* l, Value* r, CollectedHeap* heap) -> Value* {
      if (auto li = dynamic_cast<IntegerValue*>(l)) {
        if (auto ri = dynamic_cast<IntegerValue*>(r))
          return heap->allocate<IntegerValue>(li->value + ri->value);
      }

      // If either is string, concatenate str(lhs) + str(rhs)
      if (dynamic_cast<StringValue*>(l) || dynamic_cast<StringValue*>(r)) {
        return heap->allocate<StringValue>(str(l) + str(r));
      }
      cast_error("operator '+' expects integers or strings");
      return nullptr;
  }},

  // -
  {BinOp::SUB, [](Value* l, Value* r, CollectedHeap* heap) -> Value* {
      auto li = dynamic_cast<IntegerValue*>(l);
      auto ri = dynamic_cast<IntegerValue*>(r);
      if (!li || !ri) cast_error("operator '-' expects integers");
      return heap->allocate<IntegerValue>(li->value - ri->value);
  }},

  // *
  {BinOp::MUL, [](Value* l, Value* r, CollectedHeap* heap) -> Value* {
      auto li = dynamic_cast<IntegerValue*>(l);
      auto ri = dynamic_cast<IntegerValue*>(r);
      if (!li || !ri) cast_error("operator '*' expects integers");
      return heap->allocate<IntegerValue>(li->value * ri->value);
  }},

  // /
  {BinOp::DIV, [](Value* l, Value* r, CollectedHeap* heap) -> Value* {
      auto li = dynamic_cast<IntegerValue*>(l);
      auto ri = dynamic_cast<IntegerValue*>(r);
      if (!li || !ri) cast_error("operator '/' expects integers");
      if (ri->value == 0) arith_error("divide by zero");
      return heap->allocate<IntegerValue>(li->value / ri->value);
  }},

  // <
  {BinOp::LT, [](Value* l, Value* r, CollectedHeap* heap) -> Value* {
      (void)heap;
      auto li = dynamic_cast<IntegerValue*>(l);
      auto ri = dynamic_cast<IntegerValue*>(r);
      if (!li || !ri) cast_error("operator '<' expects integers");
      return BooleanValue::from(li->value < ri->value);
  }},

  // <=
  {BinOp::LTE, [](Value* l, Value* r, CollectedHeap* heap) -> Value* {
      (void)heap;
      auto li = dynamic_cast<IntegerValue*>(l);
      auto ri = dynamic_cast<IntegerValue*>(r);
      if (!li || !ri) cast_error("operator '<=' expects integers");
      return BooleanValue::from(li->value <= ri->value);
  }},

  // >
  {BinOp::GT, [](Value* l, Value* r, CollectedHeap* heap) -> Value* {
      (void)heap;
      auto li = dynamic_cast<IntegerValue*>(l);
      auto ri = dynamic_cast<IntegerValue*>(r);
      if (!li || !ri) cast_error("operator '>' expects integers");
      return BooleanValue::from(li->value > ri->value);
  }},

  // >=
  {BinOp::GTE, [](Value* l, Value* r, CollectedHeap* heap) -> Value* {
      (void)heap;
      auto li = dynamic_cast<IntegerValue*>(l);
      auto ri = dynamic_cast<IntegerValue*>(r);
      if (!li || !ri) cast_error("operator '>=' expects integers");
      return BooleanValue::from(li->value >= ri->value);
  }},

  // &
  {BinOp::AND, [](Value* l, Value* r, CollectedHeap* heap) -> Value* {
      (void)heap;
      auto lb = dynamic_cast<BooleanValue*>(l);
      auto rb = dynamic_cast<BooleanValue*>(r);
      if (!lb || !rb) cast_error("operator '&' expects booleans");
      return BooleanValue::from(lb->value() && rb->value());
  }},

  // |
  {BinOp::OR, [](Value* l, Value* r, CollectedHeap* heap) -> Value* {
      (void)heap;
      auto lb = dynamic_cast<BooleanValue*>(l);
      auto rb = dynamic_cast<BooleanValue*>(r);
      if (!lb || !rb) cast_error("operator '|' expects booleans");
      return BooleanValue::from(lb->value() || rb->value());
  }},

  // ==
  {BinOp::EQ, [](Value* l, Value* r, CollectedHeap* heap) -> Value* {
      (void)heap;
      // Cross-type equality is FALSE
      auto li = dynamic_cast<IntegerValue*>(l);
      auto ri = dynamic_cast<IntegerValue*>(r);
    if (li && ri) return BooleanValue::from(li->value == ri->value);

      auto ls = dynamic_cast<StringValue*>(l);
      auto rs = dynamic_cast<StringValue*>(r);
    if (ls && rs) return BooleanValue::from(ls->value == rs->value);

      auto lb = dynamic_cast<BooleanValue*>(l);
      auto rb = dynamic_cast<BooleanValue*>(r);
    if (lb && rb) return BooleanValue::from(lb->value() == rb->value());

      auto lrec = dynamic_cast<RecordValue*>(l);
      auto rrec = dynamic_cast<RecordValue*>(r);
    if (lrec && rrec) return BooleanValue::from(lrec == rrec); // address identity

      auto lf = dynamic_cast<FunctionValue*>(l);
      auto rf = dynamic_cast<FunctionValue*>(r);
      if (lf && rf) {
        // Minimal: pointer identity. (If you later implement closure/body equality, adjust here.)
    return BooleanValue::from(lf == rf);
      }

    if (auto lnf = dynamic_cast<NativeFunctionValue*>(l)) {
        if (auto rnf = dynamic_cast<NativeFunctionValue*>(r))
            return BooleanValue::from(lnf == rnf);
        }

      // None equals itself only
            if (dynamic_cast<NoneValue*>(l) && dynamic_cast<NoneValue*>(r))
                return BooleanValue::from(true);

      // Different types → false
      return BooleanValue::from(false);
  }},
};
};
