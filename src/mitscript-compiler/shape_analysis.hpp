#pragma once

#include "cfg.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace mitscript::analysis {

enum class ShapeKind { Unknown, ShapeId, Top };

struct ShapeInfo {
    ShapeKind kind = ShapeKind::Unknown;
    int shape_id = -1;

    static ShapeInfo unknown() { return {ShapeKind::Unknown, -1}; }
    static ShapeInfo top() { return {ShapeKind::Top, -1}; }
    static ShapeInfo shape(int id) { return {ShapeKind::ShapeId, id}; }

    bool is_monomorphic() const { return kind == ShapeKind::ShapeId; }
    bool operator==(const ShapeInfo& o) const { return kind == o.kind && shape_id == o.shape_id; }
    bool operator!=(const ShapeInfo& o) const { return !(*this == o); }
};

struct Shape {
    int id = -1;
    std::vector<std::string> fields;
};

class ShapeRegistry {
public:
    int intern_shape(const std::vector<std::string>& fields);
    const Shape* lookup(int id) const;
    int slot_index(int shape_id, const std::string& field) const; // returns -1 if not found

private:
    std::vector<Shape> shapes_;
    std::unordered_map<std::string, int> key_to_id_;
};

struct ShapeState {
    std::vector<ShapeInfo> vregs;
    std::vector<ShapeInfo> locals;
};

struct ShapeAnalysisResult {
    std::vector<ShapeState> in_states;
    std::vector<ShapeState> out_states;
    ShapeRegistry registry;
    size_t num_vregs = 0;
    size_t num_locals = 0;
};

// Runs intraprocedural shape analysis on a single function.
ShapeAnalysisResult run_shape_analysis(mitscript::CFG::FunctionCFG& fn);

// Query helpers
ShapeInfo get_shape_at_block_out(const ShapeAnalysisResult& res, mitscript::CFG::BlockId bid, int vreg);
bool is_field_access_monomorphic(const ShapeAnalysisResult& res, mitscript::CFG::BlockId bid, int vreg);
int get_slot_index(const ShapeAnalysisResult& res, int shape_id, const std::string& field_name);

} // namespace mitscript::analysis
