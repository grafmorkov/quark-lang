#pragma once

#include "ir.h"
#include "quark/frontend/ast.h"
#include "quark/support/compiler_context.h"

#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>

namespace quark::codegen {

struct IRGenerator {
    CompilerContext& ctx;

    IRProgram program;

    IRFunction* current_func = nullptr;

    Reg next_reg = 0;
    Label next_label = 0;

    uint32_t next_func_id = 0;

    // local variable -> register slot
    std::unordered_map<std::string, Reg> locals;

    // function name -> id
    std::unordered_map<std::string, uint32_t> function_ids;

    explicit IRGenerator(CompilerContext& c)
        : ctx(c) {}

    // Entry

    void gen_program(const std::vector<ast::Stmt*>& stmts);

    // Functions

    void gen_function(const ast::FuncStmt& fn);

    // Stmts

    void gen_stmt(const ast::Stmt& stmt);
    void gen_block(const ast::Block& block);

    // Exprs

    Reg gen_expr(const ast::Expr& expr);

private:

    // Helpers

    Reg new_reg();
    Label new_label();

    void emit(const IRInst& inst);

    IRBinaryOp map_op(ast::BinaryOp op);

    uint32_t resolve_function_id(const std::string& name);

    std::unordered_map<std::string, uint32_t> function_ids;
    std::vector<std::unordered_map<std::string, uint32_t>> local_scopes;
    std::vector<std::unordered_map<std::string, const ast::Type*>> type_scopes;
    std::vector<std::string> namespace_stack;
    bool current_terminated = false;

    // Expressions

    Reg gen_int(const ast::IntExpr& n);
    Reg gen_string(const ast::StringExpr& n);
    Reg gen_var(const ast::VarExpr& n);
    Reg gen_binary(const ast::BinaryExpr& n);
    Reg gen_assign(const ast::AssignExpr& n);
    Reg gen_call(const ast::CallExpr& n);
    Reg gen_field(const ast::FieldExpr& n);

    // Statements

    void gen_expr_stmt(const ast::ExprStmt& n);
    void gen_var_decl(const ast::VarDecl& n);
    void gen_return(const ast::ReturnStmt& n);
    void gen_if(const ast::IfStmt& n);
    void gen_while(const ast::WhileStmt& n);
    void gen_struct(const ast::StructDecl& n);
    void gen_namespace(const ast::NamespaceStmt& n);
};

} // namespace quark::codegen