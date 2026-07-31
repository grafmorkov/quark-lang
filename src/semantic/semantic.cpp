#include "quark/semantic/semantic.h"
#include "quark/frontend/ast.h"
#include "quark/semantic/symbol_table.h"
#include "quark/support/compiler_context.h"
#include "quark/support/symbol_path.h"
#include "quark/attributes/attributes.h"
#include <cstdint>

#include "utils/logger.h"

#include <optional>
#include <unordered_set>
#include <utility>
#include <variant>

namespace {

symb_t::Symbol* resolve_qualified(
    quark::symb_t::SymbolTable& symbols,
    const std::vector<std::string>& path
) {
    if (path.empty()) return nullptr;
    if (path.size() == 1) return symbols.lookup(path[0]);

    auto* ns = symbols.get_current_namespace();
    while (ns) {
        auto* target = ns;
        for (size_t i = 0; i + 1 < path.size() && target; ++i) {
            auto it = target->children.find(path[i]);
            if (it == target->children.end()) {
                target = nullptr;
                break;
            }
            target = it->second;
        }
        if (target) {
            auto sym_it = target->symbols.find(path.back());
            if (sym_it != target->symbols.end()) {
                return sym_it->second;
            }
        }
        ns = ns->parent;
    }

    if (path.size() >= 2) {
        auto* first_sym = symbols.lookup(path[0]);
        if (first_sym && std::holds_alternative<quark::symb_t::EnumSymbol>(first_sym->data)) {
            return symbols.lookup(path.back());
        }
    }

    return symbols.lookup(quark::support::join_namespace(path));
}

symb_t::Symbol* lookup_struct(quark::CompilerContext& ctx, const std::string& struct_name) {
    return resolve_qualified(ctx.symbols, quark::support::split_path(struct_name));
}

struct NamespacePathGuard {
    std::vector<std::string>& path;
    size_t prev_size;
    NamespacePathGuard(std::vector<std::string>& p, const std::string& name) : path(p) {
        prev_size = path.size();
        path.push_back(name);
    }
    ~NamespacePathGuard() { path.resize(prev_size); }
};

std::string full_qualified(const std::vector<std::string>& module_ns,
                           const std::vector<std::string>& ns_path,
                           const std::string& name) {
    std::vector<std::string> full = module_ns;
    full.insert(full.end(), ns_path.begin(), ns_path.end());
    full.push_back(name);
    return support::join_namespace(full);
}

std::string generic_key(const std::vector<std::string>& ns_path,
                        const std::string& name) {
    if (!ns_path.empty()) {
        auto full = ns_path;
        full.push_back(name);
        return support::join_namespace(full);
    }
    return name;
}

} // namespace

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
    if (a->kind == ast::TypeKind::Reference){
        return types_equal(a->pointed, b->pointed);
    }

    return true;
}

bool is_assignable(const ast::Type* to, const ast::Type* from) {
    if (types_equal(to, from)) return true;

    auto is_numeric_type = [](TypeKind k) {
        return k >= TypeKind::I8 && k <= TypeKind::F64;
    };
    auto is_ptr_like = [](TypeKind k) {
        return k == TypeKind::Pointer || k == TypeKind::Reference;
    };

    // Implicit numeric promotion
    if (is_numeric_type(to->kind) && is_numeric_type(from->kind))
        return true;

    // Pointer<->Reference interop (same pointed type)
    if (is_ptr_like(to->kind) && is_ptr_like(from->kind))
        return types_equal(to->pointed, from->pointed);

    // Reference<T> <-> T: auto-ref / auto-deref
    if (to->kind == TypeKind::Reference && from->kind != TypeKind::Reference)
        return types_equal(to->pointed, from);
    if (from->kind == TypeKind::Reference && to->kind != TypeKind::Reference)
        return types_equal(from->pointed, to);

    return false;
}

bool infer_generic_params(
    const ast::Type* pattern,
    const ast::Type* concrete,
    const std::vector<std::string>& generic_params,
    std::unordered_map<std::string, const ast::Type*>& subst
) {
    if (!pattern || !concrete) return false;

    if (pattern->kind == ast::TypeKind::Generic) {
        if (std::find(generic_params.begin(), generic_params.end(), pattern->struct_name) != generic_params.end()) {
            auto [it, inserted] = subst.try_emplace(pattern->struct_name, concrete);
            if (!inserted) return types_equal(it->second, concrete);
            return true;
        }
        return true;
    }

    if (pattern->kind != concrete->kind) return false;

    if (pattern->kind == ast::TypeKind::Struct) {
        auto base_of = [](const std::string& name) -> std::string {
            auto dollar = name.find('$');
            std::string base = (dollar != std::string::npos) ? name.substr(0, dollar) : name;
            auto colon = base.rfind("::");
            return (colon != std::string::npos) ? base.substr(colon + 2) : base;
        };
        if (base_of(pattern->struct_name) != base_of(concrete->struct_name)) return false;
        if (pattern->type_args.size() != concrete->type_args.size()) return false;
        for (size_t i = 0; i < pattern->type_args.size(); ++i) {
            if (!infer_generic_params(pattern->type_args[i], concrete->type_args[i], generic_params, subst))
                return false;
        }
        return true;
    }

    if (pattern->kind == ast::TypeKind::Pointer) {
        return infer_generic_params(pattern->pointed, concrete->pointed, generic_params, subst);
    }

    if (pattern->kind == ast::TypeKind::Reference) {
        if (concrete->kind != ast::TypeKind::Reference) return false;
        return infer_generic_params(pattern->pointed, concrete->pointed, generic_params, subst);
    }

    return types_equal(pattern, concrete);
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

bool symbol_is_enum_value(const quark::symb_t::Symbol& sym) {
    if (const auto* v = std::get_if<quark::symb_t::VarSymbol>(&sym.data)) {
        return v->const_value.has_value() && !v->is_mut;
    }
    return false;
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
    if (std::get_if<quark::symb_t::EnumSymbol>(&sym.data)) {
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

    // Auto-deref references
    if (base_type->kind == ast::TypeKind::Reference && base_type->pointed) {
        base_type = base_type->pointed;
    }

    if (base_type->kind != ast::TypeKind::Struct) {
        ctx.errors.add("Field access on non-struct type");
        return nullptr;
    }

    auto* sym = lookup_struct(ctx, base_type->struct_name);
    if (!sym && !base_type->type_args.empty()) {
        if (ctx.types.try_instantiate(base_type->struct_name, base_type->type_args)) {
            const auto* fields = ctx.types.get_struct_fields(base_type->struct_name);
            if (fields) {
                const auto* field_attrs = ctx.types.get_struct_field_attrs(base_type->struct_name);
                ctx.symbols.declare_struct_global(
                    base_type->struct_name, *fields, {},
                    field_attrs ? *field_attrs : std::vector<std::vector<ast::Attribute>>{}
                );
                sym = lookup_struct(ctx, base_type->struct_name);
            }
        }
    }
    if (!sym) {
        ctx.errors.add("Unknown struct: " + base_type->struct_name);
        return nullptr;
    }

    auto* ss = std::get_if<quark::symb_t::StructSymbol>(&sym->data);
    if (!ss) {
        ctx.errors.add("Invalid struct symbol: " + base_type->struct_name);
        return nullptr;
    }

    for (size_t i = 0; i < ss->field_names.size(); ++i) {
        if (ss->field_names[i] == field_name) {
            return ss->field_types[i];
        }
    }

    ctx.errors.add("Unknown field: " + field_name);
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
                [&](ast::EnumDecl& enm) {
                    for (auto it = enm.attributes.begin(); it != enm.attributes.end(); ) {
                        if (it->name == "hide") {
                            current_module->attributes.push_back(std::move(*it));
                            it = enm.attributes.erase(it);
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
                [&](const ast::EnumDecl& enm) {
                    if (!has_attr(enm.attributes, "public")) {
                        auto* sym = ctx.symbols.lookup(enm.name);
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

const ast::Type* SemanticAnalyzer::canonicalize_struct_type(const ast::Type* type) {
    if (!type) return type;

    if (type->kind == TypeKind::Pointer && type->pointed) {
        const ast::Type* new_pointed = canonicalize_struct_type(type->pointed);
        if (new_pointed != type->pointed) return ctx.types.get_pointer(new_pointed);
        return type;
    }
    if (type->kind == TypeKind::Reference && type->pointed) {
        const ast::Type* new_pointed = canonicalize_struct_type(type->pointed);
        if (new_pointed != type->pointed) return ctx.types.get_reference(new_pointed);
        return type;
    }
    if (type->kind != TypeKind::Struct) return type;

    std::string base = type->struct_name;
    std::string unmangled;
    if (ctx.types.is_mangled_name(base, unmangled)) {
        base = unmangled;
    } else {
        auto dollar = base.find('$');
        if (dollar != std::string::npos) base = base.substr(0, dollar);
    }

    if (base.find("::") != std::string::npos) return type;

    auto* sym = lookup_struct(ctx, base);
    if (!sym || sym->owning_module.empty()) {
        return type;
    }

    std::string canonical = support::join_namespace(sym->owning_module) + "::" + base;
    if (canonical == type->struct_name) return type;

    if (!type->type_args.empty()) {
        return ctx.types.get_deferred_generic(canonical, type->type_args);
    }
    return ctx.types.get_struct(canonical);
}

void SemanticAnalyzer::collect_declarations(const std::vector<ast::Stmt*>& stmts) {
    for (auto* stmt : stmts) {
        if (!stmt) {
            continue;
        }

        std::visit(overloaded{
            [&](const ast::FuncStmt& fn) {
                auto& f = const_cast<ast::FuncStmt&>(fn);
                f.return_type = canonicalize_struct_type(f.return_type);
                for (auto& arg : f.args) {
                    arg.type = canonicalize_struct_type(arg.type);
                }
                if (!fn.type_params.empty()) {
                    types::GenericFuncDef def;
                    def.params = fn.type_params;
                    def.args = fn.args;
                    def.return_type = fn.return_type;
                    def.body = fn.body;
                    def.attributes = fn.attributes;
                    def.module_namespace = module_namespace;
                    def.module_namespace.insert(
                        def.module_namespace.end(), namespace_path.begin(), namespace_path.end());
                    std::string qualified = generic_key(namespace_path, fn.name);
                    ctx.types.register_generic_func(qualified, def);
                    std::string full_q = full_qualified(module_namespace, namespace_path, fn.name);
                    if (full_q != qualified) {
                        ctx.types.register_generic_func(full_q, def);
                    }
                } else {
                    if (!ctx.symbols.declare(fn)) {
                        ctx.errors.add("Function redeclaration: " + fn.name);
                        return;
                    }
                }
            },
            [&](const ast::StructDecl& str) {
                if (!str.type_params.empty()) {
                    types::GenericStructDef def;
                    def.params = str.type_params;
                    def.fields = str.fields;
                    ctx.types.register_generic_struct(str.name, def);
                    std::string qualified = generic_key(namespace_path, str.name);
                    if (qualified != str.name) {
                        ctx.types.register_generic_struct(qualified, def);
                    }
                    std::string full_q = full_qualified(module_namespace, namespace_path, str.name);
                    if (full_q != qualified && full_q != str.name) {
                        ctx.types.register_generic_struct(full_q, def);
                    }
                } else {
                    if (!ctx.symbols.declare(str)) {
                        ctx.errors.add("Struct redeclaration: " + str.name);
                        return;
                    }
                    // Also register in TypeContext so type_size can find fields cross-module
                    std::vector<std::pair<std::string, const ast::Type*>> fields;
                    for (const auto& f : str.fields) {
                        fields.emplace_back(f.name, f.type);
                    }
                    std::vector<std::vector<ast::Attribute>> field_attrs;
                    for (const auto& f : str.fields) {
                        field_attrs.push_back(f.attributes);
                    }
                    ctx.types.register_struct(str.name, fields, field_attrs);
                    std::string qualified = generic_key(namespace_path, str.name);
                    if (qualified != str.name) {
                        ctx.types.register_struct(qualified, fields, field_attrs);
                    }
                    std::string full_q = full_qualified(module_namespace, namespace_path, str.name);
                    if (full_q != qualified && full_q != str.name) {
                        ctx.types.register_struct(full_q, fields, field_attrs);
                    }
                }
            },
            [&](const ast::NamespaceStmt& ns) {
                NamespaceGuard guard(ctx.symbols, ns.name);
                NamespacePathGuard path_guard(namespace_path, ns.name);
                if (ns.body) {
                    collect_declarations(ns.body->stmts);
                }
            },
            [&](const ast::EnumDecl& enm) {
                if (!ctx.symbols.declare_symbol(enm.name, symb_t::Symbol{
                    enm.name,
                    symb_t::EnumSymbol{ enm.variants },
                    enm.attributes
                })) {
                    ctx.errors.add("Enum redeclaration: " + enm.name);
                    return;
                }
                for (size_t i = 0; i < enm.variants.size(); ++i) {
                    ctx.symbols.declare_symbol(enm.variants[i], symb_t::Symbol{
                        enm.variants[i],
                        symb_t::VarSymbol{
                            ctx.types.get_builtin(TypeKind::I32),
                            false, true, static_cast<int64_t>(i)
                        },
                        {}
                    });
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
        [&](const ast::EnumDecl& n) { analyze_enum_decl(n); },
        [&](const ast::NamespaceStmt& n) { analyze_namespace_stmt(n); },
        [&](const ast::ExprStmt& n) { analyze_expr_stmt(n); },
        [&](const ast::ReturnStmt& n) { analyze_return(n); },
        [&](const ast::FuncStmt& n) { analyze_func(n); },
        [&](const ast::IfStmt& n) { analyze_if(n); },
        [&](const ast::WhileStmt& n) { analyze_while(n); },
        [&](const ast::ModuleDecl&) {},
        [&](const ast::LoadStmt&) {},
        [&](const ast::UsingStmt& n) { analyze_using(n); },
        [&](const ast::RegionStmt& n) { analyze_region(n); },
        [&](const auto&) {
            ctx.errors.add(stmt->loc, "Unsupported statement node in semantic analysis");
            return;
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
    if (const auto* ue = std::get_if<ast::UnaryExpr>(&expr->kind)) {
        if (ue->op == ast::UnaryOp::Not) {
            auto val = try_eval_const(ue->operand);
            if (val) return *val == 0 ? 1 : 0;
        }
        return std::nullopt;
    }
    if (const auto* be = std::get_if<ast::BinaryExpr>(&expr->kind)) {
        auto lhs = try_eval_const(be->lhs);
        auto rhs = try_eval_const(be->rhs);
        if (!lhs || !rhs) return std::nullopt;
        switch (be->op) {
            case ast::BinaryOp::Eq:  return *lhs == *rhs ? 1 : 0;
            case ast::BinaryOp::Neq: return *lhs != *rhs ? 1 : 0;
            case ast::BinaryOp::Lt:  return *lhs <  *rhs ? 1 : 0;
            case ast::BinaryOp::Lte: return *lhs <= *rhs ? 1 : 0;
            case ast::BinaryOp::Gt:  return *lhs >  *rhs ? 1 : 0;
            case ast::BinaryOp::Gte: return *lhs >= *rhs ? 1 : 0;
            default: return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<int64_t> SemanticAnalyzer::eval_guard_cond(const ast::Expr* expr, const std::string& struct_name) {
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

    // Resolve field references to their default values in the struct definition
    if (const auto* fe = std::get_if<ast::FieldExpr>(&expr->kind)) {
        const ast::Expr* def_val = ctx.types.get_field_default_value(struct_name, fe->field);
        if (def_val) return eval_guard_cond(def_val, struct_name);
        return std::nullopt;
    }

    // Also resolve bare VarExpr that reference struct fields (e.g. @guard(has_value))
    if (const auto* ve = std::get_if<ast::VarExpr>(&expr->kind)) {
        // First try to resolve as a struct field
        const ast::Expr* def_val = ctx.types.get_field_default_value(struct_name, ve->name);
        if (def_val) return eval_guard_cond(def_val, struct_name);
        // Fallback to regular variable lookup
        return try_eval_const(expr);
    }

    // Unary NOT
    if (const auto* ue = std::get_if<ast::UnaryExpr>(&expr->kind)) {
        if (ue->op == ast::UnaryOp::Not) {
            auto val = eval_guard_cond(ue->operand, struct_name);
            if (val) return *val == 0 ? 1 : 0;
        }
        return std::nullopt;
    }

    // Binary comparisons
    if (const auto* be = std::get_if<ast::BinaryExpr>(&expr->kind)) {
        auto lhs = eval_guard_cond(be->lhs, struct_name);
        auto rhs = eval_guard_cond(be->rhs, struct_name);
        if (!lhs || !rhs) return std::nullopt;
        switch (be->op) {
            case ast::BinaryOp::Eq:  return *lhs == *rhs ? 1 : 0;
            case ast::BinaryOp::Neq: return *lhs != *rhs ? 1 : 0;
            case ast::BinaryOp::Lt:  return *lhs <  *rhs ? 1 : 0;
            case ast::BinaryOp::Lte: return *lhs <= *rhs ? 1 : 0;
            case ast::BinaryOp::Gt:  return *lhs >  *rhs ? 1 : 0;
            case ast::BinaryOp::Gte: return *lhs >= *rhs ? 1 : 0;
            default: return std::nullopt;
        }
    }

    return std::nullopt;
}

void SemanticAnalyzer::analyze_var_decl(const ast::VarDecl& var) {
    if (!var.type) {
        ctx.errors.add("Variable declaration missing type: " + var.name);
        return;
    }

    // Substitute generic type params if in a concrete instantiation context
    const ast::Type* resolved_type = var.type;
    if (current_type_subst && !current_type_subst->empty()) {
        resolved_type = ctx.types.substitute_type(var.type, *current_type_subst);
    }
    resolved_type = canonicalize_struct_type(resolved_type);

    for (const auto& attr : var.attributes) {
        analyze_attribute(attr, attrs::AttributeTarget::Variable);
    }

    bool has_init = false;
    bool has_guard = false;
    for (const auto& attr : var.attributes) {
        if (attr.name == "init"){
            has_init = true;
        }
        else if (attr.name == "guard" && !attr.args.empty()) {
            auto* guard_type = analyze_expr(attr.args[0]);
            if (!guard_type) continue;
            has_guard = true;
        }
    }

    if (var.value) {
        // For standalone struct init (type_ref == nullptr), pass the variable's declared type
        if (auto* si = std::get_if<ast::StructInitExpr>(&var.value->kind)) {
            if (!si->type_ref) {
                // Create a TypeExpr node from the variable's declared type
                si->type_ref = memory::make<ast::Expr>(ctx.ast_arena, ast::TypeExpr{ resolved_type }, var.value->loc);
            }
        }
        const ast::Type* value_type = analyze_expr(var.value);
        if (!value_type) return;

        if (!is_assignable(resolved_type, value_type)) {
            ctx.errors.add(var.value->loc, "Type mismatch in variable initialization: " + var.name);
            return;
        }
    } else if (!var.is_mut && !has_init) {
        ctx.errors.add("Immutable variable must be initialized: " + var.name);
        return;
    }

    // Declare with resolved type
    if (!ctx.symbols.declare_symbol(var.name, symb_t::Symbol{
        var.name,
        symb_t::VarSymbol{resolved_type, var.is_mut, var.value != nullptr},
        var.attributes
    })) {
        ctx.errors.add("Variable already declared: " + var.name);
        return;
    }

    if (has_init && !var.value) {
        ctx.symbols.mark_initialized(var.name);
    }

    if (has_guard) {
        auto* sym = ctx.symbols.lookup(var.name);
        if (sym && std::holds_alternative<symb_t::VarSymbol>(sym->data)) {
            auto& vs = std::get<symb_t::VarSymbol>(sym->data);
            for (const auto& attr : var.attributes) {
                if (attr.name == "guard" && !attr.args.empty()) {
                    vs.guard_cond = attr.args[0];
                    break;
                }
            }
        }
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
            ctx.errors.add("Duplicate field: " + field.name);
            return;
        }

        if (!field.type) {
            ctx.errors.add("Field missing type: " + field.name);
            return;
        }

        for (const auto& attr : field.attributes) {
            analyze_attribute(attr, attrs::AttributeTarget::Field);
        }

        if (field.default_value) {
            const ast::Type* dt = analyze_expr(field.default_value);
            if (!dt) return;

            if (!is_assignable(field.type, dt)) {
                ctx.errors.add("Type mismatch in field default value: " + field.name);
                return;
            }
        }
    }
}

void SemanticAnalyzer::analyze_enum_decl(const ast::EnumDecl& enm) {
    // Enum variants are already declared as VarSymbols in collect_declarations.
    // Here we just validate: variants must be valid identifiers.
    if (enm.variants.empty()) {
        ctx.errors.add("Enum must have at least one variant: " + enm.name);
        return;
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
        ctx.errors.add(ret.value ? ret.value->loc : SourceLocation{}, "Return outside function");
        return;
    }

    if (!value_type) return;

    if (!is_assignable(current_function_return_type, value_type)) {
        ctx.errors.add(ret.value ? ret.value->loc : SourceLocation{}, 0, "Return type mismatch");
        return;
    }
}

void SemanticAnalyzer::analyze_func(const ast::FuncStmt& func) {
    if (func.is_extern && func.body) {
        ctx.errors.add("Extern function cannot have a body: " + func.name);
        return;
    }

    for (const auto& attr : func.attributes) {
        analyze_attribute(attr, attrs::AttributeTarget::Function);
    }

    for (const auto& attr : func.attributes) {
        if (attr.name == "syscall") {
            if (!func.is_extern) {
                ctx.errors.add("@syscall can only be used on extern functions: " + func.name);
                return;
            }
            const auto* num = std::get_if<ast::IntExpr>(&attr.args[0]->kind);
            if (!num) {
                ctx.errors.add("@syscall argument must be an integer literal: " + func.name);
                return;
            }
        }
        if (attr.name == "import") {
            if (!func.is_extern) {
                ctx.errors.add("@import can only be used on extern functions: " + func.name);
                return;
            }
            for (const auto* arg : attr.args) {
                if (!std::get_if<ast::StringExpr>(&arg->kind)) {
                    ctx.errors.add("@import arguments must be string literals: " + func.name);
                    return;
                }
            }
        }
    }

    if (!func.return_type) {
        ctx.errors.add("Function missing return type: " + func.name);
        return;
    }

    for (const auto& arg : func.args) {
        if (!arg.type) {
            ctx.errors.add("Function argument missing type: " + arg.name);
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
            ctx.errors.add("Duplicate function argument: " + arg.name);
            current_function_return_type = prev_return_type;
            return;
        }
    }

    // Declare implicit 'out' variable for struct-returning functions
    if (func.return_type && func.return_type->kind == TypeKind::Struct) {
        ast::VarDecl out_var;
        out_var.name = "out";
        out_var.type = func.return_type;
        out_var.is_mut = true;
        out_var.value = nullptr;
        out_var.attributes = {};
        if (!ctx.symbols.declare(out_var)) {
            ctx.errors.add("Failed to declare implicit 'out' variable");
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
    bool prev = is_in_region;
    is_in_region = true;

    {
        ScopeGuard scope(ctx.symbols);
        if (reg.body) {
            analyze_block(reg.body);
        }
    }

    is_in_region = prev;
}

void SemanticAnalyzer::analyze_using(const ast::UsingStmt& us) {
    auto* ns = ctx.symbols.resolve_namespace(us.path);
    if (!ns) {
        ctx.errors.add("Namespace not found: " + support::join_namespace(us.path));
        return;
    }

    for (const auto& [name, sym] : ns->symbols) {
        ctx.symbols.declare_symbol(name, *sym, true);
    }

    // Copy generic function definitions from the imported namespace
    std::string ns_prefix = support::join_namespace(us.path) + "::";
    for (const auto& [key, def] : ctx.types.get_all_generic_funcs()) {
        if (key.starts_with(ns_prefix) && key.size() > ns_prefix.size()) {
            std::string short_name = key.substr(ns_prefix.size());
            if (!ctx.types.get_generic_func(short_name)) {
                ctx.types.register_generic_func(short_name, def);
            }
        }
    }
}

void SemanticAnalyzer::check_visibility(const symb_t::Symbol& sym, const std::string& context) {
    if (has_attr(sym.attributes, "private") && sym.owning_module != module_namespace) {
        ctx.errors.add("Cannot access private symbol '" + sym.name + "' from " + context);
        return;
    }
}

void SemanticAnalyzer::analyze_attribute(const ast::Attribute& attr, const attrs::AttributeTarget target) {
	auto* it = find_attr(attr.name);

	if(it == nullptr){
		ctx.errors.add("Attribute: '@" + attr.name + "' not found");
		return;
	}
	if(!has_flag(it->targets, target)){
		ctx.errors.add("Attribute: '@" + attr.name + "' has incorrect targets: " + attr_target_to_string(target)
				+ ". Expected: " + attr_target_to_string(it->targets));
		return;
	}

	if(attr.args.size() > it->max_args){
		ctx.errors.add("Expected '" + std::to_string(it->max_args)
                                  + "' count of args for '@" + attr.name + "' attribute."
                                  + "Got: '" + std::to_string(attr.args.size()) );
		return;
	}
	else if(attr.args.size() < it->min_args){
		ctx.errors.add("Expected '" + std::to_string(it->min_args)
				+ "' count of args for '@" + attr.name + "' attribute."
				+ "Got: '" + std::to_string(attr.args.size()) );
		return;
	}
}

const ast::Type* SemanticAnalyzer::analyze_expr(ast::Expr* expr) {
    if (!expr) return nullptr;

    const ast::Type* ty = std::visit(overloaded{
        [&](const ast::IntExpr& n) -> const ast::Type* {
            if (n.value >= INT32_MIN && n.value <= INT32_MAX) {
                return ctx.types.get_builtin(TypeKind::I32);
            }
            return ctx.types.get_builtin(TypeKind::I64);
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
            return analyze_var(n, expr);
        },
        [&](const ast::AssignExpr& n) -> const ast::Type* {
            return analyze_assign(n);
        },
        [&](const ast::BinaryExpr& n) -> const ast::Type* {
            return analyze_binary(n, expr);
        },
        [&](const ast::UnaryExpr& n) -> const ast::Type* {
            return analyze_unary(n);
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
            ctx.errors.add("Type used as value");
            return nullptr;
        },
        [&](const ast::IndexExpr& n) -> const ast::Type* {
            return analyze_index(n);
        },
        [&](const ast::StructInitExpr& n) -> const ast::Type* {
            return analyze_struct_init(n);
        },
        [&](const ast::SizeofExpr& n) -> const ast::Type* {
            return analyze_sizeof(n);
        },
        [&](const auto&) -> const ast::Type* {
            ctx.errors.add("Unsupported expression node in semantic analysis");
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

const ast::Type* SemanticAnalyzer::analyze_var(const ast::VarExpr& var, const ast::Expr* expr) {
    auto* sym = ctx.symbols.lookup(var.name);

    if (!sym) {
        ctx.errors.add(expr->loc, "Undefined variable: " + var.name);
        return nullptr;
    }

    check_visibility(*sym, module_namespace.empty() ? "::" : support::join_namespace(module_namespace));

    const ast::Type* type = symbol_type(*sym);
    if (!type) {
        ctx.errors.add(expr->loc, "Symbol is not a value: " + var.name);
        return nullptr;
    }

    if (!symbol_is_initialized(*sym)) {
        ctx.errors.add(expr->loc, "Use of uninitialized variable: " + var.name);
        return nullptr;
    }

    return type;
}

const ast::Type* SemanticAnalyzer::resolve_lvalue(const ast::Expr* expr) {
    if (!expr) {
        ctx.errors.add("Invalid lvalue");
        return nullptr;
    }

    if (const auto* var = std::get_if<ast::VarExpr>(&expr->kind)) {
        auto* sym = ctx.symbols.lookup(var->name);
        if (!sym) {
            // Check for implicit 'out' struct field access
            if (current_function_return_type && current_function_return_type->kind == TypeKind::Struct) {
                if (!current_function_return_type->type_args.empty()) {
                    ctx.types.try_instantiate(current_function_return_type->struct_name, current_function_return_type->type_args);
                }
                const ast::Type* field_type = ctx.types.get_field_type(current_function_return_type->struct_name, var->name);
                if (field_type) {
                    return field_type;
                }
            }
            ctx.errors.add("Undefined variable: " + var->name);
            return nullptr;
        }

        check_visibility(*sym, module_namespace.empty() ? "::" : support::join_namespace(module_namespace));

        if (!symbol_is_mutable(*sym)) {
            ctx.errors.add("Cannot assign to immutable variable: " + var->name);
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

        // Auto-deref references
        if (base_type->kind == TypeKind::Reference && base_type->pointed) {
            base_type = base_type->pointed;
        }

        if (base_type->kind != TypeKind::Pointer) {
            ctx.errors.add("Cannot index non-pointer type");
            return nullptr;
        }

        const ast::Type* elem_type = base_type->pointed;
        if (!elem_type) {
            ctx.errors.add("Invalid pointer target");
            return nullptr;
        }

        const ast::Type* index_type = analyze_expr(index->index);
        if (!index_type) return nullptr;

        if (index_type->kind == TypeKind::Void) {
            ctx.errors.add("Index must be an integer");
            return nullptr;
        }

        return elem_type;
    }

    ctx.errors.add("Invalid lvalue");
    return nullptr;
}

const ast::Type* SemanticAnalyzer::analyze_assign(const ast::AssignExpr& asg) {
    const ast::Type* value_type = analyze_expr(asg.value);
    if (!value_type) return nullptr;

    if (!asg.target) {
        ctx.errors.add("Assignment target is missing");
        return nullptr;
    }

    const ast::Type* target_type = resolve_lvalue(asg.target);
    if (!target_type) return nullptr;

    if (!is_assignable(target_type, value_type)) {
        std::string tname = target_type ? (target_type->kind == TypeKind::Struct ? target_type->struct_name : (target_type->kind == TypeKind::Generic ? "Generic:" + target_type->struct_name : std::to_string((int)target_type->kind))) : "null";
        std::string vname = value_type ? (value_type->kind == TypeKind::Struct ? value_type->struct_name : (value_type->kind == TypeKind::Generic ? "Generic:" + value_type->struct_name : std::to_string((int)value_type->kind))) : "null";
        ctx.errors.add("Type mismatch in assignment: " + tname + " vs " + vname);
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

    // Compile-time guard check for struct fields with @guard
    if (base->kind == ast::TypeKind::Struct) {
        auto* s_sym = lookup_struct(ctx, base->struct_name);
        if (s_sym) {
            auto* ss = std::get_if<symb_t::StructSymbol>(&s_sym->data);
            if (ss) {
                for (size_t i = 0; i < ss->field_names.size(); ++i) {
                    if (ss->field_names[i] != node.field) continue;
                    for (const auto& fa : ss->field_attributes[i]) {
                        if (fa.name == "guard" && !fa.args.empty()) {
                            auto val = eval_guard_cond(fa.args[0], base->struct_name);
                            if (val && *val == 0) {
                                ctx.errors.add("guard failed for field '" + node.field + "'");
                                return nullptr;
                            }
                        }
                    }
                    break;
                }
            }
        }
    }

    return resolve_struct_field(ctx, base, node.field);
}

namespace {
    bool is_builtin_type_kind(ast::TypeKind k) {
        return k != ast::TypeKind::Struct && k != ast::TypeKind::Generic;
    }
}

const ast::Type* SemanticAnalyzer::analyze_binary(const ast::BinaryExpr& b, const ast::Expr* expr) {
    const ast::Type* l = analyze_expr(b.lhs);
    const ast::Type* r = analyze_expr(b.rhs);

    if (!l || !r) return nullptr;

    if (is_builtin_type_kind(l->kind) && is_builtin_type_kind(r->kind)) {
        if (!types_equal(l, r)) {
            ctx.errors.add(expr->loc, "Type mismatch in binary expression");
            return nullptr;
        }
        switch (b.op) {
            case ast::BinaryOp::Eq:
            case ast::BinaryOp::Neq:
            case ast::BinaryOp::Lt:
            case ast::BinaryOp::Lte:
            case ast::BinaryOp::Gt:
            case ast::BinaryOp::Gte:
                return ctx.types.get_builtin(TypeKind::Bool);
            default:
                return l;
        }
    }

    ctx.errors.add("Operator overloading is not supported for these types");
    return nullptr;
}

const ast::Type* SemanticAnalyzer::analyze_unary(const ast::UnaryExpr& u){
    const ast::Type* operand = analyze_expr(u.operand);
    if (!operand) return nullptr;

    if (u.op == ast::UnaryOp::AddrOf) {
        if (operand->kind == TypeKind::Bool ||
            (operand->kind >= TypeKind::I8 && operand->kind <= TypeKind::F64)) {
            ctx.errors.add("Cannot take address of a value type");
            return nullptr;
        }
        return ctx.types.get_reference(operand);
    }

    if (is_builtin_type_kind(operand->kind)) {
        if (u.op == ast::UnaryOp::Not) {
            if (operand->kind != TypeKind::Bool && operand->kind != TypeKind::U32) {
                ctx.errors.add("Unary '!' not supported for this type");
                return nullptr;
            }
        } else {
            if (!(operand->kind >= TypeKind::I8 && operand->kind <= TypeKind::F64)) {
                ctx.errors.add("Unary '-' not supported for this type");
                return nullptr;
            }
        }
        return operand;
    }

    ctx.errors.add("Operator overloading is not supported for this type");
    return nullptr;
}

void SemanticAnalyzer::check_arg_guard(const ast::Expr* arg, const std::string& call_name) {
    if (!arg) return;

    if (const auto* ve = std::get_if<ast::VarExpr>(&arg->kind)) {
        auto* sym = ctx.symbols.lookup(ve->name);
        if (!sym) return;
        auto* vs = std::get_if<symb_t::VarSymbol>(&sym->data);
        if (!vs || !vs->guard_cond) return;
        auto val = try_eval_const(vs->guard_cond);
        if (val && *val == 0) {
            ctx.errors.add("guard failed for '" + ve->name + "' in call to '" + call_name + "'");
            return;
        }
        return;
    }

    if (const auto* fe = std::get_if<ast::FieldExpr>(&arg->kind)) {
        const ast::Type* base_type = fe->base ? fe->base->resolved_type : nullptr;

        if (!base_type || base_type->kind != ast::TypeKind::Struct) return;

        auto* s_sym = lookup_struct(ctx, base_type->struct_name);
        if (!s_sym) return;

        auto* ss = std::get_if<symb_t::StructSymbol>(&s_sym->data);
        if (!ss) return;

        for (size_t i = 0; i < ss->field_names.size(); ++i) {
            if (ss->field_names[i] != fe->field) continue;
            for (const auto& fa : ss->field_attributes[i]) {
                if (fa.name == "guard" && !fa.args.empty()) {
                    auto val = eval_guard_cond(fa.args[0], base_type->struct_name);
                    if (val && *val == 0) {
                        ctx.errors.add("guard failed for field '" + fe->field + "' in call to '" + call_name + "'");
                        return;
                    }
                }
            }
            break;
        }
    }
}

const ast::Type* SemanticAnalyzer::analyze_call(const ast::CallExpr& call) {
    if (!call.callee) {
        ctx.errors.add("Call callee is missing");
        return nullptr;
    }

    auto path = support::flatten_path(call.callee);

    if (is_in_region && path.size() == 1 && path[0] == "alloc") {
        if (call.args.size() == 2) {
            // alloc(T, count) - typed allocation
            const auto* type_expr = std::get_if<ast::TypeExpr>(&call.args[0]->kind);
            if (!type_expr) {
                ctx.errors.add("First argument to alloc must be a type, e.g. alloc(i32, 10)");
                return nullptr;
            }
            const ast::Type* elem_type = type_expr->type;
            if (!elem_type) {
                ctx.errors.add("Invalid element type in alloc");
                return nullptr;
            }
            call.args[0]->resolved_type = elem_type;

            const ast::Type* count_type = analyze_expr(call.args[1]);
            if (!count_type) return nullptr;
            if (count_type->kind == TypeKind::Void) {
                ctx.errors.add("alloc count must be an integer");
                return nullptr;
            }

            return ctx.types.get_pointer(elem_type);
        }

        if (call.args.size() != 1) {
            ctx.errors.add("alloc takes 1 argument (bytes) or 2 arguments (type, count)");
            return nullptr;
        }
        const ast::Type* size_type = analyze_expr(call.args[0]);
        if (!size_type) return nullptr;

        if (size_type->kind == TypeKind::Void || size_type->kind == TypeKind::String) {
            ctx.errors.add("alloc argument must be an integer (size in bytes)");
            return nullptr;
        }

        return ctx.types.get_pointer(ctx.types.get_builtin(TypeKind::Void));
    }

    std::string func_name = path.back();
    std::string qualified_func_name = support::join_namespace(path);

    // Check if this is a generic function call
    const auto* generic_def = ctx.types.get_generic_func(qualified_func_name);
    if (generic_def && !call.type_args.empty()) {
        // Build substitution map: param name -> concrete type
        std::unordered_map<std::string, const Type*> subst;
        if (call.type_args.size() != generic_def->params.size()) {
            ctx.errors.add("Type argument count mismatch for generic function: " + qualified_func_name);
            return nullptr;
        }
        for (size_t i = 0; i < generic_def->params.size(); ++i) {
            subst[generic_def->params[i]] = call.type_args[i];
        }

        // Mangle the concrete function name
        std::string mangled = ctx.types.mangle_func_name(qualified_func_name, call.type_args);

        // Check if already instantiated
        auto* concrete_sym = ctx.symbols.lookup(mangled);
        if (!concrete_sym) {
            // Extract function's namespace for qualifying struct types
            std::vector<std::string> func_ns;
            {
                std::string qn = qualified_func_name;
                auto colon = qn.rfind("::");
                if (colon != std::string::npos) {
                    std::string ns = qn.substr(0, colon);
                    func_ns = support::split_path(ns);
                }
            }

            auto qualify_struct = [&](const Type* type) -> const Type* {
                if (type && type->kind == TypeKind::Struct) {
                    std::vector<const Type*> concrete_type_args;
                    for (const auto* arg : type->type_args) {
                        concrete_type_args.push_back(ctx.types.substitute_type(arg, subst));
                    }
                    std::string base_name = type->struct_name;
                    std::string unmangled;
                    if (ctx.types.is_mangled_name(base_name, unmangled)) {
                        base_name = unmangled;
                    }
                    // Qualify the struct name with the generic function's module
                    // namespace so types defined in the module (e.g. cmp::ordering)
                    // match the caller's qualified references.
                    std::string qualified_base = base_name;
                    if (!func_ns.empty() && base_name.find("::") == std::string::npos) {
                        qualified_base = full_qualified(func_ns, {}, base_name);
                    }
                    return ctx.types.get_deferred_generic(qualified_base, concrete_type_args);
                }
                return ctx.types.substitute_type(type, subst);
            };

            // Create concrete arg types by substitution
            std::vector<const Type*> concrete_arg_types;
            std::vector<ast::FuncArg> concrete_args;
            for (const auto& arg : generic_def->args) {
                concrete_arg_types.push_back(qualify_struct(arg.type));
                concrete_args.push_back(ctx.types.substitute_func_arg(arg, subst));
            }
            const Type* concrete_return = qualify_struct(generic_def->return_type);

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
            ctx.generic_return_types[mangled] = concrete_return;

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

            // The generic body must be analyzed in its defining module's
            // namespace so unqualified references to sibling module
            // declarations (funcs, structs, globals) resolve correctly.
            // Walk from the global namespace (not from the caller's
            // current namespace, which may nest the same names differently).
            // Prefer the generic definition's module namespace (the
            // caller's written path may be unqualified, e.g. a same-file
            // generic), falling back to the call path minus the name.
            std::vector<std::string> func_module_ns = generic_def->module_namespace;
            if (func_module_ns.empty()) {
                func_module_ns.assign(path.begin(), path.end() - 1);
            }

            // Analyze the concrete function body (type-check with concrete types)
            if (concrete_fn_stmt.body) {
                auto* prev_subst = current_type_subst;
                current_type_subst = &subst;
                auto* saved_ns = ctx.symbols.get_current_namespace();
                ctx.symbols.set_current_namespace(ctx.symbols.create_namespace_path(func_module_ns));
                analyze_func(concrete_fn_stmt);
                ctx.symbols.set_current_namespace(saved_ns);
                current_type_subst = prev_subst;
            }

            // Store for IR generation
            ctx.generic_instantiations.push_back(decltype(ctx.generic_instantiations)::value_type{
                std::move(concrete_fn_stmt),
                func_module_ns
            });

            concrete_sym = ctx.symbols.lookup(mangled);
        }

        if (!concrete_sym) {
            ctx.errors.add("Failed to instantiate generic function: " + func_name);
            return nullptr;
        }

        auto* concrete_fn = std::get_if<symb_t::FuncSymbol>(&concrete_sym->data);
        if (!concrete_fn) {
            ctx.errors.add("Invalid concrete function symbol: " + mangled);
            return nullptr;
        }

        // Check argument count and types
        if (concrete_fn->arg_types.size() != call.args.size()) {
            ctx.errors.add("Argument count mismatch in generic call: " + func_name);
            return nullptr;
        }

        for (size_t i = 0; i < call.args.size(); ++i) {
            ast::Expr* arg = call.args[i];
            const ast::Type* arg_type = arg ? analyze_expr(arg) : nullptr;
            if (!arg_type) return nullptr;

            check_arg_guard(arg, mangled);

            if (!is_assignable(concrete_fn->arg_types[i], arg_type)) {
                ctx.errors.add("Argument type mismatch in generic call: " + func_name);
                return nullptr;
            }
        }

        return concrete_fn->return_type;
    }

    // Non-generic function lookup
    auto* sym = resolve_qualified(ctx.symbols, path);
    if (!sym) {
        ctx.errors.add("Undefined function: " + support::join_namespace(path));
        return nullptr;
    }

    check_visibility(*sym, support::join_namespace(path));

    auto* fn = std::get_if<symb_t::FuncSymbol>(&sym->data);
    if (!fn) {
        ctx.errors.add("Callee is not a function: " + support::join_namespace(path));
        return nullptr;
    }

    if (fn->arg_types.size() != call.args.size()) {
        ctx.errors.add("Argument count mismatch in call: " + support::join_namespace(path));
        return nullptr;
    }

    for (size_t i = 0; i < call.args.size(); ++i) {
        ast::Expr* arg = call.args[i];
        const ast::Type* arg_type = arg ? analyze_expr(arg) : nullptr;
        if (!arg_type) return nullptr;

        check_arg_guard(arg, support::join_namespace(path));

        if (!is_assignable(fn->arg_types[i], arg_type)) {
            ctx.errors.add("Argument type mismatch in call: " + support::join_namespace(path));
            return nullptr;
        }
    }

    return fn->return_type;
}

const ast::Type* SemanticAnalyzer::analyze_index(const ast::IndexExpr& n) {
    const ast::Type* base_type = analyze_expr(n.base);
    if (!base_type) return nullptr;

    // Auto-deref references
    if (base_type->kind == TypeKind::Reference && base_type->pointed) {
        base_type = base_type->pointed;
    }

    if (base_type->kind != TypeKind::Pointer) {
        ctx.errors.add("Cannot index non-pointer type");
        return nullptr;
    }

    const ast::Type* elem_type = base_type->pointed;
    if (!elem_type) {
        ctx.errors.add("Invalid pointer target");
        return nullptr;
    }

    const ast::Type* index_type = analyze_expr(n.index);
    if (!index_type) return nullptr;

    if (index_type->kind == TypeKind::Void) {
        ctx.errors.add("Index must be an integer");
        return nullptr;
    }

    return elem_type;
}

const ast::Type* SemanticAnalyzer::analyze_struct_init(const ast::StructInitExpr& node) {
    const ast::Type* struct_type = nullptr;

    if (node.type_ref) {
        if (auto* var = std::get_if<ast::VarExpr>(&node.type_ref->kind)) {
            if (!node.type_args.empty()) {
                struct_type = ctx.types.get_generic_instantiation(var->name, node.type_args);
            }
            if (!struct_type) {
                struct_type = ctx.types.get_struct(var->name);
            }
        } else if (auto* ns = std::get_if<ast::NamespaceExpr>(&node.type_ref->kind)) {
            auto expr = ast::Expr{*ns};
            auto path = support::flatten_path(&expr);
            std::string full_name = support::join_namespace(path);
            struct_type = ctx.types.get_struct(full_name);
        } else if (auto* te = std::get_if<ast::TypeExpr>(&node.type_ref->kind)) {
            struct_type = te->type;
        } else {
            ctx.errors.add(node.type_ref->loc, "Invalid type in struct initialization");
            return nullptr;
        }
    } else {
        ctx.errors.add("Cannot infer struct type from context. Use Type{...} syntax.");
        return nullptr;
    }

    if (!struct_type) {
        ctx.errors.add(node.type_ref->loc, "Unknown struct type");
        return nullptr;
    }

    if (struct_type->kind != TypeKind::Struct) {
        ctx.errors.add(node.type_ref->loc, "Type is not a struct");
        return nullptr;
    }

    // Resolve generic struct if needed
    if (!struct_type->type_args.empty()) {
        if (ctx.types.try_instantiate(struct_type->struct_name, struct_type->type_args)) {
            const auto* fields = ctx.types.get_struct_fields(struct_type->struct_name);
            if (fields) {
                const auto* field_attrs = ctx.types.get_struct_field_attrs(struct_type->struct_name);
                ctx.symbols.declare_struct_global(
                    struct_type->struct_name, *fields, {},
                    field_attrs ? *field_attrs : std::vector<std::vector<ast::Attribute>>{}
                );
            }
        }
    }

    // Look up struct symbol
    auto* sym = lookup_struct(ctx, struct_type->struct_name);
    if (!sym) {
        ctx.errors.add(node.type_ref->loc, "Unknown struct: " + struct_type->struct_name);
        return nullptr;
    }

    auto* ss = std::get_if<symb_t::StructSymbol>(&sym->data);
    if (!ss) {
        ctx.errors.add(node.type_ref->loc, "Invalid struct symbol: " + struct_type->struct_name);
        return nullptr;
    }

    // Validate argument count
    if (node.args.size() != ss->field_names.size()) {
        ctx.errors.add(node.type_ref->loc,
            "Struct init argument count mismatch for " + struct_type->struct_name +
            ": expected " + std::to_string(ss->field_names.size()) +
            ", got " + std::to_string(node.args.size()));
        return nullptr;
    }

    // Validate argument types
    for (size_t i = 0; i < node.args.size(); ++i) {
        const ast::Type* arg_type = analyze_expr(node.args[i]);
        if (!arg_type) return nullptr;

        if (!is_assignable(ss->field_types[i], arg_type)) {
            ctx.errors.add(node.args[i]->loc,
                "Type mismatch in struct init field '" + ss->field_names[i] + "'");
            return nullptr;
        }
    }

    return struct_type;
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
    auto* sym = resolve_qualified(ctx.symbols, path);

    if (!sym) {
        ctx.errors.add("Undefined qualified symbol");
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

    ctx.errors.add("Qualified path is not a value");
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
        case ast::TypeKind::Reference: return 8;
        default: return 0;
    }
}

const ast::Type* SemanticAnalyzer::analyze_cast(const ast::CastExpr& n){
    const ast::Type* value_type = analyze_expr(n.value);
    if(!value_type) return nullptr;

    const ast::Type* target = n.target;
    if (!target) {
        ctx.errors.add("Cast target type is missing");
        return nullptr;
    }
    // Substitute generic type params when inside a concrete instantiation
    if (current_type_subst && !current_type_subst->empty()) {
        target = ctx.types.substitute_type(target, *current_type_subst);
    }
    switch (n.kind) {
        case ast::CastKind::ValueCast:
            if (target->kind == TypeKind::String) {
                if (!is_numeric(value_type->kind)) {
                    ctx.errors.add("as: only numeric types can be converted to string");
                    return nullptr;
                }
            } else if (!is_numeric(value_type->kind) || !is_numeric(target->kind)) {
                ctx.errors.add("as: only numeric type conversions are allowed");
                return nullptr;
            }
            break;
        case ast::CastKind::Bitcast:
            if (value_type->kind == TypeKind::Pointer || target->kind == TypeKind::Pointer ||
                value_type->kind == TypeKind::Reference || target->kind == TypeKind::Reference) {
            } else if (type_size(value_type) != type_size(target)) {
                ctx.errors.add("as!: types must have the same size");
                return nullptr;
            }
            break;
    }
    return target;
}

const ast::Type* SemanticAnalyzer::analyze_sizeof(const ast::SizeofExpr& n) {
    const ast::Type* type = n.type;
    if (!type) {
        ctx.errors.add("sizeof: missing type");
        return nullptr;
    }
    if (current_type_subst && !current_type_subst->empty()) {
        type = ctx.types.substitute_type(type, *current_type_subst);
    }
    int sz = ctx.types.type_size(type);
    if (sz <= 0) {
        ctx.errors.add("sizeof: unsupported type or zero size");
        return nullptr;
    }
    // Update the AST node with the resolved type so IR gen uses the concrete type
    const_cast<ast::SizeofExpr&>(n).type = type;
    return ctx.types.get_builtin(TypeKind::U64);
}

} // namespace quark::sm
