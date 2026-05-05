#pragma once

#include "ir.h"
#include <stdexcept>
#include <vector>
#include <memory>
#include <unordered_map>

#include "quark/ast.h"

using namespace quark::ast;

namespace quark::codegen {

// IRBuilder 

struct IRBuilder {
    int temp_id = 0;

    std::vector<std::unique_ptr<IRBlock>> blocks;
    std::unordered_map<std::string, IRValue> variables;
    std::unordered_map<std::string, StructLayout> struct_layouts;
    std::vector<std::unique_ptr<IRStruct>> strs;
    IRBlock* current_block = nullptr;

    void ensure_block();
    IRValue make_temp();

    IRBlock* create_block(const std::string& name);
    void set_insert_point(IRBlock* block);

    IRValue create_const(int value);

    IRValue create_binary(IRBinaryOp op, IRValue lhs, IRValue rhs);
    void create_store(const std::string& name, IRValue value);
    void create_alloc(const std::string& name, const quark::ast::Type* t);
    void create_return(IRValue value);
    void create_branch(IRValue cond, IRBlock* then_block, IRBlock* else_block);
    void create_jump(IRBlock* target);
    void dump() const;
};

// IRGenerator

struct IRGenerator {
    IRBuilder builder;

    // ENTRY POINT
    void gen_program(const std::vector<std::unique_ptr<Stmt>>& program);

    // TOP LEVEL
    void gen_stmt(const Stmt& stmt);
    void gen_function(const FuncStmt& func);

    // EXPRESSIONS
    IRValue gen_expr(const Expr& expr);
    
private:
    // EXPRESSIONS 

    IRValue gen_node(const IntLit& node);
    IRValue gen_node(const BinaryExpr& node);
    IRValue gen_node(const VarExpr& node);
    IRValue gen_node(const AssignExpr& node);
    IRValue gen_node(const CallExpr& node);
    IRValue gen_node(const FieldAccessExpr& node);

    // STATEMENTS

    void gen_stmt_node(const ExprStmt& node);
    void gen_stmt_node(const VarDecl& node);
    void gen_stmt_node(const ReturnStmt& node);
    void gen_stmt_node(const IfStmt& node);
    void gen_stmt_node(const WhileStmt& node);
    void gen_stmt_node(const StructDecl& node);

    // FALLBACKS

    template<typename T>
    IRValue gen_node(const T&);

    template<typename T>
    void gen_stmt_node(const T&);

    // HELPERS 

    IRBinaryOp map_op(BinaryOp op);
};

} // namespace quark::codegen