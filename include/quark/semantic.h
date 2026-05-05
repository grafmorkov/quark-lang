#pragma once

#include <iostream>
#include <vector>
#include <unordered_map>

#include "symbol_table.h"
#include "compiler_context.h"

namespace quark::sm{
    class SemanticAnalyzer {
        public:
            void analyze(const std::vector<std::unique_ptr<ast::Stmt>>& stmts);
            SemanticAnalyzer(CompilerContext& _ctx): ctx(_ctx) {}

        private:
            CompilerContext& ctx;

            const ast::Type* analyze_expr(const ast::Expr* expr);
            void analyze_stmt(const ast::Stmt* stmt);

            bool is_lvalue(ast::Expr* expr);
            bool is_symbol_mutable(const ast::Expr* expr);
            const ast::Type* resolve_lvalue(const ast::Expr* expr);

            const ast::Type* current_function_return_type = nullptr;

            void analyze_stmt_node(const ast::Decl& decl);
            void analyze_stmt_node(const ast::ExprStmt& expr);
            void analyze_stmt_node(const ast::ReturnStmt& ret);
            void analyze_stmt_node(const ast::FuncStmt& func);
            void analyze_stmt_node(const ast::IfStmt& stmt);
            void analyze_stmt_node(const ast::WhileStmt& stmt);
            
            void analyze_stmt_node(const ast::VarDecl& var);
            void analyze_stmt_node(const ast::StructDecl& str);
            void analyze_stmt_node(const ast::FieldDecl& field);
            void analyze_attribute(const ast::Attribute& attribute);

            const ast::Type* analyze_expr_node(const ast::IntLit&);
            const ast::Type* analyze_expr_node(const ast::StringLit&);
            const ast::Type* analyze_expr_node(const ast::VarExpr& var);
            const ast::Type* analyze_expr_node(const ast::AssignExpr& asg);
            const ast::Type* analyze_expr_node(const ast::NoneExpr&);
            const ast::Type* analyze_expr_node(const ast::BlockExpr& block);
            const ast::Type* analyze_expr_node(const ast::BinaryExpr& b);
            const ast::Type* analyze_expr_node(const ast::CallExpr& call);
            const ast::Type* analyze_expr_node(const ast::FieldAccessExpr& node);
            
            const ast::Type* analyze_block(const ast::BlockExpr* block);
        };
}
