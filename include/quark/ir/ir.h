#pragma once

#include <string>
#include <variant>
#include <vector>
#include <memory>

#include "quark/frontend/ast.h"

namespace quark::codegen {

// VALUES
struct IRValue {
    std::string name;
    const quark::ast::Type* type; 
};

// OPS
enum class IRBinaryOp {
    Add,
    Sub,
    Mul,
    Div,
    Eq,      // ==
    NotEq,   // !=
    Lt,      // <
    Lte,     // <=
    Gt,      // >
    Gte,     // >=
};

// INST FORWARD DECL
struct IRBinary;
struct IRStore;
struct IRReturn;
struct IRBranch;
struct IRAlloc;
struct IRJump;
struct IRCall;
struct IRStruct;
struct IRGetField;
struct IRSetField;

// INST VARIANT
using IRInst = std::variant<
    IRBinary,
    IRStore,
    IRReturn,
    IRBranch,
    IRAlloc,
    IRJump,
    IRGetField,
    IRSetField,
    IRCall
>;

// BLOCK
struct IRBlock {
    std::string name;
    std::vector<IRInst> inst;
    bool terminated = false;

    void dump() const;
};
// INSTRUCTIONS
struct IRBinary {
    IRBinaryOp op;
    IRValue dst;
    IRValue lhs;
    IRValue rhs;
};

struct IRAlloc {
    std::string name;
    const quark::ast::Type* type;

    IRAlloc(std::string n, const quark::ast::Type* t) : name(n), type(t) {}
};

struct IRStore {
    IRValue target;
    IRValue value;
};

struct IRReturn {
    IRValue value;
};

struct IRBranch {
    IRValue cond;
    IRBlock* then_block;
    IRBlock* else_block;
};

struct IRJump {
    IRBlock* target;
};

struct IRCall {
    IRValue callee;
    std::vector<IRValue> args;
    IRValue dst;
};
struct IRStruct {
    std::string name;
    std::vector<std::string> field_names;
    std::vector<const ast::Type*> field_types;
};

struct IRGetField {
    IRValue dst;
    IRValue base;
    int index;
};

struct IRSetField {
    IRValue base;
    IRValue value;
    int index;
};

// ---- INSTRUCTION DUMP ----
void dump_instr(const IRBinary& i);
void dump_instr(const IRStore& i);
void dump_instr(const IRReturn& i);
void dump_instr(const IRBranch& i);
void dump_instr(const IRJump& i);
void dump_instr(const IRCall& i);
void dump_instr(const IRStruct& i);
} // namespace quark::codegen