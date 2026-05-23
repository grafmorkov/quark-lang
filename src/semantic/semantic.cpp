#include "quark/semantic/semantic.h"
#include "quark/support/compiler_context.h"
#include "utils/logger.h"

#include <utility>
#include <variant>

using namespace utils::logger;

namespace quark::sm {

namespace {

template<class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

bool types_equal(const ast::Type* a, const ast::Type* b) {
    if (a == b) return true;
    if (!a || !b) return false;

    if (a->kind != b->kind) return false;

    if (a->kind == ast::TypeKind::Struct) {
        return a->struct_name == b->struct_name;
    }

    return true;
}

bool is_assignable(const ast::Type* to, const ast::Type* from) {
    return types_equal(to, from);
}

struct ScopeGuard {
    quark::symb_t::SymbolTable& symbols;

    explicit ScopeGuard(quark::symb_t::SymbolTable& s)
        : symbols(s) {
        symbols.enter_scope();
    }

    ~ScopeGuard() {
        symbols.exit_scope();
    }
};

struct NamespaceGuard {
    quark::symb_t::SymbolTable& symbols;

    explicit NamespaceGuard(quark::symb_t::SymbolTable& s, const std::string& name)
        : symbols(s) {
        symbols.enter_namespace(name);
    }

    ~NamespaceGuard() {
        symbols.exit_namespace();
    }
};

const ast::Type* symbol_type(const quark::symb_t::Symbol& sym) {
    if (const auto* v = std::get_if<quark::symb_t::VarSymbol>(&sym.data)) {
        return v->type;
    }
    if (const auto* a = std::get_if<quark::symb_t::FuncArgSymbol>(&sym.data)) {
        return a->type;
    }
    return nullptr;
}

bool symbol_is_mutable(const quark::symb_t::Symbol& sym) {
    if (const auto* v = std::get_if<quark::symb_t::VarSymbol>(&sym.data)) {
        return v->is_mut;
    }
    if (const auto* a = std::get_if<quark::symb_t::FuncArgSymbol>(&sym.data)) {
        return a->is_mut;
    }
    return false;
}

bool symbol_is_initialized(const quark::symb_t::Symbol& sym) {
    if (const auto* v = std::get_if<quark::symb_t::VarSymbol>(&sym.data)) {
        return v->is_initialized;
    }
    if (std::get_if<quark::symb_t::FuncArgSymbol>(&sym.data)) {
        return true;
    }
    if (std::get_if<quark::symb_t::StructSymbol>(&sym.data)) {
        return true;
    }
    return false;
}

void mark_symbol_initialized(quark::symb_t::Symbol& sym) {
    if (auto* v = std::get_if<quark::symb_t::VarSymbol>(&sym.data)) {
        v->is_initialized = true;
    }
}

const ast::Type* resolve_struct_field(
    CompilerContext& ctx,
    const ast::Type* base_type,
    const std::string& field_name
) {
    if (!base_type) return nullptr;

    if (base_type->kind != ast::TypeKind::Struct) {
        crash("Field access on non-struct type");
        return nullptr;
    }

    auto* sym = ctx.symbols.lookup(base_type->struct_name);
    if (!sym) {
        crash("Unknown struct: " + base_type->struct_name);
        return nullptr;
    }

    auto* ss = std::get_if<quark::symb_t::StructSymbol>(&sym->data);
    if (!ss) {
        crash("Invalid struct symbol: " + base_type->struct_name);
        return nullptr;
    }

    for (size_t i = 0; i < ss->field_names.size(); ++i) {
        if (ss->field_names[i] == field_name) {
            return ss->field_types[i];
        }
    }

    crash("Unknown field: " + field_name);
    return nullptr;
}
const ast::VarExpr* get_root_var(const ast::Expr* expr) {
    if (!expr) return nullptr;

    if (const auto* v = std::get_if<ast::VarExpr>(&expr->kind)) {
        return v;
    }

    if (const auto* f = std::get_if<ast::FieldExpr>(&expr->kind)) {
        return get_root_var(f->base);
    }

    return nullptr;
}
} // namespace

void SemanticAnalyzer::analyze(const std::vector<ast::Stmt*>& stmts) {
    for (const auto* stmt : stmts) {
        if (stmt) analyze_stmt(stmt);
    }
}

void SemanticAnalyzer::analyze_stmt(const ast::Stmt* stmt) {
    if (!stmt) return;

    std::visit(overloaded {
        [&](const ast::VarDecl& n) { analyze_var_decl(n); },
        [&](const ast::StructDecl& n) { analyze_struct_decl(n); },
        [&](const ast::NamespaceStmt& n) { analyze_namespace_stmt(n); },
        [&](const ast::ExprStmt& n) { analyze_expr_stmt(n); },
        [&](const ast::ReturnStmt& n) { analyze_return(n); },
        [&](const ast::FuncStmt& n) { analyze_func(n); },
        [&](const ast::IfStmt& n) { analyze_if(n); },
        [&](const ast::WhileStmt& n) { analyze_while(n); },
        [&](const ast::LoadStmt& n) {},
        [&](const auto&) {
            crash("Unsupported statement node in semantic analysis");
        }
    }, stmt->kind);
}

void SemanticAnalyzer::analyze_var_decl(const ast::VarDecl& var) {
    if (!var.type) {
        crash("Variable declaration missing type: " + var.name);
        return;
    }

    for (const auto& attr : var.attributes) {
        analyze_attribute(attr);
    }

    if (var.value) {
        const ast::Type* value_type = analyze_expr(var.value);
        if (!value_type) return;

        if (!is_assignable(var.type, value_type)) {
            crash("Type mismatch in variable initialization: " + var.name);
            return;
        }
    } else if (!var.is_mut) {
        crash("Immutable variable must be initialized: " + var.name);
        return;
    }

    if (!ctx.symbols.declare(var)) {
        crash("Variable already declared: " + var.name);
    }
}

void SemanticAnalyzer::analyze_struct_decl(const ast::StructDecl& str) {
    symb_t::StructSymbol sym;

    for (const auto& field : str.fields) {
        for (const auto& name : sym.field_names) {
            if (name == field.name) {
                crash("Duplicate field: " + field.name);
                return;
            }
        }

        if (!field.type) {
            crash("Field missing type: " + field.name);
            return;
        }

        if (field.default_value) {
            const ast::Type* dt = analyze_expr(field.default_value);
            if (!dt) return;

            if (!is_assignable(field.type, dt)) {
                crash("Type mismatch in field default value: " + field.name);
                return;
            }
        }

        for (const auto& attr : field.attributes) {
            analyze_attribute(attr);
        }

        sym.field_names.push_back(field.name);
        sym.field_types.push_back(field.type);
    }

    if (!ctx.symbols.declare_symbol(str.name, symb_t::Symbol{
        str.name,
        sym,
        {}
    })) {
        crash("Struct already declared: " + str.name);
    }
}

void SemanticAnalyzer::analyze_namespace_stmt(const ast::NamespaceStmt& stmt) {
    NamespaceGuard guard(ctx.symbols, stmt.name);

    if (stmt.body) {
        analyze_block(stmt.body);
    }
}

void SemanticAnalyzer::analyze_expr_stmt(const ast::ExprStmt& expr) {
    if (expr.expr) {
        analyze_expr(expr.expr);
    }
}

void SemanticAnalyzer::analyze_return(const ast::ReturnStmt& ret) {
    const ast::Type* value_type = ret.value
        ? analyze_expr(ret.value)
        : ctx.types.get_void();

    if (!current_function_return_type) {
        crash("Return outside function");
        return;
    }

    if (!value_type) return;

    if (!is_assignable(current_function_return_type, value_type)) {
        crash("Return type mismatch");
    }
}

void SemanticAnalyzer::analyze_func(const ast::FuncStmt& func) {
    const ast::Type* prev_return_type = current_function_return_type;
    current_function_return_type = func.return_type;

    ScopeGuard scope(ctx.symbols);

    for (const auto& arg : func.args) {
        if (!ctx.symbols.declare(arg)) {
            crash("Argument already declared: " + arg.name);
        }
    }

    if (func.body) {
        analyze_block(func.body);
    }

    current_function_return_type = prev_return_type;
}

void SemanticAnalyzer::analyze_if(const ast::IfStmt& stmt) {
    if (stmt.condition) {
        analyze_expr(stmt.condition);
    }

    if (stmt.then_block) {
        analyze_block(stmt.then_block);
    }

    if (stmt.else_block) {
        analyze_block(stmt.else_block);
    }
}

void SemanticAnalyzer::analyze_while(const ast::WhileStmt& stmt) {
    if (stmt.condition) {
        analyze_expr(stmt.condition);
    }

    if (stmt.body) {
        analyze_block(stmt.body);
    }
}

void SemanticAnalyzer::analyze_attribute(const ast::Attribute& attribute) {
    (void)attribute;
    // TODO
}

const ast::Type* SemanticAnalyzer::analyze_expr(const ast::Expr* expr) {
    if (!expr) return nullptr;

    return std::visit(overloaded {
        [&](const ast::IntExpr& n) -> const ast::Type* { return analyze_int(n); },
        [&](const ast::StringExpr& n) -> const ast::Type* { return analyze_string(n); },
        [&](const ast::VarExpr& n) -> const ast::Type* { return analyze_var(n); },
        [&](const ast::AssignExpr& n) -> const ast::Type* { return analyze_assign(n); },
        [&](const ast::BinaryExpr& n) -> const ast::Type* { return analyze_binary(n); },
        [&](const ast::CallExpr& n) -> const ast::Type* { return analyze_call(n); },
        [&](const ast::FieldExpr& n) -> const ast::Type* { return analyze_field(n); },
        [&](const ast::NamespaceExpr& n) -> const ast::Type* { return analyze_namespace(n); },
        [&](const auto&) -> const ast::Type* {
            crash("Unsupported expression node in semantic analysis");
            return nullptr;
        }
    }, expr->kind);
}

const ast::Type* SemanticAnalyzer::analyze_int(const ast::IntExpr&) {
    return ctx.types.get_int();
}

const ast::Type* SemanticAnalyzer::analyze_string(const ast::StringExpr&) {
    return ctx.types.get_string();
}

const ast::Type* SemanticAnalyzer::analyze_var(const ast::VarExpr& var) {
    auto* sym = ctx.symbols.lookup(var.name);

    if (!sym) {
        crash("Undefined variable: " + var.name);
        return nullptr;
    }

    const ast::Type* type = symbol_type(*sym);
    if (!type) {
        crash("Symbol is not a value: " + var.name);
        return nullptr;
    }

    if (!symbol_is_initialized(*sym)) {
        crash("Use of uninitialized variable: " + var.name);
        return nullptr;
    }

    return type;
}

const ast::Type* SemanticAnalyzer::resolve_lvalue(const ast::Expr* expr) {
    if (!expr) {
        crash("Invalid lvalue");
        return nullptr;
    }

    if (const auto* var = std::get_if<ast::VarExpr>(&expr->kind)) {
        auto* sym = ctx.symbols.lookup(var->name);
        if (!sym) {
            crash("Undefined variable: " + var->name);
            return nullptr;
        }

        if (!symbol_is_mutable(*sym)) {
            crash("Cannot assign to immutable variable: " + var->name);
            return nullptr;
        }

        return symbol_type(*sym);
    }

    if (const auto* field = std::get_if<ast::FieldExpr>(&expr->kind)) {
        const ast::Type* base_type = resolve_lvalue(field->base);
        if (!base_type) return nullptr;

        return resolve_struct_field(ctx, base_type, field->field);
    }

    crash("Invalid lvalue");
    return nullptr;
}

const ast::Type* SemanticAnalyzer::analyze_assign(const ast::AssignExpr& asg) {
    const ast::Type* value_type = analyze_expr(asg.value);
    if (!value_type) return nullptr;

    if (!asg.target) {
        crash("Assignment target is missing");
        return nullptr;
    }

    const ast::Type* target_type = resolve_lvalue(asg.target);
    if (!target_type) return nullptr;

    if (!is_assignable(target_type, value_type)) {
        crash("Type mismatch in assignment");
        return nullptr;
    }

    if (const auto* root = get_root_var(asg.target)) {
        if (auto* sym = ctx.symbols.lookup(root->name)) {
            mark_symbol_initialized(*sym);
        }
    }

    return target_type;
}

const ast::Type* SemanticAnalyzer::analyze_field(const ast::FieldExpr& node) {
    const ast::Type* base = analyze_expr(node.base);
    if (!base) return nullptr;

    return resolve_struct_field(ctx, base, node.field);
}

const ast::Type* SemanticAnalyzer::analyze_binary(const ast::BinaryExpr& b) {
    const ast::Type* l = analyze_expr(b.lhs);
    const ast::Type* r = analyze_expr(b.rhs);

    if (!l || !r) return nullptr;

    if (!types_equal(l, r)) {
        crash("Type mismatch in binary expression");
        return nullptr;
    }

    return l;
}

const ast::Type* SemanticAnalyzer::analyze_call(const ast::CallExpr&) {
    return nullptr; // TODO: function symbols
}

const ast::Type* SemanticAnalyzer::analyze_block(const ast::Block* block) {
    if (!block) return ctx.types.get_void();

    ScopeGuard scope(ctx.symbols);

    for (const auto& stmt : block->stmts) {
        if (stmt) {
            analyze_stmt(stmt);
        }
    }

    return ctx.types.get_void();
}
const ast::Type* SemanticAnalyzer::analyze_namespace(const ast::NamespaceExpr& n){
    auto* sym = ctx.symbols.lookup(n.ns);

    if (!sym) {
        crash("Undefined namespace: " + n.ns);
        return nullptr;
    }
    const auto* struct_sym = std::get_if<symb_t::StructSymbol>(&sym->data);

    if (!struct_sym) {
        crash(n.ns + " is not a namespace-like symbol");
        return nullptr;
    }

    for (size_t i = 0; i < struct_sym->field_names.size(); ++i) {
        if (struct_sym->field_names[i] == n.name) {
            return struct_sym->field_types[i];
        }
    }

    crash("Unknown symbol in namespace: " + n.ns + "::" + n.name);
    return nullptr;
}

} // namespace quark::sm