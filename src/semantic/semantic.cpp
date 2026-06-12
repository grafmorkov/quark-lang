#include "quark/semantic/semantic.h"
#include "quark/support/compiler_context.h"
#include "quark/support/symbol_path.h"
#include "quark/attributes/attributes.h"

#include "utils/logger.h"

#include <optional>
#include <unordered_set>
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
    if (a->kind == ast::TypeKind::Pointer){
        return types_equal(a->pointed, b->pointed);
    }

    return true;
}

bool is_assignable(const ast::Type* to, const ast::Type* from) {
    return types_equal(to, from);
}
const attrs::AttributeInfo* find_attr(const std::string& name){
	for(const auto& attr : attrs::attributes){
		if(attr.first == name){
			return &attr.second;
		}
	}
	return nullptr;
}
inline bool has_flag(attrs::AttributeTarget value, attrs::AttributeTarget flag) {
    return (static_cast<uint32_t>(value) &
            static_cast<uint32_t>(flag)) != 0;
}
std::string attr_target_to_string(attrs::AttributeTarget target)
{
    if (target == attrs::AttributeTarget::None)
        return "None";

    std::string result;

    auto append = [&](attrs::AttributeTarget flag, const char* name) {
        if ((static_cast<uint32_t>(target) &
             static_cast<uint32_t>(flag)) != 0)
        {
            if (!result.empty())
                result += " | ";

            result += name;
        }
    };

    append(attrs::AttributeTarget::Function, "Function");
    append(attrs::AttributeTarget::Variable, "Variable");
    append(attrs::AttributeTarget::Field,    "Field");
    append(attrs::AttributeTarget::Struct,   "Struct");
    append(attrs::AttributeTarget::Module,   "Module");

    return result;
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
    if (std::get_if<quark::symb_t::FuncSymbol>(&sym.data)) {
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
    if (!base_type) {
        return nullptr;
    }

    if (base_type->kind != ast::TypeKind::Struct) {
        crash("Field access on non-struct type");
        return nullptr;
    }

    auto* sym = ctx.symbols.lookup(base_type->struct_name);
    if (!sym && !base_type->type_args.empty()) {
        if (ctx.types.try_instantiate(base_type->struct_name, base_type->type_args)) {
            const auto* fields = ctx.types.get_struct_fields(base_type->struct_name);
            if (fields) {
                ctx.symbols.declare_struct_global(base_type->struct_name, *fields);
                sym = ctx.symbols.lookup(base_type->struct_name);
            }
        }
    }
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
    if (!expr) {
        return nullptr;
    }

    if (const auto* v = std::get_if<ast::VarExpr>(&expr->kind)) {
        return v;
    }

    if (const auto* f = std::get_if<ast::FieldExpr>(&expr->kind)) {
        return get_root_var(f->base);
    }

    return nullptr;
}

} // namespace

namespace {
    bool has_attr(const std::vector<ast::Attribute>& attrs, const std::string& name) {
        for (const auto& a : attrs) {
            if (a.name == name) return true;
        }
        return false;
    }
}

void SemanticAnalyzer::analyze(const std::vector<ast::Stmt*>& stmts, modules::Module* mod) {
    current_module = mod;
    ctx.symbols.set_current_module_ns(module_namespace);

    // Extract module-level attributes (like @hide) from top-level statements
    if (current_module) {
        for (auto* stmt : stmts) {
            if (!stmt) continue;
            std::visit(overloaded{
                [&](ast::FuncStmt& fn) {
                    for (auto it = fn.attributes.begin(); it != fn.attributes.end(); ) {
                        if (it->name == "hide") {
                            current_module->attributes.push_back(std::move(*it));
                            it = fn.attributes.erase(it);
                        } else {
                            ++it;
                        }
                    }
                },
                [&](ast::StructDecl& str) {
                    for (auto it = str.attributes.begin(); it != str.attributes.end(); ) {
                        if (it->name == "hide") {
                            current_module->attributes.push_back(std::move(*it));
                            it = str.attributes.erase(it);
                        } else {
                            ++it;
                        }
                    }
                },
                [&](ast::VarDecl& var) {
                    for (auto it = var.attributes.begin(); it != var.attributes.end(); ) {
                        if (it->name == "hide") {
                            current_module->attributes.push_back(std::move(*it));
                            it = var.attributes.erase(it);
                        } else {
                            ++it;
                        }
                    }
                },
                [&](ast::ModuleDecl& mod_decl) {
                    // Module-level attributes (@hide etc.) now live on the decl itself
                    for (auto it = mod_decl.attributes.begin(); it != mod_decl.attributes.end(); ) {
                        if (it->name == "hide") {
                            current_module->attributes.push_back(std::move(*it));
                            it = mod_decl.attributes.erase(it);
                        } else {
                            ++it;
                        }
                    }
                },
                [&](const auto&) {}
            }, stmt->kind);
        }
    }

    for (const auto& part : module_namespace) {
        ctx.symbols.enter_namespace(part);
    }

    collect_declarations(stmts);

    // If module has @hide, mark all non-@public symbols as private
    if (current_module && has_attr(current_module->attributes, "hide")) {
        for (auto* stmt : stmts) {
            if (!stmt) continue;
            std::visit(overloaded{
                [&](const ast::FuncStmt& fn) {
                    if (!has_attr(fn.attributes, "public")) {
                        auto* sym = ctx.symbols.lookup(fn.name);
                        if (sym) sym->attributes.push_back({"private", {}});
                    }
                },
                [&](const ast::StructDecl& str) {
                    if (!str.type_params.empty()) return; // generic — no symbol
                    if (!has_attr(str.attributes, "public")) {
                        auto* sym = ctx.symbols.lookup(str.name);
                        if (sym) sym->attributes.push_back({"private", {}});
                    }
                },
                [&](const ast::VarDecl& var) {
                    if (!has_attr(var.attributes, "public")) {
                        auto* sym = ctx.symbols.lookup(var.name);
                        if (sym) sym->attributes.push_back({"private", {}});
                    }
                },
                [&](const auto&) {}
            }, stmt->kind);
        }
    }

    for (auto* stmt : stmts) {
        analyze_stmt(stmt);
    }

    for (size_t i = 0; i < module_namespace.size(); ++i) {
        ctx.symbols.exit_namespace();
    }
}

void SemanticAnalyzer::collect_declarations(const std::vector<ast::Stmt*>& stmts) {
    for (auto* stmt : stmts) {
        if (!stmt) {
            continue;
        }

        std::visit(overloaded{
            [&](const ast::FuncStmt& fn) {
                if (!fn.type_params.empty()) {
                    types::GenericFuncDef def;
                    def.params = fn.type_params;
                    def.args = fn.args;
                    def.return_type = fn.return_type;
                    def.body = fn.body;
                    def.attributes = fn.attributes;
                    ctx.types.register_generic_func(fn.name, def);
                } else {
                    if (!ctx.symbols.declare(fn)) {
                        crash("Function redeclaration: " + fn.name);
                    }
                }
            },
            [&](const ast::StructDecl& str) {
                if (!str.type_params.empty()) {
                    types::GenericStructDef def;
                    def.params = str.type_params;
                    def.fields = str.fields;
                    ctx.types.register_generic_struct(str.name, def);
                } else {
                    if (!ctx.symbols.declare(str)) {
                        crash("Struct redeclaration: " + str.name);
                    }
                }
            },
            [&](const ast::NamespaceStmt& ns) {
                NamespaceGuard guard(ctx.symbols, ns.name);
                if (ns.body) {
                    collect_declarations(ns.body->stmts);
                }
            },
            [&](const ast::ModuleDecl&) {},
            [&](const auto&) {}
        }, stmt->kind);
    }
}

void SemanticAnalyzer::analyze_stmt(const ast::Stmt* stmt) {
    if (!stmt) return;

    std::visit(overloaded{
        [&](const ast::VarDecl& n) { analyze_var_decl(n); },
        [&](const ast::StructDecl& n) { analyze_struct_decl(n); },
        [&](const ast::NamespaceStmt& n) { analyze_namespace_stmt(n); },
        [&](const ast::ExprStmt& n) { analyze_expr_stmt(n); },
        [&](const ast::ReturnStmt& n) { analyze_return(n); },
        [&](const ast::FuncStmt& n) { analyze_func(n); },
        [&](const ast::IfStmt& n) { analyze_if(n); },
        [&](const ast::WhileStmt& n) { analyze_while(n); },
        [&](const ast::ModuleDecl&) {},
        [&](const ast::LoadStmt&) {},
        [&](const ast::RegionStmt& n) { analyze_region(n); },
        [&](const auto&) {
            crash("Unsupported statement node in semantic analysis");
        }
    }, stmt->kind);
}

std::optional<int64_t> SemanticAnalyzer::try_eval_const(const ast::Expr* expr) {
    if (!expr) return std::nullopt;
    if (const auto* ie = std::get_if<ast::IntExpr>(&expr->kind)) {
        return ie->value;
    }
    if (const auto* be = std::get_if<ast::BoolExpr>(&expr->kind)) {
        return be->value ? 1 : 0;
    }
    if (const auto* ce = std::get_if<ast::CharExpr>(&expr->kind)) {
        return ce->value;
    }
    if (const auto* ve = std::get_if<ast::VarExpr>(&expr->kind)) {
        auto* sym = ctx.symbols.lookup(ve->name);
        if (sym && std::holds_alternative<symb_t::VarSymbol>(sym->data)) {
            return std::get<symb_t::VarSymbol>(sym->data).const_value;
        }
    }
    return std::nullopt;
}

void SemanticAnalyzer::analyze_var_decl(const ast::VarDecl& var) {
    if (!var.type) {
        crash("Variable declaration missing type: " + var.name);
        return;
    }

    for (const auto& attr : var.attributes) {
        analyze_attribute(attr, attrs::AttributeTarget::Variable);
    }

    bool has_init = false;
    for (const auto& attr : var.attributes) {
        if (attr.name == "init") has_init = true;
    }

    if (var.value) {
        const ast::Type* value_type = analyze_expr(var.value);
        if (!value_type) return;

        if (!is_assignable(var.type, value_type)) {
            crash("Type mismatch in variable initialization: " + var.name);
            return;
        }
    } else if (!var.is_mut && !has_init) {
        crash("Immutable variable must be initialized: " + var.name);
        return;
    }

    if (!ctx.symbols.declare(var)) {
        crash("Variable already declared: " + var.name);
    }

    if (has_init && !var.value) {
        ctx.symbols.mark_initialized(var.name);
    }

    if (var.value) {
        if (const auto* ie = std::get_if<ast::IntExpr>(&var.value->kind)) {
            auto* sym = ctx.symbols.lookup(var.name);
            if (sym && std::holds_alternative<symb_t::VarSymbol>(sym->data)) {
                auto& vs = std::get<symb_t::VarSymbol>(sym->data);
                vs.const_value = ie->value;
            }
        } else if (const auto* be = std::get_if<ast::BoolExpr>(&var.value->kind)) {
            auto* sym = ctx.symbols.lookup(var.name);
            if (sym && std::holds_alternative<symb_t::VarSymbol>(sym->data)) {
                auto& vs = std::get<symb_t::VarSymbol>(sym->data);
                vs.const_value = be->value ? 1 : 0;
            }
        } else if (const auto* ce = std::get_if<ast::CharExpr>(&var.value->kind)) {
            auto* sym = ctx.symbols.lookup(var.name);
            if (sym && std::holds_alternative<symb_t::VarSymbol>(sym->data)) {
                auto& vs = std::get<symb_t::VarSymbol>(sym->data);
                vs.const_value = ce->value;
            }
        }
    }

    for (const auto& attr : var.attributes) {
        if (attr.name == "guard" && !attr.args.empty()) {
            const ast::Type* guard_type = analyze_expr(attr.args[0]);
            if (!guard_type) continue;
            auto guard_val = try_eval_const(attr.args[0]);
            if (guard_val && *guard_val == 0) {
                auto* sym = ctx.symbols.lookup(var.name);
                if (sym && std::holds_alternative<symb_t::VarSymbol>(sym->data)) {
                    std::get<symb_t::VarSymbol>(sym->data).guard_blocked = true;
                }
            }
        }
    }
}

void SemanticAnalyzer::analyze_struct_decl(const ast::StructDecl& str) {
    if (!str.type_params.empty()) {
        // Generic struct - fields may reference type params;
        // skip deep analysis, concrete instances are checked at instantiation.
        return;
    }

    std::unordered_set<std::string> seen;

    for (const auto& field : str.fields) {
        if (!seen.insert(field.name).second) {
            crash("Duplicate field: " + field.name);
            return;
        }

        if (!field.type) {
            crash("Field missing type: " + field.name);
            return;
        }

        for (const auto& attr : field.attributes) {
            analyze_attribute(attr, attrs::AttributeTarget::Field);
        }

        if (field.default_value) {
            const ast::Type* dt = analyze_expr(field.default_value);
            if (!dt) return;

            if (!is_assignable(field.type, dt)) {
                crash("Type mismatch in field default value: " + field.name);
                return;
            }
        }
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
        : ctx.types.get_builtin(TypeKind::Void);

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
    if (func.is_extern && func.body) {
        crash("Extern function cannot have a body: " + func.name);
        return;
    }

    for (const auto& attr : func.attributes) {
        analyze_attribute(attr, attrs::AttributeTarget::Function);
    }

    if (!func.return_type) {
        crash("Function missing return type: " + func.name);
        return;
    }

    for (const auto& arg : func.args) {
        if (!arg.type) {
            crash("Function argument missing type: " + arg.name);
            return;
        }
    }

    // Generic functions: register only, body is analyzed at instantiation
    if (!func.type_params.empty()) {
        return;
    }

    if (!func.body) {
        return;
    }

    const ast::Type* prev_return_type = current_function_return_type;
    current_function_return_type = func.return_type;

    ScopeGuard scope(ctx.symbols);

    for (const auto& arg : func.args) {
        if (!ctx.symbols.declare(arg)) {
            crash("Duplicate function argument: " + arg.name);
            current_function_return_type = prev_return_type;
            return;
        }
    }

    analyze_block(func.body);

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
void SemanticAnalyzer::analyze_region(const ast::RegionStmt& reg) {
    is_in_region = true;

    {
        ScopeGuard scope(ctx.symbols);
        if (reg.body) {
            analyze_block(reg.body);
        }
    }

    is_in_region = false;
}

void SemanticAnalyzer::check_visibility(const symb_t::Symbol& sym, const std::string& context) {
    if (has_attr(sym.attributes, "private") && sym.owning_module != module_namespace) {
        crash("Cannot access private symbol '" + sym.name + "' from " + context);
    }
}

void SemanticAnalyzer::analyze_attribute(const ast::Attribute& attr, const attrs::AttributeTarget target) {
	auto* it = find_attr(attr.name);

	if(it == nullptr){
		crash("Attribute: '@" + attr.name + "' not found");
	}
	if(!has_flag(it->targets, target)){
		crash("Attribute: '@" + attr.name + "' has incorrect targets: " + attr_target_to_string(target)
				+ ". Expected: " + attr_target_to_string(it->targets));
	}

	if(attr.args.size() > it->max_args){
		crash("Expected '" + std::to_string(it->max_args)
                                  + "' count of args for '@" + attr.name + "' attribute."
                                  + "Got: '" + std::to_string(attr.args.size()) );
	}
	else if(attr.args.size() < it->min_args){
		crash("Expected '" + std::to_string(it->min_args) 
				+ "' count of args for '@" + attr.name + "' attribute." 
				+ "Got: '" + std::to_string(attr.args.size()) );
	}
}

const ast::Type* SemanticAnalyzer::analyze_expr(ast::Expr* expr) {
    if (!expr) return nullptr;

    const ast::Type* ty = std::visit(overloaded{
        [&](const ast::IntExpr&) -> const ast::Type* {
            return ctx.types.get_builtin(TypeKind::I32);
        },
        [&](const ast::BoolExpr&) -> const ast::Type* {
            return ctx.types.get_builtin(TypeKind::Bool);
        },
        [&](const ast::FloatExpr&) -> const ast::Type* {
            return ctx.types.get_builtin(TypeKind::F64);
        },
        [&](const ast::StringExpr&) -> const ast::Type* {
            return ctx.types.get_builtin(TypeKind::String);
        },
        [&](const ast::CharExpr&) -> const ast::Type* {
            return ctx.types.get_builtin(TypeKind::U8);
        },
        [&](const ast::VarExpr& n) -> const ast::Type* {
            return analyze_var(n);
        },
        [&](const ast::AssignExpr& n) -> const ast::Type* {
            return analyze_assign(n);
        },
        [&](const ast::BinaryExpr& n) -> const ast::Type* {
            return analyze_binary(n);
        },
        [&](const ast::CallExpr& n) -> const ast::Type* {
            return analyze_call(n);
        },
        [&](const ast::FieldExpr& n) -> const ast::Type* {
            return analyze_field(n);
        },
        [&](const ast::NamespaceExpr& n) -> const ast::Type* {
            return analyze_namespace(n);
        },
        [&](const ast::CastExpr& n) -> const ast::Type*{
            return analyze_cast(n);
        },
        [&](const ast::TypeExpr&) -> const ast::Type* {
            crash("Type used as value");
            return nullptr;
        },
        [&](const ast::IndexExpr& n) -> const ast::Type* {
            return analyze_index(n);
        },
        [&](const auto&) -> const ast::Type* {
            crash("Unsupported expression node in semantic analysis");
            return nullptr;
        }
    }, expr->kind);

    if (ty) {
        expr->resolved_type = ty;
    }
    return ty;
}

const ast::Type* SemanticAnalyzer::analyze_int(const ast::IntExpr&) {
    return ctx.types.get_builtin(TypeKind::I32);
}

const ast::Type* SemanticAnalyzer::analyze_string(const ast::StringExpr&) {
    return ctx.types.get_builtin(TypeKind::String);
}

const ast::Type* SemanticAnalyzer::analyze_var(const ast::VarExpr& var) {
    auto* sym = ctx.symbols.lookup(var.name);

    if (!sym) {
        crash("Undefined variable: " + var.name);
        return nullptr;
    }

    check_visibility(*sym, module_namespace.empty() ? "::" : support::join_namespace(module_namespace));

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

        check_visibility(*sym, module_namespace.empty() ? "::" : support::join_namespace(module_namespace));

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

    if (const auto* index = std::get_if<ast::IndexExpr>(&expr->kind)) {
        const ast::Type* base_type = analyze_expr(index->base);
        if (!base_type) return nullptr;

        if (base_type->kind != TypeKind::Pointer) {
            crash("Cannot index non-pointer type");
            return nullptr;
        }

        const ast::Type* elem_type = base_type->pointed;
        if (!elem_type) {
            crash("Invalid pointer target");
            return nullptr;
        }

        const ast::Type* index_type = analyze_expr(index->index);
        if (!index_type) return nullptr;

        if (index_type->kind == TypeKind::Void) {
            crash("Index must be an integer");
            return nullptr;
        }

        return elem_type;
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

const ast::Type* SemanticAnalyzer::analyze_call(const ast::CallExpr& call) {
    if (!call.callee) {
        crash("Call callee is missing");
        return nullptr;
    }

    auto path = support::flatten_path(call.callee);

    if (is_in_region && path.size() == 1 && path[0] == "alloc") {
        if (call.args.size() == 2) {
            // alloc(T, count) — typed allocation
            const auto* type_expr = std::get_if<ast::TypeExpr>(&call.args[0]->kind);
            if (!type_expr) {
                crash("First argument to alloc must be a type, e.g. alloc(i32, 10)");
                return nullptr;
            }
            const ast::Type* elem_type = type_expr->type;
            if (!elem_type) {
                crash("Invalid element type in alloc");
                return nullptr;
            }
            call.args[0]->resolved_type = elem_type;

            const ast::Type* count_type = analyze_expr(call.args[1]);
            if (!count_type) return nullptr;
            if (count_type->kind == TypeKind::Void) {
                crash("alloc count must be an integer");
                return nullptr;
            }

            return ctx.types.get_pointer(elem_type);
        }

        if (call.args.size() != 1) {
            crash("alloc takes 1 argument (bytes) or 2 arguments (type, count)");
            return nullptr;
        }
        const ast::Type* size_type = analyze_expr(call.args[0]);
        if (!size_type) return nullptr;

        if (size_type->kind == TypeKind::Void || size_type->kind == TypeKind::String) {
            crash("alloc argument must be an integer (size in bytes)");
            return nullptr;
        }

        return ctx.types.get_pointer(ctx.types.get_builtin(TypeKind::Void));
    }

    std::string func_name = path.back();

    // Check if this is a generic function call
    const auto* generic_def = ctx.types.get_generic_func(func_name);
    if (generic_def && !call.type_args.empty()) {
        // Build substitution map: param name -> concrete type
        std::unordered_map<std::string, const Type*> subst;
        if (call.type_args.size() != generic_def->params.size()) {
            crash("Type argument count mismatch for generic function: " + func_name);
            return nullptr;
        }
        for (size_t i = 0; i < generic_def->params.size(); ++i) {
            subst[generic_def->params[i]] = call.type_args[i];
        }

        // Mangle the concrete function name
        std::string mangled = ctx.types.mangle_func_name(func_name, call.type_args);

        // Check if already instantiated
        auto* concrete_sym = ctx.symbols.lookup(mangled);
        if (!concrete_sym) {
            // Create concrete arg types by substitution
            std::vector<const Type*> concrete_arg_types;
            std::vector<ast::FuncArg> concrete_args;
            for (const auto& arg : generic_def->args) {
                concrete_arg_types.push_back(ctx.types.substitute_type(arg.type, subst));
                concrete_args.push_back(ctx.types.substitute_func_arg(arg, subst));
            }
            const Type* concrete_return = ctx.types.substitute_type(generic_def->return_type, subst);

            // Create and register the concrete function symbol
            symb_t::FuncSymbol concrete_fn_sym;
            concrete_fn_sym.arg_types = concrete_arg_types;
            concrete_fn_sym.return_type = concrete_return;
            concrete_fn_sym.is_extern = generic_def->body == nullptr;
            concrete_fn_sym.is_defined = generic_def->body != nullptr;
            concrete_fn_sym.is_entry = false;

            ctx.symbols.declare_symbol(mangled, symb_t::Symbol{
                mangled,
                concrete_fn_sym,
                {}
            });

            // Create a concrete FuncStmt for body analysis and IR gen
            ast::FuncStmt concrete_fn_stmt;
            concrete_fn_stmt.name = mangled;
            concrete_fn_stmt.args = concrete_args;
            concrete_fn_stmt.return_type = concrete_return;
            concrete_fn_stmt.type_params = {};
            concrete_fn_stmt.is_extern = generic_def->body == nullptr;
            concrete_fn_stmt.is_forward = false;
            concrete_fn_stmt.is_entry = false;
            concrete_fn_stmt.has_body = generic_def->body != nullptr;
            concrete_fn_stmt.body = const_cast<ast::Block*>(generic_def->body);
            concrete_fn_stmt.attributes = generic_def->attributes;

            // Analyze the concrete function body (type-check with concrete types)
            if (concrete_fn_stmt.body) {
                analyze_func(concrete_fn_stmt);
            }

            // Store for IR generation
            ctx.generic_instantiations.push_back(std::move(concrete_fn_stmt));

            concrete_sym = ctx.symbols.lookup(mangled);
        }

        if (!concrete_sym) {
            crash("Failed to instantiate generic function: " + func_name);
            return nullptr;
        }

        auto* concrete_fn = std::get_if<symb_t::FuncSymbol>(&concrete_sym->data);
        if (!concrete_fn) {
            crash("Invalid concrete function symbol: " + mangled);
            return nullptr;
        }

        // Check argument count and types
        if (concrete_fn->arg_types.size() != call.args.size()) {
            crash("Argument count mismatch in generic call: " + func_name);
            return nullptr;
        }

        for (size_t i = 0; i < call.args.size(); ++i) {
            ast::Expr* arg = call.args[i];
            const ast::Type* arg_type = arg ? analyze_expr(arg) : nullptr;
            if (!arg_type) return nullptr;

            if (!is_assignable(concrete_fn->arg_types[i], arg_type)) {
                crash("Argument type mismatch in generic call: " + func_name);
                return nullptr;
            }
        }

        return concrete_fn->return_type;
    }

    // Non-generic function lookup
    auto* sym = path.size() == 1
        ? ctx.symbols.lookup(path[0])
        : ctx.symbols.lookup_qualified(path);
    if (!sym) {
        crash("Undefined function: " + support::join_namespace(path));
        return nullptr;
    }

    check_visibility(*sym, support::join_namespace(path));

    auto* fn = std::get_if<symb_t::FuncSymbol>(&sym->data);
    if (!fn) {
        crash("Callee is not a function: " + support::join_namespace(path));
        return nullptr;
    }

    if (fn->arg_types.size() != call.args.size()) {
        crash("Argument count mismatch in call: " + support::join_namespace(path));
        return nullptr;
    }

    for (size_t i = 0; i < call.args.size(); ++i) {
        ast::Expr* arg = call.args[i];
        const ast::Type* arg_type = arg ? analyze_expr(arg) : nullptr;
        if (!arg_type) return nullptr;

        if (!is_assignable(fn->arg_types[i], arg_type)) {
            crash("Argument type mismatch in call: " + support::join_namespace(path));
            return nullptr;
        }
    }

    return fn->return_type;
}

const ast::Type* SemanticAnalyzer::analyze_index(const ast::IndexExpr& n) {
    const ast::Type* base_type = analyze_expr(n.base);
    if (!base_type) return nullptr;

    if (base_type->kind != TypeKind::Pointer) {
        crash("Cannot index non-pointer type");
        return nullptr;
    }

    const ast::Type* elem_type = base_type->pointed;
    if (!elem_type) {
        crash("Invalid pointer target");
        return nullptr;
    }

    const ast::Type* index_type = analyze_expr(n.index);
    if (!index_type) return nullptr;

    if (index_type->kind == TypeKind::Void) {
        crash("Index must be an integer");
        return nullptr;
    }

    return elem_type;
}

const ast::Type* SemanticAnalyzer::analyze_block(const ast::Block* block) {
    if (!block) return ctx.types.get_builtin(TypeKind::Void);

    ScopeGuard scope(ctx.symbols);

    for (const auto& stmt : block->stmts) {
        if (stmt) {
            analyze_stmt(stmt);
        }
    }

    return ctx.types.get_builtin(TypeKind::Void);
}

const ast::Type* SemanticAnalyzer::analyze_namespace(const ast::NamespaceExpr& n) {
    auto expr = ast::Expr{n};
    auto path = support::flatten_path(&expr);
    auto* sym = ctx.symbols.lookup_qualified(path);

    if (!sym) {
        crash("Undefined qualified symbol");
        return nullptr;
    }

    check_visibility(*sym, support::join_namespace(path));

    if (auto* vt = std::get_if<symb_t::VarSymbol>(&sym->data)) {
        return vt->type;
    }
    if (auto* ft = std::get_if<symb_t::FuncSymbol>(&sym->data)) {
        return ft->return_type;
    }
    if (std::get_if<symb_t::StructSymbol>(&sym->data)) {
        return nullptr;
    }

    crash("Qualified path is not a value");
    return nullptr;
}
bool is_numeric(ast::TypeKind kind) {
    switch (kind) {
        case ast::TypeKind::I8: case ast::TypeKind::I16:
        case ast::TypeKind::I32: case ast::TypeKind::I64:
        case ast::TypeKind::U8: case ast::TypeKind::U16:
        case ast::TypeKind::U32: case ast::TypeKind::U64:
        case ast::TypeKind::F32: case ast::TypeKind::F64:
            return true;
        default:
            return false;
    }
}

int type_size(const ast::Type* t) {
    if (!t) return 0;
    switch (t->kind) {
        case ast::TypeKind::Bool:
        case ast::TypeKind::I8:
        case ast::TypeKind::U8:   return 1;
        case ast::TypeKind::I16:
        case ast::TypeKind::U16:  return 2;
        case ast::TypeKind::F32:
        case ast::TypeKind::I32:
        case ast::TypeKind::U32:  return 4;
        case ast::TypeKind::F64:
        case ast::TypeKind::I64:
        case ast::TypeKind::U64:  return 8;
        case ast::TypeKind::Pointer: return 8;
        default: return 0;
    }
}

const ast::Type* SemanticAnalyzer::analyze_cast(const ast::CastExpr& n){
    const ast::Type* value_type = analyze_expr(n.value);
    if(!value_type) return nullptr;

    if(!n.target){
        crash("Cast target type is missing");
        return nullptr;
    }
    switch (n.kind) {
        case ast::CastKind::ValueCast:
            if (n.target->kind == TypeKind::String) {
                if (!is_numeric(value_type->kind)) {
                    crash("as: only numeric types can be converted to string");
                    return nullptr;
                }
            } else if (!is_numeric(value_type->kind) || !is_numeric(n.target->kind)) {
                crash("as: only numeric type conversions are allowed");
                return nullptr;
            }
            break;
        case ast::CastKind::Bitcast:
            if (value_type->kind == TypeKind::Pointer || n.target->kind == TypeKind::Pointer) {
            } else if (type_size(value_type) != type_size(n.target)) {
                crash("as!: types must have the same size");
                return nullptr;
            }
            break;
    }
    return n.target;
}

} // namespace quark::sm
