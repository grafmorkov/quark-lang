#include "quark/codegen/ir_gen.h"
#include "utils/logger.h"
#include <utility>

using namespace utils::logger;
// IRBuilder 

namespace quark::codegen {

void IRBuilder::ensure_block() {
    if (!current_block)
        crash("No current IR block set");

    if (current_block->terminated)
        crash("Block already terminated: " + current_block->name);
}

IRValue IRBuilder::make_temp() {
    return IRValue{ "t" + std::to_string(temp_id++) };
}

IRBlock* IRBuilder::create_block(const std::string& name) {
    blocks.push_back(std::make_unique<IRBlock>());
    auto* block = blocks.back().get();
    block->name = name;
    return block;
}

void IRBuilder::set_insert_point(IRBlock* block) {
    current_block = block;
}

IRValue IRBuilder::create_const(int value) {
    return IRValue{ std::to_string(value) };
}

IRValue IRBuilder::create_binary(IRBinaryOp op, IRValue lhs, IRValue rhs) {
    ensure_block();

    IRValue res = make_temp();
    res.type = lhs.type;

    current_block->inst.push_back(IRBinary{
        op, res, lhs, rhs
    });

    return res;
}

void IRBuilder::create_alloc(const std::string& name, const quark::ast::Type* t) {
    ensure_block();

    if (variables.find(name) != variables.end()) {
        crash("The variable '" + name + "' has been already declared");
    }

    IRValue v = make_temp();
    v.type = t;
    variables[name] = v;

    current_block->inst.push_back(IRAlloc{v.name, t});
}
void IRBuilder::create_store(const std::string& name, IRValue value) {
    ensure_block();

    auto it = variables.find(name);
    if (it == variables.end()) {
        crash("Undefined variable: '" + name + "'" );
    }

    current_block->inst.push_back(IRStore{
        it->second,
        value
    });
}

void IRBuilder::create_return(IRValue value) {
    ensure_block();

    current_block->inst.push_back(IRReturn{ value });

    current_block->terminated = true;
    current_block = nullptr;
}

void IRBuilder::create_branch(IRValue cond, IRBlock* then_block, IRBlock* else_block) {
    ensure_block();

    current_block->inst.push_back(IRBranch{
        cond, then_block, else_block
    });

    current_block->terminated = true;
    current_block = nullptr;
}

void IRBuilder::create_jump(IRBlock* target) {
    ensure_block();

    current_block->inst.push_back(IRJump{ target });

    current_block->terminated = true;
    current_block = nullptr;
}

// IRGenerator 
void IRGenerator::gen_program(const std::vector<std::unique_ptr<Stmt>>& program) {
    auto* entry = builder.create_block("entry");
    builder.set_insert_point(entry);
    
    for (const auto& stmt : program) {
        if (std::holds_alternative<FuncStmt>(stmt->kind)) {
            gen_function(std::get<FuncStmt>(stmt->kind));
        } else {
            gen_stmt(*stmt);
        }
    }
}

IRValue IRGenerator::gen_expr(const Expr& expr) {
    return std::visit([this](auto& node) {
        return gen_node(node);
    }, expr.kind);
}

void IRGenerator::gen_stmt(const Stmt& stmt) {
    std::visit([this](auto& node) {
        gen_stmt_node(node);
    }, stmt.kind);
}

void IRGenerator::gen_function(const FuncStmt& func) {
    for (auto& stmt : func.body->statements)
        gen_stmt(*stmt);

    if (builder.current_block && !builder.current_block->terminated) {
        if (func.return_t->kind == Type::Void) {
            builder.create_return(builder.create_const(0));
        } else {
            crash("Missing return in non-void function");
        }
    }
}

// EXPRESSIONS
template<typename T>
IRValue IRGenerator::gen_node(const T&) {
    return IRValue{"unhandled"}; // TODO
}
template<typename T>
void IRGenerator::gen_stmt_node(const T&) {
    // TODO
}
IRValue IRGenerator::gen_node(const IntLit& node) {
    return builder.create_const(node.value);
}

IRValue IRGenerator::gen_node(const BinaryExpr& node) {
    return builder.create_binary(
        map_op(node.op),
        gen_expr(*node.lhs),
        gen_expr(*node.rhs)
    );
}

IRValue IRGenerator::gen_node(const VarExpr& node) {
    auto it = builder.variables.find(node.name);

    if (it == builder.variables.end())
        crash("Undefined variable: " + node.name);
    return it->second;
}
IRValue IRGenerator::gen_node(const FieldAccessExpr& node) {
    IRValue base = gen_expr(*node.base);

    if (!base.type || base.type->kind != Type::Struct) {
        crash("Field access base must be a struct");
    }

    auto it = builder.struct_layouts.find(base.type->struct_name);
    if (it == builder.struct_layouts.end()) {
        crash("Unknown struct layout: " + base.type->struct_name);
    }

    const auto& layout = it->second;
    auto f = layout.field_index.find(node.field);
    if (f == layout.field_index.end()) {
        crash("Unknown field: " + node.field);
    }

    IRValue dst = builder.make_temp();
    dst.type = layout.field_types[f->second];

    builder.current_block->inst.push_back(IRGetField{
        dst,
        base,
        f->second
    });

    return dst;
}
IRValue IRGenerator::gen_node(const CallExpr& node) {
    std::vector<IRValue> args;
    for (const auto& arg : node.args) {
        args.push_back(gen_expr(*arg));
    }

    IRValue callee = gen_expr(*node.callee);
    IRValue dst = builder.make_temp();

    builder.current_block->inst.push_back(IRCall{
        callee, args, dst
    });

    return dst;
}

IRValue IRGenerator::gen_node(const AssignExpr& node) {
    IRValue value = gen_expr(*node.value);

    if (auto* var = std::get_if<VarExpr>(&node.target->kind)) {
        builder.create_store(var->name, value);
        return value;
    }

    if (auto* field = std::get_if<FieldAccessExpr>(&node.target->kind)) {
        IRValue base = gen_expr(*field->base);

        if (!base.type || base.type->kind != Type::Struct) {
            crash("Field assignment base must be a struct");
        }

        auto it = builder.struct_layouts.find(base.type->struct_name);
        if (it == builder.struct_layouts.end()) {
            crash("Unknown struct layout: " + base.type->struct_name);
        }

        const auto& layout = it->second;
        auto f = layout.field_index.find(field->field);
        if (f == layout.field_index.end()) {
            crash("Unknown field: " + field->field);
        }

        builder.current_block->inst.push_back(IRSetField{
            base,
            value,
            f->second
        });

        return value;
    }

    crash("Assignment target must be variable or field access");
}

//  STATEMENTS

void IRGenerator::gen_stmt_node(const ExprStmt& node) {
    gen_expr(*node.expr);
}

void IRGenerator::gen_stmt_node(const VarDecl& node) {
    builder.create_alloc(node.name, node.type);

    if (node.value) {
        builder.create_store(node.name, gen_expr(*node.value));
    }
}

void IRGenerator::gen_stmt_node(const ReturnStmt& node) {
    if (node.value)
        builder.create_return(gen_expr(*node.value));
}

void IRGenerator::gen_stmt_node(const IfStmt& node) {
    auto cond = gen_expr(*node.condition);

    auto* then_block = builder.create_block("then");
    auto* end_block  = builder.create_block("end");

    IRBlock* else_block = nullptr;

    if (node.elseBranch) {
        else_block = builder.create_block("else");
        builder.create_branch(cond, then_block, else_block);
    } else {
        builder.create_branch(cond, then_block, end_block);
    }

    // THEN
    builder.set_insert_point(then_block);
    for (auto& s : node.thenBranch->statements)
        gen_stmt(*s);

    if (!then_block->terminated) {
        builder.create_jump(end_block);
    }

    // ELSE
    if (node.elseBranch) {
        builder.set_insert_point(else_block);
        for (auto& s : node.elseBranch->statements)
            gen_stmt(*s);

        if (!else_block->terminated) {
            builder.create_jump(end_block);
        }
    }

    builder.set_insert_point(end_block);
}
void IRGenerator::gen_stmt_node(const WhileStmt& node) {
    auto* cond_block = builder.create_block("while_cond");
    auto* body_block = builder.create_block("while_body");
    auto* end_block  = builder.create_block("while_end");
    
    builder.create_jump(cond_block);
    
    builder.set_insert_point(cond_block);
    auto cond = gen_expr(*node.condition);
    builder.create_branch(cond, body_block, end_block);
    
    builder.set_insert_point(body_block);
    for (auto& s : node.body->statements)
        gen_stmt(*s);
    builder.create_jump(cond_block);
    
    builder.set_insert_point(end_block);
}
void IRGenerator::gen_stmt_node(const StructDecl& node) {
    StructLayout layout;

    for (int i = 0; i < node.fields.size(); i++) {
        const auto& field = node.fields[i];

        layout.field_index[field->name] = i;
        layout.field_types.push_back(field->type);
    }

    builder.struct_layouts[node.name] = std::move(layout);
}

// OPS 

IRBinaryOp IRGenerator::map_op(BinaryOp op) {
    switch (op) {
        case BinaryOp::Add:   return IRBinaryOp::Add;
        case BinaryOp::Sub:   return IRBinaryOp::Sub;
        case BinaryOp::Mul:   return IRBinaryOp::Mul;
        case BinaryOp::Div:   return IRBinaryOp::Div;
        case BinaryOp::Eq:    return IRBinaryOp::Eq;   
        case BinaryOp::NotEq: return IRBinaryOp::NotEq; 
        case BinaryOp::Lt:    return IRBinaryOp::Lt;    
        case BinaryOp::Lte:   return IRBinaryOp::Lte;   
        case BinaryOp::Gt:    return IRBinaryOp::Gt;    
        case BinaryOp::Gte:   return IRBinaryOp::Gte;   
        default: crash("Unsupported binary op");
    }
}
}