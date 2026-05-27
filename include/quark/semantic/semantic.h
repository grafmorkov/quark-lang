#pragma once

#include <vector>

#include "quark/support/compiler_context.h"

namespace quark::sm {

class SemanticAnalyzer {
    public:
        explicit SemanticAnalyzer(CompilerContext& ctx,
                  std::vector<std::string> ns)
    : ctx(ctx), module_namespace(std::move(ns)) {}

        void analyze(const std::vector<ast::Stmt*>& stmts);

    private:

        CompilerContext& ctx;
        const ast::Type* current_function_return_type = nullptr;
        std::vector<std::string> module_namespace;
        bool is_in_region = false;

        void analyze_stmt(const ast::Stmt* stmt);
        const ast::Type* analyze_expr(ast::Expr* expr);
        const ast::Type* analyze_block(const ast::Block* block);
        const ast::Type* resolve_lvalue(const ast::Expr* expr);
        void collect_declarations(const std::vector<ast::Stmt*>& stmts);

        void analyze_var_decl(const ast::VarDecl& var);
        void analyze_struct_decl(const ast::StructDecl& str);
        void analyze_namespace_stmt(const ast::NamespaceStmt& stmt);
        void analyze_expr_stmt(const ast::ExprStmt& expr);
        void analyze_return(const ast::ReturnStmt& ret);
        void analyze_func(const ast::FuncStmt& func);
        void analyze_if(const ast::IfStmt& stmt);
        void analyze_while(const ast::WhileStmt& stmt);
        void analyze_region(const ast::RegionStmt& reg);
        void analyze_attribute(const ast::Attribute& attribute);

        const ast::Type* analyze_int(const ast::IntExpr&);
        const ast::Type* analyze_string(const ast::StringExpr&);
        const ast::Type* analyze_var(const ast::VarExpr&);
        const ast::Type* analyze_assign(const ast::AssignExpr&);
        const ast::Type* analyze_binary(const ast::BinaryExpr&);
        const ast::Type* analyze_call(const ast::CallExpr&);
        const ast::Type* analyze_field(const ast::FieldExpr&);
        const ast::Type* analyze_namespace(const ast::NamespaceExpr&);
        const ast::Type* analyze_cast(const ast::CastExpr&);
        const ast::Type* analyze_index(const ast::IndexExpr&);
    };

} // namespace quark::sm