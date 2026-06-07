#include <functional>
#include <utility>
#include <variant>

#include "quark/ir/ir_gen.h"
#include "quark/support/symbol_path.h"
#include "utils/logger.h"

using namespace utils::logger;

namespace quark::codegen {

namespace {

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
        crash("Field access base type is null");
    }

    if (base_type->kind != ast::TypeKind::Struct) {
        crash("Field access on non-struct type");
    }

    auto* sym = ctx.symbols.lookup(base_type->struct_name);
    if (!sym) {
        crash("Unknown struct: " + base_type->struct_name);
    }

    auto* ss = std::get_if<quark::symb_t::StructSymbol>(&sym->data);
    if (!ss) {
        crash("Invalid struct symbol: " + base_type->struct_name);
    }

    for (size_t i = 0; i < ss->field_names.size(); ++i) {
        if (ss->field_names[i] == field_name) {
            return {
                static_cast<uint32_t>(i * 8u),
                ss->field_types[i]
            };
        }
    }

    crash("Unknown field: " + field_name);
}


} // namespace

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
        crash("No current IR function set");
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
        default:
            crash("Unsupported binary op");
    }
}

void IRGenerator::gen_program(std::span<quark::modules::Module* const> modules) {
    program.functions.clear();
    function_ids.clear();
    namespace_stack.clear();
    local_scopes.clear();
    type_scopes.clear();

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
            crash("Generic instantiation not registered: " + fn.name);
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
        current_func->arg_count = static_cast<uint32_t>(fn.args.size());
        current_func->local_count = 0;
        current_func->temp_count = 0;
        current_func->is_extern = fn.is_extern;

        current_func->is_entry = false;

        next_reg = 0;
        next_label = 0;
        next_local = 0;
        current_terminated = false;

        local_scopes.clear();
        type_scopes.clear();
        local_scopes.emplace_back();
        type_scopes.emplace_back();

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
                crash("Missing return in non-void generic function: " + fn.name);
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

    for (const auto* stmt : mod.ast) {
        if (!stmt) continue;

        if (std::holds_alternative<ast::LoadStmt>(stmt->kind) ||
            std::holds_alternative<ast::ModuleDecl>(stmt->kind)) {
            continue;
        }

        if (!is_declaration_stmt(*stmt)) {
            crash("Top-level executable statements are not supported without __toplevel");
        }

        gen_stmt(*stmt);
    }

    current_module = saved_module;
    namespace_stack = std::move(saved_namespace);
}

void IRGenerator::gen_function(const ast::FuncStmt& func) {
    const std::string qname = support::qualify_name(current_module->namespace_path, namespace_stack, func.name);

    auto it = function_ids.find(qname);
    if (it == function_ids.end()) {
        crash("Function was not pre-collected: " + qname);
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
    current_func->arg_count = static_cast<uint32_t>(func.args.size());
    current_func->local_count = 0;
    current_func->temp_count = 0;
    current_func->is_extern = func.is_extern;

    current_func->is_entry = false;
    for (const auto& attr : func.attributes) {
        if (attr.name == "entry") {
            current_func->is_entry = true;
            break;
        }
    }

    next_reg = 0;
    next_label = 0;
    next_local = 0;
    current_terminated = false;

    local_scopes.clear();
    type_scopes.clear();
    local_scopes.emplace_back();
    type_scopes.emplace_back();

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
            crash("Missing return in non-void function: " + func.name);
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
                crash("Variable declaration missing type: " + node.name);
            }

            const uint32_t local = new_local();
            local_scopes.back()[node.name] = local;
            type_scopes.back()[node.name] = node.type;

            if (node.value) {
                const uint32_t value = gen_expr(*node.value);
                emit(IRStoreLocal{ local, value });
            }
        },

        [&](const ast::ReturnStmt& node) {
            if (!current_func) {
                crash("Return outside function");
            }

            if (node.value) {
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
                crash("If statement missing condition");
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
                crash("While statement missing condition");
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
                        crash("Namespace scopes may only contain declarations");
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
            crash("Unsupported statement node in IR generation");
        }
    }, stmt.kind);
}

void IRGenerator::gen_region(const ast::RegionStmt& reg) {
    if (!current_func) {
        crash("Region outside function");
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

        if (path.size() == 1 && current_module) {
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

        crash("Undefined function: " + qname);
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
                (void)type;
                const uint32_t dst = new_reg();
                emit(IRLoadLocal{ dst, local });
                return dst;
            }

            auto* sym = ctx.symbols.lookup(node.name);
            if (!sym) {
                crash("Undefined variable: " + node.name);
            }

            if (!symbol_type(*sym)) {
                crash("Symbol is not a value: " + node.name);
            }

            crash("Global value lowering is not implemented yet: " + node.name);
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

        [&](const ast::AssignExpr& node) -> uint32_t {
            if (!node.target || !node.value) {
                crash("Assignment target or value is missing");
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
                    crash("Undefined variable: " + var->name);
                }

                if (!symbol_is_mutable(*sym)) {
                    crash("Cannot assign to immutable variable: " + var->name);
                }

                crash("Global assignment lowering is not implemented yet: " + var->name);
            }

            if (const auto* field = std::get_if<ast::FieldExpr>(&node.target->kind)) {
                const uint32_t base = gen_expr(*field->base);
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
                    crash("Invalid pointer index in assignment");
                }

                uint32_t elem_size = type_size(base_type->pointed);
                emit(IRStoreElement{base, idx, value, elem_size});
                return value;
            }

            crash("Assignment target must be variable, field access, or index expression");
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
                            crash("First argument to alloc must be a type, e.g. alloc(i32, 10)");
                        }
                        uint32_t elem_size = type_size(type_expr->type);
                        if (elem_size == 0) {
                            crash("Cannot allocate element of unknown size");
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
                        crash("alloc takes 1 argument (bytes) or 2 arguments (type, count)");
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
                    crash("Null call argument");
                }
                args.push_back(gen_expr(*arg));
            }

            if (!node.callee) {
                crash("Call callee is missing");
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

            const uint32_t dst = new_reg();
            emit(IRCall{
                dst,
                func_id,
                args
            });

            return dst;
        },

        [&](const ast::NamespaceExpr&) -> uint32_t {
            crash("Namespace expressions are not supported in IR generation yet");
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
            crash("Type used as value in IR generation");
        },

        [&](const ast::IndexExpr& node) -> uint32_t {
            const uint32_t base = gen_expr(*node.base);
            const uint32_t idx = gen_expr(*node.index);
            const uint32_t dst = new_reg();

            const ast::Type* base_type = node.base->resolved_type;
            if (!base_type || base_type->kind != ast::TypeKind::Pointer || !base_type->pointed) {
                crash("Invalid pointer index in IR gen");
            }

            uint32_t elem_size = type_size(base_type->pointed);
            emit(IRLoadElement{dst, base, idx, elem_size});
            return dst;
        }
    }, expr.kind);
}

} // namespace quark::codegen
