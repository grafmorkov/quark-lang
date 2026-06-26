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
            if (!sym) return 0;
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
           std::holds_alternative<ast::StructDecl>(stmt.kind);
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
    function_ids.clear();
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
    for (const auto& fn : ctx.generic_instantiations) {
        register_function(fn.name);
    }

    for (auto* mod : modules) {
        if (!mod) continue;
        gen_module(*mod);
    }

    // Generate IR for concrete generic instantiations
    for (const auto& fn : ctx.generic_instantiations) {
        auto it = function_ids.find(fn.name);
        if (it == function_ids.end()) {
            ctx.errors.add("Generic instantiation not registered: " + fn.name); return;
        }

        IRFunction* saved_func = current_func;
        uint32_t saved_next_reg = next_reg;
        uint32_t saved_next_label = next_label;
        uint32_t saved_next_local = next_local;
        bool saved_terminated = current_terminated;
        auto saved_locals = local_scopes;
        auto saved_types = type_scopes;

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
        next_label = 0;
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
            next_label = saved_next_label;
            next_local = saved_next_local;
            current_terminated = saved_terminated;
            local_scopes = std::move(saved_locals);
            type_scopes = std::move(saved_types);
            continue;
        }

        if (fn.body) {
            gen_block(*fn.body);
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
        next_label = saved_next_label;
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
            std::holds_alternative<ast::ModuleDecl>(stmt->kind)) {
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
    uint32_t saved_next_label = next_label;
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
    for (const auto& attr : func.attributes) {
        if (attr.name == "entry") {
            current_func->is_entry = true;
            break;
        }
    }

    // Sret: return struct via hidden pointer arg
    const bool is_sret = func.return_type &&
                         func.return_type->kind == ast::TypeKind::Struct;
    current_func->sret = is_sret;
    current_func->arg_count = static_cast<uint32_t>(func.args.size()) + (is_sret ? 1 : 0);

    next_reg = 0;
    next_label = 0;
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
        next_label = saved_next_label;
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
    next_label = saved_next_label;
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
            const uint32_t else_label = node.else_block ? new_label() : end_label;

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

            const bool then_terminated = current_terminated;
            if (!then_terminated) {
                emit(IRJump{ end_label });
            }

            bool else_terminated = false;
            if (node.else_block) {
                emit(IRLabel{ else_label });
                current_terminated = false;
                gen_block(*node.else_block);
                else_terminated = current_terminated;
                if (!else_terminated) {
                    emit(IRJump{ end_label });
                }
            }

            emit(IRLabel{ end_label });
            current_terminated = node.else_block && then_terminated && else_terminated;
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

    Local data_local = new_local();
    Local offset_local = new_local();
    Local cap_local = new_local();

    region_stack.push_back({data_local, offset_local, cap_local});

    emit(IRRegionBegin{data_local, offset_local, cap_local, 1024 * 1024});

    if (reg.body) {
        gen_block(*reg.body);
    }

    emit(IRRegionEnd{data_local, cap_local});

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


        if (current_module) {
            {
                const std::string qualified = support::qualify_name(
                    current_module->namespace_path,
                    namespace_stack,
                    support::join_namespace(path)
                );
                auto it2 = function_ids.find(qualified);
                if (it2 != function_ids.end()) {
                    return it2->second;
                }
            }
            {
                std::vector<std::string> mod_path = current_module->namespace_path;
                mod_path.insert(mod_path.end(), path.begin(), path.end());
                const std::string qualified = support::join_namespace(mod_path);
                auto it2 = function_ids.find(qualified);
                if (it2 != function_ids.end()) {
                    return it2->second;
                }
            }
            if (path.size() == 1) {
                const std::string qualified = support::qualify_name(
                    current_module->namespace_path,
                    namespace_stack,
                    path[0]
                );
                auto it2 = function_ids.find(qualified);
                if (it2 != function_ids.end()) {
                    return it2->second;
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

            ctx.errors.add("Global value lowering is not implemented yet: " + node.name); return 0;
        },

        [&](const ast::BinaryExpr& node) -> uint32_t {
            const ast::Type* lhs_type = node.lhs ? node.lhs->resolved_type : nullptr;

            if (lhs_type && lhs_type->kind == ast::TypeKind::Struct) {
                std::string op_name;
                switch (node.op) {
                    case ast::BinaryOp::Add: op_name = "operator+"; break;
                    case ast::BinaryOp::Sub: op_name = "operator-"; break;
                    case ast::BinaryOp::Mul: op_name = "operator*"; break;
                    case ast::BinaryOp::Div: op_name = "operator/"; break;
                    case ast::BinaryOp::Eq:  op_name = "operator=="; break;
                    case ast::BinaryOp::Neq: op_name = "operator!="; break;
                    case ast::BinaryOp::Lt:  op_name = "operator<"; break;
                    case ast::BinaryOp::Lte: op_name = "operator<="; break;
                    case ast::BinaryOp::Gt:  op_name = "operator>"; break;
                    case ast::BinaryOp::Gte: op_name = "operator>="; break;
                    case ast::BinaryOp::BitAnd:   op_name = "operator&"; break;
                    case ast::BinaryOp::BitOr:    op_name = "operator|"; break;
                    case ast::BinaryOp::LogicAnd: op_name = "operator&&"; break;
                    case ast::BinaryOp::LogicOr:  op_name = "operator||"; break;
                }

                const uint32_t lhs = gen_expr(*node.lhs);
                const uint32_t rhs = gen_expr(*node.rhs);

                // Allocate temp buffer for the struct result (sret)
                int sz = type_size(lhs_type, &ctx);
                if (current_func) current_func->extra_stack += sz;
                const uint32_t sret_ptr = new_reg();
                emit(IRAlloca{ sret_ptr, static_cast<uint32_t>(current_func ? current_func->extra_stack : 0) });

                const uint32_t dst = new_reg();
                // Build mangled function name from struct type info
                std::string mangled = ctx.types.mangle_func_name(op_name, lhs_type->type_args);
                // Also try qualified name: extract namespace from struct name
                std::string struct_base = lhs_type->struct_name;
                {
                    auto dollar = struct_base.find('$');
                    if (dollar != std::string::npos)
                        struct_base = struct_base.substr(0, dollar);
                }
                // First try the simple mangled name
                uint32_t func_id;
                auto it = function_ids.find(mangled);
                if (it != function_ids.end()) {
                    func_id = it->second;
                } else {
                    // Try qualified: extract namespace from struct name
                    std::string ns_op = struct_base;
                    auto colon = struct_base.rfind("::");
                    if (colon != std::string::npos) {
                        ns_op = struct_base.substr(0, colon) + "::" + op_name;
                    } else {
                        ns_op = op_name;
                    }
                    std::string qualified_mangled = ctx.types.mangle_func_name(ns_op, lhs_type->type_args);
                    auto it2 = function_ids.find(qualified_mangled);
                    if (it2 != function_ids.end()) {
                        func_id = it2->second;
                    } else {
                        func_id = resolve_function_id({op_name});
                    }
                }
                emit(IRCall{ dst, func_id, {sret_ptr, lhs, rhs}, true });
                return sret_ptr;
            }

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

            if (op_type && op_type->kind == ast::TypeKind::Struct) {
                std::string op_name;
                switch (node.op) {
                    case ast::UnaryOp::Neg: op_name = "operator-"; break;
                    case ast::UnaryOp::Not: op_name = "operator!"; break;
                }

                const uint32_t operand = gen_expr(*node.operand);

                int sz = type_size(op_type, &ctx);
                if (current_func) current_func->extra_stack += sz;
                const uint32_t sret_ptr = new_reg();
                emit(IRAlloca{ sret_ptr, static_cast<uint32_t>(current_func ? current_func->extra_stack : 0) });

                const uint32_t dst = new_reg();
                uint32_t func_id = resolve_function_id({op_name});
                emit(IRCall{ dst, func_id, {sret_ptr, operand}, true });
                return sret_ptr;
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
                        emit(IRRegionAlloc{dst, total, ri.data_local, ri.offset_local, ri.cap_local});
                        return dst;
                    }

                    if (node.args.size() != 1 || !node.args[0]) {
                        ctx.errors.add("alloc takes 1 argument (bytes) or 2 arguments (type, count)"); return 0;
                    }
                    const uint32_t size = gen_expr(*node.args[0]);
                    const uint32_t dst = new_reg();
                    const auto& ri = region_stack.back();
                    emit(IRRegionAlloc{dst, size, ri.data_local, ri.offset_local, ri.cap_local});
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

            // Handle generic function calls: use mangled name
            if (!node.type_args.empty()) {
                std::string mangled = ctx.types.mangle_func_name(callee_path.back(), node.type_args);
                func_id = resolve_function_id({mangled});
            } else {
                func_id = resolve_function_id(callee_path);
            }

            // Check if callee returns a struct (needs sret)
            bool is_sret_call = false;
            uint32_t sret_ptr = 0;
            {
                auto* fn_sym = resolve_qualified(ctx, callee_path);
                if (fn_sym) {
                    auto* fs = std::get_if<quark::symb_t::FuncSymbol>(&fn_sym->data);
                    if (fs && fs->return_type &&
                        fs->return_type->kind == ast::TypeKind::Struct) {
                        is_sret_call = true;
                        int sz = type_size(fs->return_type, &ctx);
                        if (current_func) current_func->extra_stack += sz;
                        sret_ptr = new_reg();
                        emit(IRAlloca{ sret_ptr, static_cast<uint32_t>(current_func ? current_func->extra_stack : 0) });
                        args.insert(args.begin(), sret_ptr);
                    }
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

        [&](const ast::NamespaceExpr&) -> uint32_t {
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

        [&](const ast::TypeExpr&) -> uint32_t {
            ctx.errors.add("Type used as value in IR generation"); return 0;
        },

        [&](const ast::IndexExpr& node) -> uint32_t {
            const uint32_t base = gen_expr(*node.base);
            const uint32_t idx = gen_expr(*node.index);
            const uint32_t dst = new_reg();

            const ast::Type* base_type = node.base->resolved_type;
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
