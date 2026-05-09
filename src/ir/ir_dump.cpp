#include <iostream>
#include "quark/codegen/ir.h"
#include "quark/codegen/ir_gen.h"

namespace quark::codegen {

namespace {

std::string op_to_string(IRBinaryOp op) {
    switch (op) {
        case IRBinaryOp::Add: return "add";
        case IRBinaryOp::Sub: return "sub";
        case IRBinaryOp::Mul: return "mul";
        case IRBinaryOp::Div: return "div";
        case IRBinaryOp::Eq: return "eq";
        case IRBinaryOp::NotEq: return "neq";
        case IRBinaryOp::Lt: return "lt";
        case IRBinaryOp::Lte: return "lte";
        case IRBinaryOp::Gt: return "gt";
        case IRBinaryOp::Gte: return "gte";
    }
    return "unknown";
}

std::string type_to_string(const ast::Type* t) {
    if (!t) return "<?>";

    switch (t->kind) {
        case ast::Type::Int: return "int";
        case ast::Type::Float: return "float";
        case ast::Type::String: return "string";
        case ast::Type::Void: return "void";
        case ast::Type::Struct: return "struct " + t->struct_name;
    }
    return "<?>"; 
}

std::string v(const IRValue& v) {
    return v.name + ":" + type_to_string(v.type);
}

void indent(int n) {
    for (int i = 0; i < n; i++) std::cout << "  ";
}

} // namespace

// IR Builder

void IRBuilder::dump() const {
    for (const auto& block : blocks) {
        block->dump();
    }
}

// IRBlock

void IRBlock::dump() const {
    std::cout << name << ":\n";

    for (const auto& inst : this->inst) {
        std::visit([](auto& node) {
            dump_instr(node);
        }, inst);
    }

    if (terminated) {
        std::cout << "  ; terminated\n";
    }
}

// Inst

void dump_instr(const IRBinary& i) {
    std::cout << "  " << v(i.dst)
              << " = " << op_to_string(i.op)
              << " " << v(i.lhs)
              << ", " << v(i.rhs)
              << "\n";
}

void dump_instr(const IRAlloc& i) {
    std::cout << "  " << i.name
              << " = alloc "
              << type_to_string(i.type)
              << "\n";
}

void dump_instr(const IRStore& i) {
    std::cout << "  store "
              << v(i.value)
              << " -> "
              << v(i.target)
              << "\n";
}

void dump_instr(const IRReturn& i) {
    std::cout << "  ret " << v(i.value) << "\n";
}

void dump_instr(const IRBranch& i) {
    std::cout << "  br "
              << v(i.cond)
              << " ? "
              << i.then_block->name
              << " : "
              << i.else_block->name
              << "\n";
}

void dump_instr(const IRJump& i) {
    std::cout << "  jmp " << i.target->name << "\n";
}

void dump_instr(const IRCall& i) {
    if (!i.dst.name.empty()) {
        std::cout << "  " << v(i.dst) << " = ";
    } else {
        std::cout << "  ";
    }

    std::cout << "call " << v(i.callee) << "(";

    for (size_t j = 0; j < i.args.size(); ++j) {
        if (j) std::cout << ", ";
        std::cout << v(i.args[j]);
    }

    std::cout << ")\n";
}

// Struct
// I Removed it, cause it should not be in the ir instructions(maybe i'll add this later)
// void dump_instr(const IRStruct& i) {
//     std::cout << "  struct " << i.name << " {\n";

//     for (size_t j = 0; j < i.field_names.size(); ++j) {
//         std::cout << "    "
//                   << i.field_names[j]
//                   << ": "
//                   << type_to_string(i.field_types[j])
//                   << "\n";
//     }

//     std::cout << "  }\n";
// }

// Fields ops

void dump_instr(const IRGetField& i) {
    std::cout << "  " << v(i.dst)
              << " = getfield "
              << v(i.base)
              << " . #" << i.index
              << "\n";
}

void dump_instr(const IRSetField& i) {
    std::cout << "  setfield "
              << v(i.base)
              << " . #" << i.index
              << " = "
              << v(i.value)
              << "\n";
}

} // namespace quark::codegen