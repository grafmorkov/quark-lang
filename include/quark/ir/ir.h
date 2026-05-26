#pragma once

#include <cstdint>
#include <vector>
#include <variant>
#include <string>

#include "quark/frontend/ast.h"

namespace quark::codegen {

using Reg = uint32_t;
using Local = uint32_t;
using Label = uint32_t;

enum class IRBinaryOp {
    Add, Sub, Mul, Div,
    Eq, NotEq,
    Lt, Lte, Gt, Gte
};

struct IRLoadConst {
    Reg dst;
    int64_t value;
};
struct IRLoadFloatConst {
    Reg dst;
    double value;
};
struct IRLoadString {
    Reg dst;
    uint32_t string_id;
};

struct IRLoadLocal {
    Reg dst;
    Local local;
};

struct IRStoreLocal {
    Local local;
    Reg src;
};

struct IRBinary {
    IRBinaryOp op;
    Reg dst;
    Reg lhs;
    Reg rhs;
    ast::TypeKind type_kind = ast::TypeKind::I32;
};

struct IRCall {
    Reg dst;
    uint32_t func_id;
    std::vector<Reg> args;
};
struct IRCast {
    Reg dst;
    Reg src;
    ast::TypeKind src_kind;
    ast::TypeKind target_kind;
    ast::CastKind kind;
};

struct IRReturn {
    Reg value;
};

struct IRJump {
    Label target;
};

struct IRBranch {
    Reg cond;
    Label then_label;
    Label else_label;
};

struct IRLabel {
    Label id;
};

struct IRGetField {
    Reg dst;
    Reg base;
    uint32_t offset;
};

struct IRSetField {
    Reg base;
    Reg value;
    uint32_t offset;
};
struct IRString {
    Label id;
    std::string value;
};

using IRInst = std::variant<
    IRLoadConst,
    IRLoadFloatConst,
    IRLoadLocal,
    IRStoreLocal,
    IRBinary,
    IRCall,
    IRReturn,
    IRJump,
    IRBranch,
    IRLabel,
    IRGetField,
    IRSetField,
    IRCast,
    IRLoadString
>;

struct IRFunction {
    uint32_t id;
    std::string name;

    uint32_t arg_count = 0;
    uint32_t local_count = 0;
    uint32_t temp_count = 0;

    std::vector<IRInst> body;

    bool is_extern;
    bool is_entry;
};

struct IRProgram {
    std::vector<IRFunction> functions;
    std::vector<IRString> strings;

    void dump() const;
};

} // namespace quark::codegen