#pragma once

#include <optional>
#include <unordered_map>
#include <vector>

#include "quanta/frontend/ast.h"
#include "quanta/support/compiler_context.h"
#include "quanta/attributes/attributes.h"
#include "quanta/modules/module.h"

namespace quanta::sm {

class SemanticAnalyzer {
    public:
        explicit SemanticAnalyzer(CompilerContext& ctx,
                  std::vector<std::string> ns)
    : ctx(ctx), module_namespace(std::move(ns)) {}

        void analyze(const std::vector<ast::Stmt*>& stmts, modules::Module* mod = nullptr);

    private:

        CompilerContext& ctx;
        const ast::Type* current_function_return_type = nullptr;
        std::vector<std::string> module_namespace;
        std::vector<std::string> namespace_path;
        modules::Module* current_module = nullptr;
        bool is_in_region = false;
        int loop_depth = 0;           // nesting depth of while loops
        int break_depth = 0;          // nesting depth of loops and switches ('break' targets)
        const std::unordered_map<std::string, const ast::Type*>* current_type_subst = nullptr;

        void analyze_stmt(ast::Stmt* stmt);
        const ast::Type* analyze_expr(ast::Expr* expr);
        const ast::Type* analyze_block(const ast::Block* block);
        const ast::Type* resolve_lvalue(const ast::Expr* expr);
        void collect_declarations(const std::vector<ast::Stmt*>& stmts);

        // Canonicalize unqualified struct type names to their module-qualified
        // form so cross-module references (qualified or via `using`) match the
        // types used inside the defining module (e.g. cmp::ordering).
        const ast::Type* canonicalize_struct_type(const ast::Type* type);

        void analyze_var_decl(const ast::VarDecl& var);
        void analyze_struct_decl(const ast::StructDecl& str);
        void analyze_enum_decl(const ast::EnumDecl& enm);
        void analyze_namespace_stmt(const ast::NamespaceStmt& stmt);
        void analyze_expr_stmt(const ast::ExprStmt& expr);
        void analyze_return(const ast::ReturnStmt& ret);
        void analyze_break(const ast::BreakStmt& br);
        void analyze_continue(const ast::ContinueStmt& cont);
        void analyze_func(const ast::FuncStmt& func);
        void analyze_if(const ast::IfStmt& stmt);
        void analyze_else_if(const ast::ElseIfStmt& stmt);
        void analyze_while(const ast::WhileStmt& stmt);
        void analyze_switch(ast::SwitchStmt& stmt);
        void analyze_region(const ast::RegionStmt& reg);
        void analyze_using(const ast::UsingStmt& us);
        void analyze_attribute(const ast::Attribute& attribute, const attrs::AttributeTarget target);
        void check_visibility(const symb_t::Symbol& sym, const std::string& context);
        void check_arg_guard(const ast::Expr* arg, const std::string& call_name);

        const ast::Type* analyze_int(const ast::IntExpr&);
        const ast::Type* analyze_string(const ast::StringExpr&);
        const ast::Type* analyze_var(const ast::VarExpr&, const ast::Expr*);
        const ast::Type* analyze_assign(const ast::AssignExpr&);
        const ast::Type* analyze_binary(const ast::BinaryExpr&, const ast::Expr* expr);
        const ast::Type* analyze_unary(const ast::UnaryExpr&);
        const ast::Type* analyze_call(const ast::CallExpr&);
        const ast::Type* analyze_field(const ast::FieldExpr&);
        const ast::Type* analyze_namespace(const ast::NamespaceExpr&);
        const ast::Type* analyze_cast(const ast::CastExpr&);
        const ast::Type* analyze_index(const ast::IndexExpr&);
        const ast::Type* analyze_struct_init(const ast::StructInitExpr&);
        const ast::Type* analyze_sizeof(const ast::SizeofExpr&);
        std::optional<int64_t> try_eval_const(const ast::Expr* expr);
        std::optional<int64_t> eval_guard_cond(const ast::Expr* expr, const std::string& struct_name);
    };

} // namespace quanta::sm
