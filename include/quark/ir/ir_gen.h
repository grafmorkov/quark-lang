#pragma once

#include "ir.h"
#include "quark/frontend/ast.h"
#include "quark/support/compiler_context.h"
#include "quark/modules/module.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <string>
#include <span>

namespace quark::codegen {

struct IRGenerator {
    CompilerContext& ctx;

    IRProgram program;

    IRFunction* current_func = nullptr;
    const ast::Type* current_func_return_type = nullptr;
    const modules::Module* current_module = nullptr;

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

    // local variable attributes (for runtime attrs)
    std::unordered_map<std::string, std::vector<ast::Attribute>> local_var_attrs;

    // namespace nesting
    std::vector<std::string> namespace_stack;

    struct RegionInfo {
        Local data_local;
        Local offset_local;
        Local cap_local;
    };
    std::vector<RegionInfo> region_stack;

    explicit IRGenerator(CompilerContext& c);

    // Entry

    void gen_program(std::span<quark::modules::Module* const> modules);
    void gen_module(const quark::modules::Module& mod);
    // Functions

    void gen_function(const ast::FuncStmt& fn);

    // For runtime attributes. (Now there is no runtime attributes)
    void emit_attr_lowering(const std::string& var_name);
    void emit_attr_lowering(const std::string& var_name, const std::vector<ast::Attribute>& attrs);

    // Statements

    void gen_stmt(const ast::Stmt& stmt);
    void gen_block(const ast::Block& block);
    void gen_region(const ast::RegionStmt& reg);

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
