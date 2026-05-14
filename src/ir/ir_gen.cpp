#include "quark/ir/ir_gen.h"
#include "utils/logger.h"

#include <functional>
#include <utility>
#include <variant>

using namespace utils::logger;

namespace quark::codegen {

namespace {

template<class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

std::string join_namespace(const std::vector<std::string>& ns) {
    if (ns.empty()) return {};

    std::string out;
    for (size_t i = 0; i < ns.size(); ++i) {
        if (i) out += "::";
        out += ns[i];
    }
    return out;
}

std::string qualify_name(
    const std::vector<std::string>& ns,
    const std::string& name
) {
    const std::string prefix = join_namespace(ns);
    if (prefix.empty()) return name;
    return prefix + "::" + name;
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

std::pair<int, const ast::Type*> resolve_struct_field(
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
            return { static_cast<int>(i), ss->field_types[i] };
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

void IRGenerator::gen_program(const std::vector<ast::Stmt*>& program_stmts) {
    program.functions.clear();
    function_ids.clear();
    namespace_stack.clear();
    local_scopes.clear();
    type_scopes.clear();

    next_reg = 0;
    next_label = 0;
    current_terminated = false;
    current_func = nullptr;

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

    std::function<void(const std::vector<ast::Stmt*>&)> collect_functions;
    collect_functions = [&](const std::vector<ast::Stmt*>& stmts) {
        for (const auto* stmt : stmts) {
            if (!stmt) continue;

            std::visit(overloaded {
                [&](const ast::FuncStmt& fn) {
                    register_function(qualify_name(namespace_stack, fn.name));
                },
                [&](const ast::NamespaceStmt& ns) {
                    namespace_stack.push_back(ns.name);
                    if (ns.body) {
                        collect_functions(ns.body->stmts);
                    }
                    namespace_stack.pop_back();
                },
                [&](const auto&) {
                    // other statements do not register functions
                }
            }, stmt->kind);
        }
    };

    // Create toplevel function first.
    register_function("__toplevel");
    collect_functions(program_stmts);

    const uint32_t toplevel_id = function_ids.at("__toplevel");
    current_func = &program.functions[toplevel_id];
    current_func->body.clear();

    next_reg = 0;
    next_label = 0;
    current_terminated = false;

    local_scopes.emplace_back();
    type_scopes.emplace_back();

    for (const auto* stmt : program_stmts) {
        if (!stmt || current_terminated) continue;
        gen_stmt(*stmt);
    }

    // Make toplevel always terminate cleanly.
    if (!current_terminated) {
        const uint32_t zero = new_reg();
        emit(IRConst{ zero, 0 });
        emit(IRReturn{ zero });
        current_terminated = true;
    }

    type_scopes.pop_back();
    local_scopes.pop_back();
}

void IRGenerator::gen_function(const ast::FuncStmt& func) {
    const std::string qname = qualify_name(namespace_stack, func.name);

    auto it = function_ids.find(qname);
    if (it == function_ids.end()) {
        crash("Function was not pre-collected: " + qname);
    }

    IRFunction* saved_func = current_func;
    uint32_t saved_next_reg = next_reg;
    uint32_t saved_next_label = next_label;
    bool saved_terminated = current_terminated;
    auto saved_namespace = namespace_stack;
    auto saved_locals = local_scopes;
    auto saved_types = type_scopes;

    current_func = &program.functions[it->second];
    current_func->body.clear();

    next_reg = 0;
    next_label = 0;
    current_terminated = false;

    local_scopes.clear();
    type_scopes.clear();
    local_scopes.emplace_back();
    type_scopes.emplace_back();

    for (const auto& arg : func.args) {
        uint32_t reg = new_reg();
        local_scopes.back()[arg.name] = reg;
        type_scopes.back()[arg.name] = arg.type;
    }

    if (func.body) {
        gen_block(*func.body);
    }

    if (!current_terminated) {
        if (func.return_type && func.return_type->kind == ast::TypeKind::Void) {
            const uint32_t zero = new_reg();
            emit(IRConst{ zero, 0 });
            emit(IRReturn{ zero });
            current_terminated = true;
        } else {
            crash("Missing return in non-void function: " + func.name);
        }
    }

    current_func = saved_func;
    next_reg = saved_next_reg;
    next_label = saved_next_label;
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

    std::visit(overloaded {
        [&](const ast::ExprStmt& node) {
            if (node.expr) {
                (void)gen_expr(*node.expr);
            }
        },

        [&](const ast::VarDecl& node) {
            if (!node.type) {
                crash("Variable declaration missing type: " + node.name);
            }

            uint32_t reg = new_reg();
            local_scopes.back()[node.name] = reg;
            type_scopes.back()[node.name] = node.type;

            if (node.value) {
                const uint32_t value = gen_expr(*node.value);
                emit(IRAssign{ reg, value });
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
                emit(IRConst{ zero, 0 });
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
            // Compile-time only. Layout is already validated in semantic.
        },

        [&](const ast::FuncStmt& fn) {
            gen_function(fn);
        },

        [&](const ast::NamespaceStmt& ns) {
            namespace_stack.push_back(ns.name);

            // Namespace is a declaration scope, not a runtime flow barrier.
            const bool saved_terminated = current_terminated;
            current_terminated = false;

            local_scopes.emplace_back();
            type_scopes.emplace_back();

            if (ns.body) {
                for (const auto* child : ns.body->stmts) {
                    if (!child || current_terminated) {
                        break;
                    }
                    gen_stmt(*child);
                }
            }

            type_scopes.pop_back();
            local_scopes.pop_back();

            current_terminated = saved_terminated;
            namespace_stack.pop_back();
        },

        [&](const auto&) {
            crash("Unsupported statement node in IR generation");
        }
    }, stmt.kind);
}

uint32_t IRGenerator::gen_expr(const ast::Expr& expr) {
    auto lookup_local = [&](const std::string& name, uint32_t& reg_out, const ast::Type*& type_out) -> bool {
        for (int i = static_cast<int>(local_scopes.size()) - 1; i >= 0; --i) {
            auto rit = local_scopes[i].find(name);
            if (rit != local_scopes[i].end()) {
                reg_out = rit->second;
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

    auto resolve_function_id = [&](const std::string& name) -> uint32_t {
        const std::string qname = qualify_name(namespace_stack, name);
        auto it = function_ids.find(qname);
        if (it == function_ids.end()) {
            crash("Undefined function: " + qname);
        }
        return it->second;
    };

    return std::visit(overloaded {
        [&](const ast::IntExpr& node) -> uint32_t {
            const uint32_t dst = new_reg();
            emit(IRConst{ dst, node.value });
            return dst;
        },

        [&](const ast::StringExpr&) -> uint32_t {
            crash("String lowering is not implemented in this IR yet");
        },

        [&](const ast::VarExpr& node) -> uint32_t {
            uint32_t reg = 0;
            const ast::Type* type = nullptr;

            if (lookup_local(node.name, reg, type)) {
                (void)type;
                return reg;
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

            emit(IRBinary{
                map_op(node.op),
                dst,
                lhs,
                rhs
            });

            return dst;
        },

        [&](const ast::AssignExpr& node) -> uint32_t {
            if (!node.target || !node.value) {
                crash("Assignment target or value is missing");
            }

            const uint32_t value = gen_expr(*node.value);

            if (const auto* var = std::get_if<ast::VarExpr>(&node.target->kind)) {
                bool updated = false;

                for (int i = static_cast<int>(local_scopes.size()) - 1; i >= 0; --i) {
                    auto it = local_scopes[i].find(var->name);
                    if (it != local_scopes[i].end()) {
                        const uint32_t dst = new_reg();
                        emit(IRAssign{ dst, value });
                        it->second = dst;
                        updated = true;
                        return dst;
                    }
                }

                if (!updated) {
                    auto* sym = ctx.symbols.lookup(var->name);
                    if (!sym) {
                        crash("Undefined variable: " + var->name);
                    }

                    if (!symbol_is_mutable(*sym)) {
                        crash("Cannot assign to immutable variable: " + var->name);
                    }

                    crash("Global assignment lowering is not implemented yet: " + var->name);
                }
            }

            if (const auto* field = std::get_if<ast::FieldExpr>(&node.target->kind)) {
                const uint32_t base = gen_expr(*field->base);
                const ast::Type* base_type = nullptr;

                {
                    if (const auto* base_var = std::get_if<ast::VarExpr>(&field->base->kind)) {
                        for (int i = static_cast<int>(local_scopes.size()) - 1; i >= 0; --i) {
                            auto tit = type_scopes[i].find(base_var->name);
                            if (tit != type_scopes[i].end()) {
                                base_type = tit->second;
                                break;
                            }
                        }
                    }
                }

                if (!base_type) {
                    if (const auto* base_var = std::get_if<ast::VarExpr>(&field->base->kind)) {
                        auto* sym = ctx.symbols.lookup(base_var->name);
                        if (sym) {
                            base_type = symbol_type(*sym);
                        }
                    }
                }

                const auto [index, field_type] = resolve_struct_field(ctx, base_type, field->field);
                (void)field_type;

                emit(IRSetField{
                    base,
                    value,
                    index
                });

                return value;
            }

            crash("Assignment target must be variable or field access");
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
            }

            if (!base_type) {
                if (const auto* base_var = std::get_if<ast::VarExpr>(&node.base->kind)) {
                    auto* sym = ctx.symbols.lookup(base_var->name);
                    if (sym) {
                        base_type = symbol_type(*sym);
                    }
                }
            }

            const auto [index, field_type] = resolve_struct_field(ctx, base_type, node.field);
            (void)field_type;

            const uint32_t dst = new_reg();
            emit(IRGetField{
                dst,
                base,
                index
            });

            return dst;
        },

        [&](const ast::CallExpr& node) -> uint32_t {
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

            if (const auto* callee = std::get_if<ast::VarExpr>(&node.callee->kind)) {
                func_id = resolve_function_id(callee->name);
            } else {
                crash("Unsupported callee expression");
            }

            const uint32_t dst = new_reg();
            emit(IRCall{
                dst,
                func_id,
                args
            });

            return dst;
        }
    }, expr.kind);
}

} // namespace quark::codegen