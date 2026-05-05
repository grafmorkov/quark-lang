#include "quark/semantic.h"
#include "quark/compiler_context.h"
#include "utils/logger.h"

#include <type_traits>

using namespace utils::logger;

namespace quark::sm {

namespace {

// Type utils

bool types_equal(const ast::Type* a, const ast::Type* b) {
    if (a == b) return true;
    if (!a || !b) return false;

    if (a->kind != b->kind) return false;

    if (a->kind == ast::Type::Struct)
        return a->struct_name == b->struct_name;

    return true;
}
bool is_assignable(const ast::Type* to, const ast::Type* from) {
    return types_equal(to, from);
}

// Scope guard

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


// Symbol table

const ast::Type* symbol_type(const quark::symb_t::Symbol& sym) {
    return std::visit([](const auto& s) -> const ast::Type* {
        using T = std::decay_t<decltype(s)>;

        if constexpr (std::is_same_v<T, quark::symb_t::VarSymbol>) {
            return s.type;
        } else if constexpr (std::is_same_v<T, quark::symb_t::FuncArgSymbol>) {
            return s.type;
        } else if constexpr (std::is_same_v<T, quark::symb_t::StructSymbol>) {
            return nullptr;
        }
    }, sym.data);
}

bool symbol_is_mutable(const quark::symb_t::Symbol& sym) {
    return std::visit([](const auto& s) -> bool {
        using T = std::decay_t<decltype(s)>;

        if constexpr (std::is_same_v<T, quark::symb_t::VarSymbol>) {
            return s.is_mut;
        } else if constexpr (std::is_same_v<T, quark::symb_t::FuncArgSymbol>) {
            return s.is_mut;
        } else if constexpr (std::is_same_v<T, quark::symb_t::StructSymbol>) {
            return false;
        }
    }, sym.data);
}

bool symbol_is_initialized(const quark::symb_t::Symbol& sym) {
    return std::visit([](const auto& s) -> bool {
        using T = std::decay_t<decltype(s)>;

        if constexpr (std::is_same_v<T, quark::symb_t::VarSymbol>) {
            return s.is_initialized;
        } else if constexpr (std::is_same_v<T, quark::symb_t::FuncArgSymbol>) {
            return true;
        } else if constexpr (std::is_same_v<T, quark::symb_t::StructSymbol>) {
            return true;
        }
    }, sym.data);
}

void mark_symbol_initialized(quark::symb_t::Symbol& sym) {
    if (auto* var = std::get_if<quark::symb_t::VarSymbol>(&sym.data)) {
        var->is_initialized = true;
    }
}

} // namespace

// Helper
bool SemanticAnalyzer::is_symbol_mutable(const ast::Expr* expr) {
    if (auto* var = std::get_if<ast::VarExpr>(&expr->kind)) {
        auto* sym = ctx.symbols.lookup(var->name);
        if (!sym) return false;
        return symbol_is_mutable(*sym);
    }
    return false;
}
const ast::Type* SemanticAnalyzer::resolve_lvalue(const ast::Expr* expr) {
    // VAR
    if (auto* var = std::get_if<ast::VarExpr>(&expr->kind)) {
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

    // FIELD ACCESS (a.b)
    if (auto* field = std::get_if<ast::FieldAccessExpr>(&expr->kind)) {

        const ast::Type* base_type = resolve_lvalue(field->base.get());
        if (!base_type) return nullptr;

        if (base_type->kind != ast::Type::Struct) {
            crash("Field access on non-struct type");
            return nullptr;
        }

        auto* sym = ctx.symbols.lookup(base_type->struct_name);
        if (!sym) {
            crash("Unknown struct: " + base_type->struct_name);
            return nullptr;
        }

        auto* ss = std::get_if<symb_t::StructSymbol>(&sym->data);
        if (!ss) {
            crash("Invalid struct symbol: " + base_type->struct_name);
            return nullptr;
        }

        for (size_t i = 0; i < ss->field_names.size(); i++) {
            if (ss->field_names[i] == field->field) {
                return ss->field_types[i];
            }
        }

        crash("Unknown field: " + field->field);
        return nullptr;
    }

    crash("Invalid lvalue");
    return nullptr;
}
// Entry

void SemanticAnalyzer::analyze(
    const std::vector<std::unique_ptr<ast::Stmt>>& stmts
) {
    ScopeGuard scope(ctx.symbols);

    for (const auto& stmt : stmts) {
        if (stmt) {
            analyze_stmt(stmt.get());
        }
    }
}

// Stmt dispatch

void SemanticAnalyzer::analyze_stmt(const ast::Stmt* stmt) {
    if (!stmt) return;

    std::visit([this](const auto& node) {
        analyze_stmt_node(node);
    }, stmt->kind);
}

// Stmt nodes

void SemanticAnalyzer::analyze_stmt_node(const ast::VarDecl& var) {
    const ast::Type* value_type = nullptr;

    if (var.value) {
        value_type = analyze_expr(var.value.get());
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
        return;
    }
}

void SemanticAnalyzer::analyze_stmt_node(const ast::FieldDecl& field) {
    (void)field;
    // TODO: type, attributes, etc.
}

void SemanticAnalyzer::analyze_stmt_node(const ast::StructDecl& str) {
    symb_t::StructSymbol sym;

    for (const auto& field : str.fields) {

        // check duplicate
        for (const auto& name : sym.field_names) {
            if (name == field->name) {
                crash("Duplicate field: " + field->name);
                return;
            }
        }

        sym.field_names.push_back(field->name);
        sym.field_types.push_back(field->type);
    }

    if (!ctx.symbols.declare_symbol(str.name, symb_t::Symbol{
        str.name,
        sym
    })) {
        crash("Struct already declared: " + str.name);
    }
}

void SemanticAnalyzer::analyze_attribute(const ast::Attribute& attribute) {
    (void)attribute;
    // TODO
}

void SemanticAnalyzer::analyze_stmt_node(const ast::ExprStmt& expr) {
    if (expr.expr) {
        analyze_expr(expr.expr.get());
    }
}

void SemanticAnalyzer::analyze_stmt_node(const ast::ReturnStmt& ret) {
    const ast::Type* value_type = ret.value
        ? analyze_expr(ret.value.get())
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

void SemanticAnalyzer::analyze_stmt_node(const ast::FuncStmt& func) {
    auto* prev_return_type = current_function_return_type;
    current_function_return_type = func.return_t;

    ScopeGuard scope(ctx.symbols);

    for (const auto& arg : func.args) {
        if (!ctx.symbols.declare(arg)) {
            crash("Argument already declared: " + arg.name);
            continue;
        }
    }

    if (func.body) {
        for (const auto& stmt : func.body->statements) {
            if (stmt) {
                analyze_stmt(stmt.get());
            }
        }
    }

    current_function_return_type = prev_return_type;
}

void SemanticAnalyzer::analyze_stmt_node(const ast::IfStmt& stmt) {
    if (stmt.condition) {
        analyze_expr(stmt.condition.get());
    }

    if (stmt.thenBranch) {
        analyze_block(stmt.thenBranch.get());
    }

    if (stmt.elseBranch) {
        analyze_block(stmt.elseBranch.get());
    }
}

void SemanticAnalyzer::analyze_stmt_node(const ast::WhileStmt& stmt) {
    if (stmt.condition) {
        analyze_expr(stmt.condition.get());
    }

    if (stmt.body) {
        analyze_block(stmt.body.get());
    }
}

// Expr

const ast::Type* SemanticAnalyzer::analyze_expr(const ast::Expr* expr) {
    if (!expr) return nullptr;

    return std::visit([this](const auto& node) -> const ast::Type* {
        return analyze_expr_node(node);
    }, expr->kind);
}

const ast::Type* SemanticAnalyzer::analyze_expr_node(const ast::IntLit&) {
    return ctx.types.get_int();
}

const ast::Type* SemanticAnalyzer::analyze_expr_node(const ast::StringLit&) {
    return ctx.types.get_string();
}

const ast::Type* SemanticAnalyzer::analyze_expr_node(const ast::VarExpr& var) {
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

const ast::Type* SemanticAnalyzer::analyze_expr_node(const ast::NoneExpr&) {
    return ctx.types.get_void();
}

const ast::Type* SemanticAnalyzer::analyze_expr_node(const ast::AssignExpr& asg) {
    const ast::Type* value_type = analyze_expr(asg.value.get());
    if (!value_type) return nullptr;

    if (!asg.target) {
        crash("Assignment target is missing");
        return nullptr;
    }

    const ast::Type* target_type = resolve_lvalue(asg.target.get());
    if (!target_type) return nullptr;

    if (!is_assignable(target_type, value_type)) {
        crash("Type mismatch in assignment");
        return nullptr;
    }

    if (auto* var = std::get_if<ast::VarExpr>(&asg.target->kind)) {
        auto* sym = ctx.symbols.lookup(var->name);
        if (sym) {
            mark_symbol_initialized(*sym);
        }
    }

    return target_type;
}
const ast::Type* SemanticAnalyzer::analyze_expr_node(const ast::FieldAccessExpr& node) {
    const ast::Type* base = analyze_expr(node.base.get());
    if (!base) return nullptr;

    if (base->kind != ast::Type::Struct) {
        crash("Field access on non-struct type");
        return nullptr;
    }

    auto* sym = ctx.symbols.lookup(base->struct_name);
    if (!sym) {
        crash("Unknown struct: " + base->struct_name);
        return nullptr;
    }

    auto* ss = std::get_if<symb_t::StructSymbol>(&sym->data);
    if (!ss) {
        crash("Invalid struct symbol");
        return nullptr;
    }

    for (int i = 0; i < ss->field_names.size(); i++) {
        if (ss->field_names[i] == node.field) {
            return ss->field_types[i];
        }
    }

    crash("Unknown field: " + node.field);
    return nullptr;
}

const ast::Type* SemanticAnalyzer::analyze_expr_node(const ast::BinaryExpr& b) {
    const ast::Type* l = analyze_expr(b.lhs.get());
    const ast::Type* r = analyze_expr(b.rhs.get());

    if (!l || !r) return nullptr;

    if (!types_equal(l, r)) {
        crash("Type mismatch in binary expression");
        return nullptr;
    }

    return l;
}

const ast::Type* SemanticAnalyzer::analyze_expr_node(const ast::CallExpr&) {
    return nullptr; // TODO
}

// Block

const ast::Type* SemanticAnalyzer::analyze_block(const ast::BlockExpr* block) {
    if (!block) return ctx.types.get_void();

    ScopeGuard scope(ctx.symbols);

    for (const auto& stmt : block->statements) {
        if (stmt) {
            analyze_stmt(stmt.get());
        }
    }

    return ctx.types.get_void();
}

} // namespace quark::sm