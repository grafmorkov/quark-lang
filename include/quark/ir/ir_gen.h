#pragma once

#include "ir.h"
#include "quark/frontend/ast.h"
#include "quark/support/compiler_context.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace quark::codegen {

struct IRGenerator {
    CompilerContext& ctx;

    IRProgram program;

    IRFunction* current_func = nullptr;

    Reg next_reg = 0;
    Local next_local = 0;
    Label next_label = 0;

    bool current_terminated = false;

    // function name -> id
    std::unordered_map<std::string, uint32_t> function_ids;

    // variable scopes
    std::vector<std::unordered_map<std::string, Local>> local_scopes;

    // variable type scopes
    std::vector<std::unordered_map<std::string, const ast::Type*>> type_scopes;

    // namespace nesting
    std::vector<std::string> namespace_stack;

    explicit IRGenerator(CompilerContext& c);

    // Entry

    void gen_program(const std::vector<ast::Stmt*>& stmts);

    // Functions

    void gen_function(const ast::FuncStmt& fn);

    // Statements

    void gen_stmt(const ast::Stmt& stmt);
    void gen_block(const ast::Block& block);

    // Expressions

    Reg gen_expr(const ast::Expr& expr);

private:

    // Helpers

    Reg new_reg();
    Local new_local();
    Label new_label();

    void emit(const IRInst& inst);

    IRBinaryOp map_op(ast::BinaryOp op);
};

} // namespace quark::codegen