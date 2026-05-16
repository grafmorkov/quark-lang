#pragma once

#include <cstdint>
#include <vector>
#include <variant>
#include <string>

namespace quark::codegen {

using Reg = uint32_t;
using Label = uint32_t;

enum class IRBinaryOp {
    Add, Sub, Mul, Div,
    Eq, NotEq,
    Lt, Lte, Gt, Gte
};

struct IRConst {
    Reg dst;
    int64_t value;
};

struct IRBinary {
    IRBinaryOp op;
    Reg dst;
    Reg lhs;
    Reg rhs;
};

struct IRAssign {
    Reg dst;
    Reg src;
};

struct IRCall {
    Reg dst;
    uint32_t func_id;
    std::vector<Reg> args;
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
    int index;
};

struct IRSetField {
    Reg base;
    Reg value;
    int index;
};

using IRInst = std::variant<
    IRConst,
    IRBinary,
    IRAssign,
    IRCall,
    IRReturn,
    IRJump,
    IRBranch,
    IRLabel,
    IRGetField,
    IRSetField
>;

struct IRFunction {
    uint32_t id;
    std::string name;
    uint32_t arg_count = 0;
    std::vector<IRInst> body;
};

struct IRProgram {
    std::vector<IRFunction> functions;

    void dump() const;
};

} // namespace quark::codegen