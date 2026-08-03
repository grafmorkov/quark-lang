#include <functional>
#include <utility>
#include <variant>

#include "quark/ir/ir_gen.h"
#include "quark/attributes/attributes.h"
#include "quark/support/symbol_path.h"
#include "utils/logger.h"

using namespace utils::logger;

namespace quark::codegen {

namespace {

symb_t::Symbol* resolve_qualified(CompilerContext& ctx, const std::vector<std::string>& path) {
    if (path.empty()) return nullptr;
    if (path.size() == 1) return ctx.symbols.lookup(path[0]);

    auto* ns = ctx.symbols.get_current_namespace();
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
        auto* first_sym = ctx.symbols.lookup(path[0]);
        if (first_sym && std::holds_alternative<quark::symb_t::EnumSymbol>(first_sym->data)) {
            return ctx.symbols.lookup(path.back());
        }
    }

    return ctx.symbols.lookup(support::join_namespace(path));
};

auto lookup_struct = [](CompilerContext& ctx, const std::string& struct_name) -> symb_t::Symbol* {
    return resolve_qualified(ctx, support::split_path(struct_name));
};

int type_size(const ast::Type* t, CompilerContext* ctx = nullptr) {
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
        case ast::TypeKind::Struct: {
            if (!ctx) return 0;
            auto* sym = lookup_struct(*ctx, t->struct_name);
            if (!sym && !t->type_args.empty()) {
                if (ctx->types.try_instantiate(t->struct_name, t->type_args)) {
                    const auto* fields = ctx->types.get_struct_fields(t->struct_name);
                    if (fields) {
                        const auto* field_attrs = ctx->types.get_struct_field_attrs(t->struct_name);
                        ctx->symbols.declare_struct_global(
                            t->struct_name, *fields, {},
                            field_attrs ? *field_attrs : std::vector<std::vector<ast::Attribute>>{}
                        );
                        sym = lookup_struct(*ctx, t->struct_name);
                    }
                }
            }
            if (!sym) {
                // Fallback: check type context for imported structs
                const auto* fields = ctx->types.get_struct_fields(t->struct_name);
                if (fields) {
                    int total = 0;
                    for (size_t i = 0; i < fields->size(); ++i) {
                        total += 8;
                    }
                    return total;
                }
                return 0;
            }
            auto* ss = std::get_if<quark::symb_t::StructSymbol>(&sym->data);
            if (!ss) return 0;
            int total = 0;
            for (size_t i = 0; i < ss->field_names.size(); ++i) {
                total += 8;  // each field occupies 8 bytes (qword)
            }
            return total;
        }
        default: return 0;
    }
}


template<class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

bool is_declaration_stmt(const ast::Stmt& stmt) {
    return std::holds_alternative<ast::FuncStmt>(stmt.kind) ||
           std::holds_alternative<ast::NamespaceStmt>(stmt.kind) ||
           std::holds_alternative<ast::StructDecl>(stmt.kind) ||
           std::holds_alternative<ast::EnumDecl>(stmt.kind) ||
           std::holds_alternative<ast::UsingStmt>(stmt.kind) ||
           std::holds_alternative<ast::VarDecl>(stmt.kind);
}

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

std::pair<uint32_t, const ast::Type*> resolve_struct_field(
    CompilerContext& ctx,
    const ast::Type* base_type,
    const std::string& field_name
) {
    if (!base_type) {
        ctx.errors.add("Field access base type is null"); return {};
    }

    // Auto-deref references
    if (base_type->kind == ast::TypeKind::Reference && base_type->pointed) {
        base_type = base_type->pointed;
    }

    if (base_type->kind != ast::TypeKind::Struct) {
        ctx.errors.add("Field access on non-struct type"); return {};
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
        ctx.errors.add("Unknown struct: " + base_type->struct_name); return {};
    }

    auto* ss = std::get_if<quark::symb_t::StructSymbol>(&sym->data);
    if (!ss) {
        ctx.errors.add("Invalid struct symbol: " + base_type->struct_name); return {};
    }

    for (size_t i = 0; i < ss->field_names.size(); ++i) {
        if (ss->field_names[i] == field_name) {
            return {
                static_cast<uint32_t>(i * 8u),
                ss->field_types[i]
            };
        }
    }

    ctx.errors.add("Unknown field: " + field_name); return {};
}


} // namespace

// For runtime attributes. (Now there is no runtime attributes)
void IRGenerator::emit_attr_lowering(const std::string& var_name) {
    (void)var_name;
}

void IRGenerator::emit_attr_lowering(const std::string& var_name, const std::vector<ast::Attribute>& attrs) {
    (void)var_name;
    (void)attrs;
}

IRGenerator::IRGenerator(CompilerContext& c)
    : ctx(c) {}

uint32_t IRGenerator::new_reg() {
    return next_reg++;
}

uint32_t IRGenerator::new_label() {
    return next_label++;
}

uint32_t IRGenerator::new_local() {
    return next_local++;
}

void IRGenerator::emit(const IRInst& inst) {
    if (!current_func) {
        ctx.errors.add("No current IR function set"); return;
    }

    current_func->body.push_back(inst);
}

IRBinaryOp IRGenerator::map_op(ast::BinaryOp op) {
    switch (op) {
        case ast::BinaryOp::Add:  return IRBinaryOp::Add;
        case ast::BinaryOp::Sub:  return IRBinaryOp::Sub;
        case ast::BinaryOp::Mul:  return IRBinaryOp::Mul;
        case ast::BinaryOp::Div:  return IRBinaryOp::Div;
        case ast::BinaryOp::Eq:   return IRBinaryOp::Eq;
        case ast::BinaryOp::Neq:  return IRBinaryOp::NotEq;
        case ast::BinaryOp::Lt:   return IRBinaryOp::Lt;
        case ast::BinaryOp::Lte:  return IRBinaryOp::Lte;
        case ast::BinaryOp::Gt:   return IRBinaryOp::Gt;
        case ast::BinaryOp::Gte:  return IRBinaryOp::Gte;
        case ast::BinaryOp::BitAnd:   return IRBinaryOp::BitAnd;
        case ast::BinaryOp::BitOr:    return IRBinaryOp::BitOr;
        case ast::BinaryOp::LogicAnd: return IRBinaryOp::LogicAnd;
        case ast::BinaryOp::LogicOr:  return IRBinaryOp::LogicOr;
        default:
            ctx.errors.add("Unsupported binary op"); return IRBinaryOp::Add;
    }
}

void IRGenerator::gen_program(std::span<quark::modules::Module* const> modules) {
    program.functions.clear();
    program.strings.clear();
    program.globals.clear();
    function_ids.clear();
    global_ids.clear();
    namespace_stack.clear();
    local_scopes.clear();
    type_scopes.clear();
    local_var_attrs.clear();

    next_reg = 0;
    next_label = 0;
    next_local = 0;
    current_terminated = false;
    current_func = nullptr;
    current_module = nullptr;

    auto register_function = [&](const std::string& qname) -> uint32_t {
        auto it = function_ids.find(qname);
        if (it != function_ids.end()) {
            return it->second;
        }

        const uint32_t id = static_cast<uint32_t>(program.functions.size());
        function_ids.emplace(qname, id);

        IRFunction fn;
        fn.id = id;
        fn.name = qname;
        program.functions.push_back(std::move(fn));

        return id;
    };

    auto collect_functions_in_stmts =
        [&](auto&& self,
            const std::vector<ast::Stmt*>& stmts,
            const modules::Module* module) -> void
    {
        for (const auto* stmt : stmts) {
            if (!stmt) continue;

            std::visit(overloaded{
                [&](const ast::FuncStmt& fn) {
                    register_function(
                        support::qualify_name(
                            module->namespace_path,
                            namespace_stack,
                            fn.name
                        )
                    );
                },
                [&](const ast::NamespaceStmt& ns) {
                    namespace_stack.push_back(ns.name);
                    if (ns.body) {
                        self(self, ns.body->stmts, module);
                    }
                    namespace_stack.pop_back();
                },
                [&](const auto&) {}
            }, stmt->kind);
        }
    };

    for (auto* mod : modules) {
        if (!mod) continue;
        collect_functions_in_stmts(collect_functions_in_stmts, mod->ast, mod);
    }

    // Register concrete generic function instantiations
    for (const auto& inst : ctx.generic_instantiations) {
        register_function(inst.stmt.name);
    }

    auto register_global = [&](const std::string& qname, const ast::Type* type) -> uint32_t {
        auto it = global_ids.find(qname);
        if (it != global_ids.end()) {
            return it->second;
        }

        uint32_t sz = type_size(type, &ctx);
        if (sz == 0) sz = 8;
        if (sz < 8) sz = 8;

        const uint32_t id = static_cast<uint32_t>(program.globals.size());
        global_ids.emplace(qname, id);

        IRGlobal g;
        g.name = qname;
        g.size = sz;
        program.globals.push_back(std::move(g));

        return id;
    };

    auto collect_globals_in_stmts =
        [&](auto&& self,
            const std::vector<ast::Stmt*>& stmts,
            const modules::Module* module) -> void
    {
        for (const auto* stmt : stmts) {
            if (!stmt) continue;

            std::visit(overloaded{
                [&](const ast::VarDecl& v) {
                    auto* sym = ctx.symbols.lookup(v.name);
                    if (!sym) {
                        return;
                    }
                    auto* vs = std::get_if<quark::symb_t::VarSymbol>(&sym->data);
                    if (!vs) {
                        return;
                    }

                    const std::string qname = support::qualify_name(
                        sym->owning_module, {}, v.name
                    );
                    register_global(qname, vs->type);
                },
                [&](const ast::NamespaceStmt& ns) {
                    namespace_stack.push_back(ns.name);
                    if (ns.body) {
                        self(self, ns.body->stmts, module);
                    }
                    namespace_stack.pop_back();
                },
                [&](const auto&) {}
            }, stmt->kind);
        }
    };

    for (auto* mod : modules) {
        if (!mod) continue;
        for (const auto& part : mod->namespace_path) {
            ctx.symbols.enter_namespace(part);
        }
        collect_globals_in_stmts(collect_globals_in_stmts, mod->ast, mod);
        for (size_t i = 0; i < mod->namespace_path.size(); ++i) {
            ctx.symbols.exit_namespace();
        }
    }

    for (auto* mod : modules) {
        if (!mod) continue;
        gen_module(*mod);
    }

    // Generate IR for concrete generic instantiations
    for (const auto& inst : ctx.generic_instantiations) {
        const auto& fn = inst.stmt;
        auto it = function_ids.find(fn.name);
        if (it == function_ids.end()) {
            ctx.errors.add("Generic instantiation not registered: " + fn.name); return;
        }

        IRFunction* saved_func = current_func;
        uint32_t saved_next_reg = next_reg;
        uint32_t saved_next_local = next_local;
        bool saved_terminated = current_terminated;
        auto saved_locals = local_scopes;
        auto saved_types = type_scopes;
        auto saved_generic_ns = generic_module_ns;

        current_func = &program.functions[it->second];
        current_func->body.clear();
        current_func->local_count = 0;
        current_func->temp_count = 0;
        current_func->extra_stack = 0;
        current_func->is_extern = fn.is_extern;

        current_func->is_entry = false;
        current_func_return_type = fn.return_type;

        // Sret: return struct via hidden pointer arg
        const bool is_sret = fn.return_type &&
                             fn.return_type->kind == ast::TypeKind::Struct;
        current_func->sret = is_sret;
        current_func->arg_count = static_cast<uint32_t>(fn.args.size()) + (is_sret ? 1 : 0);

    next_reg = 0;
    next_local = 0;
    current_terminated = false;

        local_scopes.clear();
        type_scopes.clear();
        local_scopes.emplace_back();
        type_scopes.emplace_back();

        if (is_sret) {
            const uint32_t sret_local = new_local();
            local_scopes.back()["__sret_ptr"] = sret_local;
            type_scopes.back()["__sret_ptr"] = nullptr;
        }

        for (const auto& arg : fn.args) {
            const uint32_t local = new_local();
            local_scopes.back()[arg.name] = local;
            type_scopes.back()[arg.name] = arg.type;
        }

        if (fn.is_extern) {
            current_func = saved_func;
            next_reg = saved_next_reg;
            next_local = saved_next_local;
            current_terminated = saved_terminated;
            local_scopes = std::move(saved_locals);
            type_scopes = std::move(saved_types);
            continue;
        }

        if (fn.body) {
            // Resolve unqualified names in the generic body from its defining
            // module's namespace, not the instantiation call site's module.
            generic_module_ns = inst.module_namespace;
            auto* saved_sym_ns = ctx.symbols.get_current_namespace();
            if (!inst.module_namespace.empty()) {
                ctx.symbols.set_current_namespace(ctx.symbols.create_namespace_path(inst.module_namespace));
            }
            gen_block(*fn.body);
            ctx.symbols.set_current_namespace(saved_sym_ns);
            generic_module_ns = saved_generic_ns;
        }

        if (!current_terminated) {
            if (fn.return_type && fn.return_type->kind == ast::TypeKind::Void) {
                const uint32_t zero = new_reg();
                emit(IRLoadConst{ zero, 0 });
                emit(IRReturn{ zero });
                current_terminated = true;
            } else {
                ctx.errors.add("Missing return in non-void generic function: " + fn.name); return;
            }
        }

        current_func->local_count = next_local;
        current_func->temp_count = next_reg;

        current_func = saved_func;
        next_reg = saved_next_reg;
        next_local = saved_next_local;
        current_terminated = saved_terminated;
        local_scopes = std::move(saved_locals);
        type_scopes = std::move(saved_types);
    }
}
void IRGenerator::gen_module(const quark::modules::Module& mod) {
    auto saved_namespace = namespace_stack;
    auto saved_module = current_module;

    current_module = &mod;
    namespace_stack.clear();

    // Enter the module's namespace so symbol lookups (e.g. struct types) can find them
    for (const auto& part : mod.namespace_path) {
        ctx.symbols.enter_namespace(part);
    }

    for (const auto* stmt : mod.ast) {
        if (!stmt) continue;

        if (std::holds_alternative<ast::LoadStmt>(stmt->kind) ||
            std::holds_alternative<ast::ModuleDecl>(stmt->kind) ||
            std::holds_alternative<ast::UsingStmt>(stmt->kind)) {
            continue;
        }

        if (!is_declaration_stmt(*stmt)) {
            ctx.errors.add("Top-level executable statements are not supported without __toplevel"); return;
        }

        gen_stmt(*stmt);
    }

    // Exit the module's namespace
    for (size_t i = 0; i < mod.namespace_path.size(); ++i) {
        ctx.symbols.exit_namespace();
    }

    current_module = saved_module;
    namespace_stack = std::move(saved_namespace);
}

void IRGenerator::gen_function(const ast::FuncStmt& func) {
    const std::string qname = support::qualify_name(current_module->namespace_path, namespace_stack, func.name);

    auto it = function_ids.find(qname);
    if (it == function_ids.end()) {
        ctx.errors.add("Function was not pre-collected: " + qname); return;
    }

    // Generic functions cannot be codegened without concrete type args
    if (!func.type_params.empty()) {
        return;
    }

    IRFunction* saved_func = current_func;
    uint32_t saved_next_reg = next_reg;
    uint32_t saved_next_local = next_local;
    bool saved_terminated = current_terminated;
    auto saved_namespace = namespace_stack;
    auto saved_locals = local_scopes;
    auto saved_types = type_scopes;

    current_func = &program.functions[it->second];
    current_func->body.clear();
    current_func->local_count = 0;
    current_func->temp_count = 0;
    current_func->extra_stack = 0;
    current_func->is_extern = func.is_extern;
    current_func_return_type = func.return_type;

    current_func->is_entry = false;
    current_func->syscall_number = -1;
    current_func->import_dll.clear();
    current_func->import_name.clear();
    for (const auto& attr : func.attributes) {
        if (attr.name == "entry") {
            current_func->is_entry = true;
        } else if (attr.name == "syscall" && !attr.args.empty()) {
            if (const auto* num = std::get_if<ast::IntExpr>(&attr.args[0]->kind)) {
                current_func->syscall_number = num->value;
            }
        } else if (attr.name == "export" && !attr.args.empty()) {
            if (const auto* se = std::get_if<ast::StringExpr>(&attr.args[0]->kind)) {
                current_func->export_name = se->value;
            }
        } else if (attr.name == "import" && !attr.args.empty()) {
            if (const auto* se = std::get_if<ast::StringExpr>(&attr.args[0]->kind)) {
                current_func->import_dll = se->value;
                if (attr.args.size() > 1) {
                    if (const auto* se2 = std::get_if<ast::StringExpr>(&attr.args[1]->kind)) {
                        current_func->import_name = se2->value;
                    }
                }
            }
        }
    }

    // Sret: return struct via hidden pointer arg
    const bool is_sret = func.return_type &&
                         func.return_type->kind == ast::TypeKind::Struct;
    current_func->sret = is_sret;
    current_func->arg_count = static_cast<uint32_t>(func.args.size()) + (is_sret ? 1 : 0);

    next_reg = 0;
    next_local = 0;
    current_terminated = false;

    local_scopes.clear();
    type_scopes.clear();
    local_scopes.emplace_back();
    type_scopes.emplace_back();

    if (is_sret) {
        const uint32_t sret_local = new_local();
        local_scopes.back()["__sret_ptr"] = sret_local;
        type_scopes.back()["__sret_ptr"] = nullptr;
    }

    for (const auto& arg : func.args) {
        const uint32_t local = new_local();
        local_scopes.back()[arg.name] = local;
        type_scopes.back()[arg.name] = arg.type;
    }

    if (func.is_extern) {
        current_func = saved_func;
        next_reg = saved_next_reg;
        next_local = saved_next_local;
        current_terminated = saved_terminated;
        namespace_stack = std::move(saved_namespace);
        local_scopes = std::move(saved_locals);
        type_scopes = std::move(saved_types);
        return;
    }

    if (func.body) {
        gen_block(*func.body);
    }

    if (!current_terminated) {
        if (func.return_type && func.return_type->kind == ast::TypeKind::Void) {
            const uint32_t zero = new_reg();
            emit(IRLoadConst{ zero, 0 });
            emit(IRReturn{ zero });
            current_terminated = true;
        } else {
            ctx.errors.add("Missing return in non-void function: " + func.name); return;
        }
    }

    current_func->local_count = next_local;
    current_func->temp_count = next_reg;

    current_func = saved_func;
    next_reg = saved_next_reg;
    next_local = saved_next_local;
    current_terminated = saved_terminated;
    namespace_stack = std::move(saved_namespace);
    local_scopes = std::move(saved_locals);
    type_scopes = std::move(saved_types);
}

void IRGenerator::gen_block(const ast::Block& block) {
    const bool outer_terminated = current_terminated;
    current_terminated = false;

    local_scopes.emplace_back();
    type_scopes.emplace_back();

    for (const auto* stmt : block.stmts) {
        if (!stmt || current_terminated) {
            break;
        }
        gen_stmt(*stmt);
    }

    const bool block_terminated = current_terminated;

    type_scopes.pop_back();
    local_scopes.pop_back();

    current_terminated = outer_terminated || block_terminated;
}

void IRGenerator::gen_stmt(const ast::Stmt& stmt) {
    if (current_terminated) {
        return;
    }

    std::visit(overloaded{
        [&](const ast::ExprStmt& node) {
            if (node.expr) {
                (void)gen_expr(*node.expr);
            }
        },

        [&](const ast::VarDecl& node) {
            if (!current_func) {
                // Top-level globals are handled by the collection pass in gen_program.
                return;
            }
            if (!node.type) {
                ctx.errors.add("Variable declaration missing type: " + node.name); return;
            }

            const uint32_t local = new_local();
            local_scopes.back()[node.name] = local;
            // Use concrete type from symbol table if available (generic instantiation)
            const ast::Type* var_type = node.type;
            {
                auto* sym = ctx.symbols.lookup(node.name);
                if (sym) {
                    if (auto* vs = std::get_if<symb_t::VarSymbol>(&sym->data)) {
                        var_type = vs->type;
                    } else if (auto* as = std::get_if<symb_t::FuncArgSymbol>(&sym->data)) {
                        var_type = as->type;
                    }
                }
            }
            type_scopes.back()[node.name] = var_type;
            local_var_attrs[node.name] = node.attributes;

            if (var_type->kind == ast::TypeKind::Struct) {
                int sz = type_size(var_type, &ctx);
                if (sz > 0) {
                    if (current_func) current_func->extra_stack += sz;
                    Reg ptr = new_reg();
                    emit(IRAlloca{ptr, static_cast<uint32_t>(current_func ? current_func->extra_stack : 0)});
                    emit(IRStoreLocal{local, ptr});
                }
                if (node.value) {
                    const uint32_t value = gen_expr(*node.value);
                    // Copy struct fields from result into local's allocated space
                    const uint32_t local_ptr = new_reg();
                    emit(IRLoadLocal{local_ptr, local});
                    int field_count = sz / 8;
                    for (int i = 0; i < field_count; ++i) {
                        const uint32_t src_val = new_reg();
                        emit(IRGetField{src_val, value, static_cast<uint32_t>(i * 8)});
                        emit(IRSetField{local_ptr, src_val, static_cast<uint32_t>(i * 8)});
                    }
                }
            } else if (node.value) {
                const uint32_t value = gen_expr(*node.value);
                emit(IRStoreLocal{ local, value });
            }
        },

        [&](const ast::ReturnStmt& node) {
            if (!current_func) {
                ctx.errors.add("Return outside function"); return;
            }

            if (current_func->sret && node.value) {
                // Load sret pointer from the hidden arg
                uint32_t sret_local = 0;
                {
                    bool found = false;
                    for (int i = static_cast<int>(local_scopes.size()) - 1; i >= 0; --i) {
                        auto it = local_scopes[i].find("__sret_ptr");
                        if (it != local_scopes[i].end()) {
                            sret_local = it->second;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        ctx.errors.add("sret pointer not found"); return;
                    }
                }
                const uint32_t sret_ptr = new_reg();
                emit(IRLoadLocal{ sret_ptr, sret_local });

                // Evaluate return value (produces a pointer to the struct data)
                const uint32_t result_ptr = gen_expr(*node.value);

                // Copy each field from result to sret buffer
                if (current_func_return_type &&
                    current_func_return_type->kind == ast::TypeKind::Struct) {
                    auto* sym = lookup_struct(ctx, current_func_return_type->struct_name);
                    if (sym) {
                        auto* ss = std::get_if<quark::symb_t::StructSymbol>(&sym->data);
                        if (ss) {
                            for (size_t i = 0; i < ss->field_names.size(); ++i) {
                                const uint32_t offset = static_cast<uint32_t>(i * 8u);
                                const uint32_t tmp = new_reg();
                                emit(IRGetField{ tmp, result_ptr, offset });
                                emit(IRSetField{ sret_ptr, tmp, offset });
                            }
                        }
                    }
                }

                const uint32_t zero = new_reg();
                emit(IRLoadConst{ zero, 0 });
                emit(IRReturn{ zero });
            } else if (node.value) {
                const uint32_t value = gen_expr(*node.value);
                emit(IRReturn{ value });
            } else {
                const uint32_t zero = new_reg();
                emit(IRLoadConst{ zero, 0 });
                emit(IRReturn{ zero });
            }

            current_terminated = true;
        },

        [&](const ast::IfStmt& node) {
            if (!node.condition) {
                ctx.errors.add("If statement missing condition"); return;
            }

            const uint32_t cond = gen_expr(*node.condition);

            const uint32_t then_label = new_label();
            const uint32_t end_label  = new_label();

            const bool has_else_if = node.else_if != nullptr;
            const bool has_else = node.else_block != nullptr;
            const uint32_t else_label = (has_else || has_else_if) ? new_label() : end_label;

            emit(IRBranch{
                cond,
                then_label,
                else_label
            });

            emit(IRLabel{ then_label });
            current_terminated = false;
            if (node.then_block) {
                gen_block(*node.then_block);
            }

            bool all_terminated = current_terminated;
            if (!all_terminated) {
                emit(IRJump{ end_label });
            }

            bool has_default = false;
            if (has_else_if) {
                emit(IRLabel{ else_label });
                current_terminated = false;

                for (auto* ei = node.else_if; ei; ei = ei->next) {
                    const uint32_t ei_cond = gen_expr(*ei->condition);
                    const uint32_t ei_then = new_label();
                    const bool has_rest = ei->next || ei->else_block;
                    const uint32_t ei_else = has_rest ? new_label() : end_label;
                    emit(IRBranch{ ei_cond, ei_then, ei_else });

                    emit(IRLabel{ ei_then });
                    current_terminated = false;
                    if (ei->then_block) {
                        gen_block(*ei->then_block);
                    }
                    all_terminated = all_terminated && current_terminated;
                    if (!current_terminated) {
                        emit(IRJump{ end_label });
                    }

                    if (ei->next) {
                        emit(IRLabel{ ei_else });
                        continue;
                    }

                    if (ei->else_block) {
                        emit(IRLabel{ ei_else });
                        current_terminated = false;
                        gen_block(*ei->else_block);
                        all_terminated = all_terminated && current_terminated;
                        if (!current_terminated) {
                            emit(IRJump{ end_label });
                        }
                        has_default = true;
                    }
                }
            } else if (has_else) {
                emit(IRLabel{ else_label });
                current_terminated = false;
                gen_block(*node.else_block);
                all_terminated = all_terminated && current_terminated;
                if (!current_terminated) {
                    emit(IRJump{ end_label });
                }
                has_default = true;
            }

            emit(IRLabel{ end_label });
            current_terminated = has_default && all_terminated;
        },

        [&](const ast::WhileStmt& node) {
            if (!node.condition) {
                ctx.errors.add("While statement missing condition"); return;
            }

            const uint32_t cond_label = new_label();
            const uint32_t body_label = new_label();
            const uint32_t end_label  = new_label();

            emit(IRJump{ cond_label });

            emit(IRLabel{ cond_label });
            const uint32_t cond = gen_expr(*node.condition);
            emit(IRBranch{ cond, body_label, end_label });

            emit(IRLabel{ body_label });
            current_terminated = false;
            if (node.body) {
                gen_block(*node.body);
            }

            if (!current_terminated) {
                emit(IRJump{ cond_label });
            }

            emit(IRLabel{ end_label });
            current_terminated = false;
        },

        [&](const ast::StructDecl&) {
            // Compile-time only.
        },

        [&](const ast::EnumDecl&) {
            // Compile-time only.
        },

        [&](const ast::FuncStmt& fn) {
            gen_function(fn);
        },

        [&](const ast::NamespaceStmt& ns) {
            namespace_stack.push_back(ns.name);

            const bool saved_terminated = current_terminated;

            if (ns.body) {
                for (const auto* child : ns.body->stmts) {
                    if (!child) {
                        continue;
                    }

                    if (!is_declaration_stmt(*child)) {
                        ctx.errors.add("Namespace scopes may only contain declarations"); return;
                    }

                    gen_stmt(*child);
                }
            }

            current_terminated = saved_terminated;
            namespace_stack.pop_back();
        },
        [&](const ast::LoadStmt&) {
            // compile-time only
        },
        [&](const ast::UsingStmt&) {
            // compile-time only
        },

        [&](const ast::RegionStmt& node) {
            gen_region(node);
        },

        [&](const auto&) {
            ctx.errors.add("Unsupported statement node in IR generation"); return;
        }
    }, stmt.kind);
}

void IRGenerator::gen_region(const ast::RegionStmt& reg) {
    if (!current_func) {
        ctx.errors.add("Region outside function"); return;
    }

    // Allocate Region struct on stack (3 fields * 8 = 24 bytes)
    if (current_func) current_func->extra_stack += 24;
    Local region_local = new_local();
    Reg struct_ptr = new_reg();
    emit(IRAlloca{struct_ptr, static_cast<uint32_t>(current_func->extra_stack)});
    emit(IRStoreLocal{region_local, struct_ptr});

    region_stack.push_back({region_local});

    emit(IRRegionBegin{region_local, 1024 * 1024});

    if (reg.body) {
        gen_block(*reg.body);
    }

    emit(IRRegionEnd{region_local});

    region_stack.pop_back();
}

uint32_t IRGenerator::gen_expr(const ast::Expr& expr) {
    auto lookup_local = [&](const std::string& name, uint32_t& local_out, const ast::Type*& type_out) -> bool {
        for (int i = static_cast<int>(local_scopes.size()) - 1; i >= 0; --i) {
            auto it = local_scopes[i].find(name);
            if (it != local_scopes[i].end()) {
                local_out = it->second;
                auto tit = type_scopes[i].find(name);
                if (tit != type_scopes[i].end()) {
                    type_out = tit->second;
                } else {
                    type_out = nullptr;
                }
                return true;
            }
        }
        return false;
    };

    auto resolve_function_id = [&](const std::vector<std::string>& path) -> uint32_t {
        const std::string qname = support::join_namespace(path);
        auto it = function_ids.find(qname);
        if (it != function_ids.end()) {
            return it->second;
        }


        if (current_module || !generic_module_ns.empty()) {
            const std::vector<std::string>& mod_path = generic_module_ns.empty()
                ? current_module->namespace_path : generic_module_ns;
            {
                const std::string qualified = support::qualify_name(
                    mod_path,
                    namespace_stack,
                    support::join_namespace(path)
                );
                auto it2 = function_ids.find(qualified);
                if (it2 != function_ids.end()) {
                    return it2->second;
                }
            }
            {
                std::vector<std::string> full = mod_path;
                full.insert(full.end(), path.begin(), path.end());
                const std::string qualified = support::join_namespace(full);
                auto it2 = function_ids.find(qualified);
                if (it2 != function_ids.end()) {
                    return it2->second;
                }
            }
            if (path.size() == 1) {
                const std::string qualified = support::qualify_name(
                    mod_path,
                    namespace_stack,
                    path[0]
                );
                auto it2 = function_ids.find(qualified);
                if (it2 != function_ids.end()) {
                    return it2->second;
                }
            }
        }

        // Last resort: check if the symbol was imported via `using`
        if (auto* sym = resolve_qualified(ctx, path)) {
            if (std::get_if<quark::symb_t::FuncSymbol>(&sym->data)) {
                std::vector<std::string> full = sym->owning_module;
                full.insert(full.end(), path.begin(), path.end());
                const std::string qualified = support::join_namespace(full);
                auto it3 = function_ids.find(qualified);
                if (it3 != function_ids.end()) {
                    return it3->second;
                }
            }
        }

        ctx.errors.add("Undefined function: " + qname);
        return 0;
    };

    return std::visit(overloaded{
        [&](const ast::IntExpr& node) -> uint32_t {
            const uint32_t dst = new_reg();
            emit(IRLoadConst{ dst, node.value });
            return dst;
        },

        [&](const ast::BoolExpr& node) -> uint32_t {
            const uint32_t dst = new_reg();
            emit(IRLoadConst{ dst, node.value ? 1 : 0 });
            return dst;
        },

        [&](const ast::FloatExpr& node) -> uint32_t {
            const uint32_t dst = new_reg();
            emit(IRLoadFloatConst{ dst, node.value });
            return dst;
        },

        [&](const ast::StringExpr& node) -> uint32_t {
            const Reg dst = new_reg();

            uint32_t id = program.strings.size();

            program.strings.push_back(IRString{
                id,
                node.value
            });

            emit(IRLoadString{ dst, id });

            return dst;
        },

        [&](const ast::CharExpr& node) -> uint32_t {
            const uint32_t dst = new_reg();
            emit(IRLoadConst{ dst, node.value });
            return dst;
        },

        [&](const ast::VarExpr& node) -> uint32_t {
            uint32_t local = 0;
            const ast::Type* type = nullptr;

            if (lookup_local(node.name, local, type)) {
                emit_attr_lowering(node.name);
                (void)type;
                const uint32_t dst = new_reg();
                emit(IRLoadLocal{ dst, local });
                return dst;
            }

            auto* sym = ctx.symbols.lookup(node.name);
            if (!sym) {
                ctx.errors.add("Undefined variable: " + node.name); return 0;
            }

            if (!symbol_type(*sym)) {
                ctx.errors.add("Symbol is not a value: " + node.name); return 0;
            }

            if (auto* vs = std::get_if<symb_t::VarSymbol>(&sym->data)) {
                if (!vs->is_mut && vs->const_value.has_value()) {
                    const uint32_t dst = new_reg();
                    emit(IRLoadConst{ dst, static_cast<int32_t>(*vs->const_value) });
                    return dst;
                }

                const std::string qname = support::qualify_name(
                    sym->owning_module, {}, node.name
                );
                auto git = global_ids.find(qname);
                if (git != global_ids.end()) {
                    const uint32_t dst = new_reg();
                    if (vs->type && vs->type->kind == ast::TypeKind::Struct) {
                        emit(IRLoadGlobalAddr{ dst, git->second });
                    } else {
                        emit(IRLoadGlobal{ dst, git->second });
                    }
                    return dst;
                }
            }

            ctx.errors.add("Global value lowering is not implemented yet: " + node.name); return 0;
        },

        [&](const ast::BinaryExpr& node) -> uint32_t {
            const uint32_t lhs = gen_expr(*node.lhs);
            const uint32_t rhs = gen_expr(*node.rhs);
            const uint32_t dst = new_reg();

            ast::TypeKind tk = ast::TypeKind::I32;
            if (node.lhs && node.lhs->resolved_type) {
                tk = node.lhs->resolved_type->kind;
            }

            emit(IRBinary{
                map_op(node.op),
                dst,
                lhs,
                rhs,
                tk
            });

            return dst;
        },

        [&](const ast::UnaryExpr& node) -> uint32_t {
            const ast::Type* op_type = node.operand ? node.operand->resolved_type : nullptr;

            // Address-of: &expr
            if (node.op == ast::UnaryOp::AddrOf) {
                // Handle &var (local variable)
                if (const auto* var = std::get_if<ast::VarExpr>(&node.operand->kind)) {
                    const ast::Type* var_type = nullptr;
                    for (int i = static_cast<int>(type_scopes.size()) - 1; i >= 0; --i) {
                        auto tit = type_scopes[i].find(var->name);
                        if (tit != type_scopes[i].end()) {
                            var_type = tit->second;
                            break;
                        }
                    }
                    for (int i = static_cast<int>(local_scopes.size()) - 1; i >= 0; --i) {
                        auto it = local_scopes[i].find(var->name);
                        if (it != local_scopes[i].end()) {
                            const uint32_t local = it->second;
                            const uint32_t dst = new_reg();
                            // Struct locals store a pointer via IRAlloca — the local IS the pointer
                            if (var_type && var_type->kind == ast::TypeKind::Struct) {
                                emit(IRLoadLocal{ dst, local });
                            } else {
                                emit(IRAddrOf{ dst, local });
                            }
                            return dst;
                        }
                    }

                    // Global variable address
                    auto* gsym = ctx.symbols.lookup(var->name);
                    if (gsym) {
                        if (auto* gvs = std::get_if<symb_t::VarSymbol>(&gsym->data)) {
                            if (gvs->is_mut || !gvs->const_value.has_value()) {
                                const std::string qname = support::qualify_name(
                                    gsym->owning_module, {}, var->name
                                );
                                auto git = global_ids.find(qname);
                                if (git != global_ids.end()) {
                                    const uint32_t dst = new_reg();
                                    emit(IRLoadGlobalAddr{ dst, git->second });
                                    return dst;
                                }
                            }
                        }
                    }
                }
                ctx.errors.add("Cannot take address of non-local expression");
                return 0;
            }

            const uint32_t operand = gen_expr(*node.operand);
            const uint32_t dst = new_reg();
            const uint32_t zero = new_reg();
            emit(IRLoadConst{ zero, 0 });

            ast::TypeKind tk = ast::TypeKind::I32;
            if (node.operand && node.operand->resolved_type) {
                tk = node.operand->resolved_type->kind;
            }

            if (node.op == ast::UnaryOp::Neg) {
                emit(IRBinary{ IRBinaryOp::Sub, dst, zero, operand, tk });
            } else {
                emit(IRBinary{ IRBinaryOp::Eq, dst, operand, zero, tk });
            }

            return dst;
        },

        [&](const ast::AssignExpr& node) -> uint32_t {
            if (!node.target || !node.value) {
                ctx.errors.add("Assignment target or value is missing"); return 0;
            }

            const uint32_t value = gen_expr(*node.value);

            if (const auto* var = std::get_if<ast::VarExpr>(&node.target->kind)) {
                for (int i = static_cast<int>(local_scopes.size()) - 1; i >= 0; --i) {
                    auto it = local_scopes[i].find(var->name);
                    if (it != local_scopes[i].end()) {
                        const uint32_t local = it->second;
                        emit(IRStoreLocal{ local, value });
                        return value;
                    }
                }

                auto* sym = ctx.symbols.lookup(var->name);
                if (!sym) {
                    ctx.errors.add("Undefined variable: " + var->name); return 0;
                }

                if (!symbol_is_mutable(*sym)) {
                    ctx.errors.add("Cannot assign to immutable variable: " + var->name); return 0;
                }

                const std::string qname = support::qualify_name(
                    sym->owning_module, {}, var->name
                );
                auto git = global_ids.find(qname);
                if (git != global_ids.end()) {
                    emit(IRStoreGlobal{ git->second, value });
                    return value;
                }

                ctx.errors.add("Global assignment lowering is not implemented yet: " + var->name); return 0;
            }

            if (const auto* field = std::get_if<ast::FieldExpr>(&node.target->kind)) {
                const uint32_t base = [&]() -> uint32_t {
                    if (const auto* bv = std::get_if<ast::VarExpr>(&field->base->kind)) {
                        uint32_t loc = 0;
                        const ast::Type* t = nullptr;
                        if (lookup_local(bv->name, loc, t)) {
                            const uint32_t d = new_reg();
                            emit(IRLoadLocal{ d, loc });
                            return d;
                        }
                    }
                    return gen_expr(*field->base);
                }();
                const ast::Type* base_type = nullptr;

                if (const auto* base_var = std::get_if<ast::VarExpr>(&field->base->kind)) {
                    for (int i = static_cast<int>(local_scopes.size()) - 1; i >= 0; --i) {
                        auto tit = type_scopes[i].find(base_var->name);
                        if (tit != type_scopes[i].end()) {
                            base_type = tit->second;
                            break;
                        }
                    }

                    if (!base_type) {
                        auto* sym = ctx.symbols.lookup(base_var->name);
                        if (sym) {
                            base_type = symbol_type(*sym);
                        }
                    }
                }

                const auto [offset, field_type] = resolve_struct_field(ctx, base_type, field->field);
                (void)field_type;

                emit(IRSetField{
                    base,
                    value,
                    offset
                });

                return value;
            }

            if (const auto* index = std::get_if<ast::IndexExpr>(&node.target->kind)) {
                const uint32_t base = gen_expr(*index->base);
                const uint32_t idx = gen_expr(*index->index);

                const ast::Type* base_type = index->base->resolved_type;
                // Auto-deref references
                if (base_type && base_type->kind == ast::TypeKind::Reference && base_type->pointed) {
                    base_type = base_type->pointed;
                }
                if (!base_type || base_type->kind != ast::TypeKind::Pointer || !base_type->pointed) {
                    ctx.errors.add("Invalid pointer index in assignment"); return 0;
                }

                uint32_t elem_size = type_size(base_type->pointed);
                emit(IRStoreElement{base, idx, value, elem_size});
                return value;
            }

            ctx.errors.add("Assignment target must be variable, field access, or index expression"); return 0;
        },

        [&](const ast::FieldExpr& node) -> uint32_t {
            const uint32_t base = gen_expr(*node.base);
            const ast::Type* base_type = nullptr;

            if (const auto* base_var = std::get_if<ast::VarExpr>(&node.base->kind)) {
                for (int i = static_cast<int>(local_scopes.size()) - 1; i >= 0; --i) {
                    auto tit = type_scopes[i].find(base_var->name);
                    if (tit != type_scopes[i].end()) {
                        base_type = tit->second;
                        break;
                    }
                }

                if (!base_type) {
                    auto* sym = ctx.symbols.lookup(base_var->name);
                    if (sym) {
                        base_type = symbol_type(*sym);
                    }
                }
            }

            const auto [offset, field_type] = resolve_struct_field(ctx, base_type, node.field);
            (void)field_type;

            // Auto-deref references for attribute lookup
            if (base_type && base_type->kind == ast::TypeKind::Reference && base_type->pointed) {
                base_type = base_type->pointed;
            }

            // Emit attribute lowering for struct field reads (e.g. @guard)
            if (base_type && base_type->kind == ast::TypeKind::Struct) {
                auto* sym = lookup_struct(ctx, base_type->struct_name);
                if (sym) {
                    auto* ss = std::get_if<quark::symb_t::StructSymbol>(&sym->data);
                    if (ss) {
                        for (size_t i = 0; i < ss->field_names.size(); ++i) {
                            if (ss->field_names[i] == node.field && i < ss->field_attributes.size()) {
                                emit_attr_lowering(node.field, ss->field_attributes[i]);
                                break;
                            }
                        }
                    }
                }
            }

            const uint32_t dst = new_reg();
            emit(IRGetField{
                dst,
                base,
                offset
            });

            return dst;
        },

        [&](const ast::CallExpr& node) -> uint32_t {
            if (!region_stack.empty() && node.callee) {
                auto callee_path = support::flatten_path(node.callee);
                if (callee_path.size() == 1 && callee_path[0] == "alloc") {
                    if (node.args.size() == 2) {
                        // alloc(T, count) — typed allocation
                        const auto* type_expr = std::get_if<ast::TypeExpr>(&node.args[0]->kind);
                        if (!type_expr || !type_expr->type) {
                            ctx.errors.add("First argument to alloc must be a type, e.g. alloc(i32, 10)"); return 0;
                        }
                        uint32_t elem_size = type_size(type_expr->type);
                        if (elem_size == 0) {
                            ctx.errors.add("Cannot allocate element of unknown size"); return 0;
                        }
                        const uint32_t count = gen_expr(*node.args[1]);
                        const uint32_t elem_reg = new_reg();
                        emit(IRLoadConst{elem_reg, static_cast<int64_t>(elem_size)});
                        const uint32_t total = new_reg();
                        emit(IRBinary{IRBinaryOp::Mul, total, count, elem_reg, ast::TypeKind::U64});

                        const uint32_t dst = new_reg();
                        const auto& ri = region_stack.back();
                        emit(IRRegionAlloc{dst, total, ri.region_local});
                        return dst;
                    }

                    if (node.args.size() != 1 || !node.args[0]) {
                        ctx.errors.add("alloc takes 1 argument (bytes) or 2 arguments (type, count)"); return 0;
                    }
                    const uint32_t size = gen_expr(*node.args[0]);
                    const uint32_t dst = new_reg();
                    const auto& ri = region_stack.back();
                    emit(IRRegionAlloc{dst, size, ri.region_local});
                    return dst;
                }
            }

            std::vector<uint32_t> args;
            args.reserve(node.args.size());

            for (const auto* arg : node.args) {
                if (!arg) {
                    ctx.errors.add("Null call argument"); return 0;
                }
                args.push_back(gen_expr(*arg));
            }

            if (!node.callee) {
                ctx.errors.add("Call callee is missing"); return 0;
            }

            uint32_t func_id = 0;

            const auto callee_path = support::flatten_path(node.callee);

            // Handle generic function calls: use mangled name. The mangling
            // must include the full module path to match the semantic pass's
            // instantiation name (e.g. "gm::g<i32>" -> "gm::g$4").
            std::string mangled;
            if (!node.type_args.empty()) {
                mangled = ctx.types.mangle_func_name(support::join_namespace(callee_path), node.type_args);
                func_id = resolve_function_id({mangled});
            } else {
                func_id = resolve_function_id(callee_path);
            }

            // Check if callee returns a struct (needs sret). Generic calls
            // have no symbol under the un-mangled name; use the concrete
            // (mangled) instantiation return type recorded by the semantic pass.
            bool is_sret_call = false;
            uint32_t sret_ptr = 0;
            {
                const ast::Type* ret_type = nullptr;
                if (!node.type_args.empty()) {
                    auto it = ctx.generic_return_types.find(mangled);
                    if (it != ctx.generic_return_types.end()) ret_type = it->second;
                } else if (auto* fn_sym = resolve_qualified(ctx, callee_path)) {
                    auto* fs = std::get_if<quark::symb_t::FuncSymbol>(&fn_sym->data);
                    if (fs) ret_type = fs->return_type;
                }
                if (ret_type && ret_type->kind == ast::TypeKind::Struct) {
                        is_sret_call = true;
                        int sz = type_size(ret_type, &ctx);
                        if (current_func) current_func->extra_stack += sz;
                        sret_ptr = new_reg();
                        emit(IRAlloca{ sret_ptr, static_cast<uint32_t>(current_func ? current_func->extra_stack : 0) });
                        args.insert(args.begin(), sret_ptr);
                    }
            }

            const uint32_t dst = new_reg();
            emit(IRCall{
                dst,
                func_id,
                args,
                is_sret_call
            });

            if (is_sret_call) {
                return sret_ptr;
            }
            return dst;
        },

        [&](const ast::NamespaceExpr& node) -> uint32_t {
            auto tmp = ast::Expr{ node };
            auto path = support::flatten_path(&tmp);
            auto* sym = resolve_qualified(ctx, path);
            if (sym) {
                if (auto* vs = std::get_if<symb_t::VarSymbol>(&sym->data)) {
                    if (vs->const_value.has_value()) {
                        const uint32_t dst = new_reg();
                        emit(IRLoadConst{ dst, static_cast<int32_t>(*vs->const_value) });
                        return dst;
                    }
                }
            }
            ctx.errors.add("Namespace expressions are not supported in IR generation yet"); return 0;
        },

        [&](const ast::CastExpr& node) -> uint32_t {
            const uint32_t src = gen_expr(*node.value);
            const uint32_t dst = new_reg();

            ast::TypeKind src_kind = ast::TypeKind::Void;
            if (node.value->resolved_type) {
                src_kind = node.value->resolved_type->kind;
            }

            emit(IRCast{
                dst,
                src,
                src_kind,
                node.target->kind,
                node.kind
            });
            return dst;
        },

        [&](const ast::StructInitExpr& node) -> uint32_t {
            const ast::Type* struct_type = expr.resolved_type;
            if (!struct_type || struct_type->kind != ast::TypeKind::Struct) {
                ctx.errors.add("StructInitExpr: invalid or missing struct type"); return 0;
            }

            int sz = type_size(struct_type, &ctx);
            if (sz <= 0) {
                ctx.errors.add("StructInitExpr: zero-size struct"); return 0;
            }

            // Allocate stack space for the struct
            if (current_func) current_func->extra_stack += sz;
            const uint32_t ptr = new_reg();
            emit(IRAlloca{ ptr, static_cast<uint32_t>(current_func ? current_func->extra_stack : 0) });

            // Initialize each field by position
            for (size_t i = 0; i < node.args.size(); ++i) {
                const uint32_t val = gen_expr(*node.args[i]);
                emit(IRSetField{ ptr, val, static_cast<uint32_t>(i * 8) });
            }

            return ptr;
        },

        [&](const ast::SizeofExpr& node) -> uint32_t {
            int sz = ctx.types.type_size(node.type);
            if (sz <= 0) {
                ctx.errors.add("sizeof: zero or unknown size"); return 0;
            }
            const uint32_t dst = new_reg();
            emit(IRLoadConst{ dst, static_cast<int64_t>(sz) });
            return dst;
        },

        [&](const ast::TypeExpr&) -> uint32_t {
            ctx.errors.add("Type used as value in IR generation"); return 0;
        },

        [&](const ast::IndexExpr& node) -> uint32_t {
            const uint32_t base = gen_expr(*node.base);
            const uint32_t idx = gen_expr(*node.index);
            const uint32_t dst = new_reg();

            const ast::Type* base_type = node.base->resolved_type;
            // Auto-deref references
            if (base_type && base_type->kind == ast::TypeKind::Reference && base_type->pointed) {
                base_type = base_type->pointed;
            }
            if (!base_type || base_type->kind != ast::TypeKind::Pointer || !base_type->pointed) {
                ctx.errors.add("Invalid pointer index in IR gen"); return 0;
            }

            uint32_t elem_size = type_size(base_type->pointed);
            emit(IRLoadElement{dst, base, idx, elem_size});
            return dst;
        }
    }, expr.kind);
}

} // namespace quark::codegen
