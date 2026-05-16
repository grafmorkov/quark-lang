#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <type_traits>
#include <variant>

#include "quark/ir/ir.h"
#include "codegen.h"
#include "utils/logger.h"

using namespace utils::logger;

namespace quark::codegen {

namespace {

template<class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

std::string asm_mangle(std::string name) {
    // Keep it simple and FASM-safe.
    for (char& ch : name) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (!(std::isalnum(c) || ch == '_')) {
            ch = '_';
        }
    }
    return name;
}

std::string label_name(uint32_t func_id, uint32_t label_id) {
    return "L_" + std::to_string(func_id) + "_" + std::to_string(label_id);
}

std::string function_name(const IRFunction& fn) {
    return "fn_" + std::to_string(fn.id) + "__" + asm_mangle(fn.name);
}

std::size_t align16(std::size_t n) {
    return (n + 15u) & ~std::size_t(15u);
}

} // namespace

struct FasmCodeGenerator final : CodeGenerator {
    std::ostringstream out;

    static std::string slot(Reg r) {
        return "[rbp - " + std::to_string((static_cast<std::size_t>(r) + 1u) * 8u) + "]";
    }

    static void append_max_reg(uint32_t& mx, Reg r) {
        if (r > mx) mx = r;
    }

    static uint32_t max_reg_in_func(const IRFunction& fn) {
        uint32_t mx = 0;

        for (const auto& inst : fn.body) {
            std::visit(overloaded{
                [&](const IRConst& x) {
                    append_max_reg(mx, x.dst);
                },
                [&](const IRBinary& x) {
                    append_max_reg(mx, x.dst);
                    append_max_reg(mx, x.lhs);
                    append_max_reg(mx, x.rhs);
                },
                [&](const IRAssign& x) {
                    append_max_reg(mx, x.dst);
                    append_max_reg(mx, x.src);
                },
                [&](const IRCall& x) {
                    append_max_reg(mx, x.dst);
                    for (Reg a : x.args) append_max_reg(mx, a);
                },
                [&](const IRReturn& x) {
                    append_max_reg(mx, x.value);
                },
                [&](const IRJump&) {
                },
                [&](const IRBranch& x) {
                    append_max_reg(mx, x.cond);
                },
                [&](const IRLabel&) {
                },
                [&](const IRGetField& x) {
                    append_max_reg(mx, x.dst);
                    append_max_reg(mx, x.base);
                },
                [&](const IRSetField& x) {
                    append_max_reg(mx, x.base);
                    append_max_reg(mx, x.value);
                },
            }, inst);
        }

        return mx;
    }

    void emit_line(const std::string& s = {}) {
        out << s << '\n';
    }

    void emit_load(Reg r) {
        emit_line("    mov rax, qword " + slot(r));
        emit_line("    push rax");
    }

    void emit_store(Reg r) {
        emit_line("    pop rax");
        emit_line("    mov qword " + slot(r) + ", rax");
    }

    void emit_binop(const IRBinary& x) {
        emit_load(x.lhs);
        emit_load(x.rhs);
        emit_line("    pop rbx");
        emit_line("    pop rax");

        switch (x.op) {
            case IRBinaryOp::Add:
                emit_line("    add rax, rbx");
                emit_line("    push rax");
                emit_store(x.dst);
                return;
            case IRBinaryOp::Sub:
                emit_line("    sub rax, rbx");
                emit_line("    push rax");
                emit_store(x.dst);
                return;
            case IRBinaryOp::Mul:
                emit_line("    imul rax, rbx");
                emit_line("    push rax");
                emit_store(x.dst);
                return;
            case IRBinaryOp::Div:
                emit_line("    cqo");
                emit_line("    idiv rbx");
                emit_line("    push rax");
                emit_store(x.dst);
                return;
            case IRBinaryOp::Eq:
            case IRBinaryOp::NotEq:
            case IRBinaryOp::Lt:
            case IRBinaryOp::Lte:
            case IRBinaryOp::Gt:
            case IRBinaryOp::Gte:
                emit_line("    cmp rax, rbx");
                switch (x.op) {
                    case IRBinaryOp::Eq:    emit_line("    sete al"); break;
                    case IRBinaryOp::NotEq: emit_line("    setne al"); break;
                    case IRBinaryOp::Lt:    emit_line("    setl al"); break;
                    case IRBinaryOp::Lte:   emit_line("    setle al"); break;
                    case IRBinaryOp::Gt:    emit_line("    setg al"); break;
                    case IRBinaryOp::Gte:   emit_line("    setge al"); break;
                    default: break;
                }
                emit_line("    movzx rax, al");
                emit_line("    push rax");
                emit_store(x.dst);
                return;
        }
    }

    void emit_call(const IRProgram& program, const IRCall& x) {
        // Push args right-to-left.
        for (auto it = x.args.rbegin(); it != x.args.rend(); ++it) {
            emit_load(*it);
        }

        if (x.func_id >= program.functions.size()) {
            crash("IRCall references invalid function id: " + std::to_string(x.func_id));
        }

        const auto& callee = program.functions[x.func_id];
        emit_line("    call " + function_name(callee));

        if (!x.args.empty()) {
            emit_line("    add rsp, " + std::to_string(x.args.size() * 8u));
        }

        emit_line("    mov qword " + slot(x.dst) + ", rax");
    }

    void emit_inst(const IRProgram& program, const IRFunction& fn, const IRInst& inst) {
        std::visit(overloaded{
            [&](const IRConst& x) {
                emit_line("    mov rax, " + std::to_string(x.value));
                emit_line("    mov qword " + slot(x.dst) + ", rax");
            },
            [&](const IRBinary& x) {
                emit_binop(x);
            },
            [&](const IRAssign& x) {
                emit_load(x.src);
                emit_store(x.dst);
            },
            [&](const IRCall& x) {
                emit_call(program, x);
            },
            [&](const IRReturn& x) {
                emit_load(x.value);
                emit_line("    pop rax");
                emit_line("    leave");
                emit_line("    ret");
            },
            [&](const IRJump& x) {
                emit_line("    jmp " + label_name(fn.id, x.target));
            },
            [&](const IRBranch& x) {
                emit_load(x.cond);
                emit_line("    pop rax");
                emit_line("    cmp rax, 0");
                emit_line("    jne " + label_name(fn.id, x.then_label));
                emit_line("    jmp " + label_name(fn.id, x.else_label));
            },
            [&](const IRLabel& x) {
                emit_line(label_name(fn.id, x.id) + ":");
            },
            [&](const IRGetField& x) {
                // Assumes structs are passed/stored as pointers.
                emit_load(x.base);
                emit_line("    pop rax");
                emit_line("    mov rax, qword [rax + " + std::to_string(x.index * 8) + "]");
                emit_line("    mov qword " + slot(x.dst) + ", rax");
            },
            [&](const IRSetField& x) {
                // Assumes structs are passed/stored as pointers.
                emit_load(x.base);
                emit_load(x.value);
                emit_line("    pop rbx");
                emit_line("    pop rax");
                emit_line("    mov qword [rax + " + std::to_string(x.index * 8) + "], rbx");
            },
        }, inst);
    }

    std::string generate(const IRProgram& program) override {
        out.str("");
        out.clear();

        emit_line("format ELF64");
        emit_line("segment readable executable");
        emit_line();

        for (const auto& fn : program.functions) {
            const uint32_t max_reg = max_reg_in_func(fn);
            const std::size_t stack_size = align16((static_cast<std::size_t>(max_reg) + 1u) * 8u);

            emit_line(function_name(fn) + ":");
            emit_line("    push rbp");
            emit_line("    mov rbp, rsp");
            if (stack_size > 0) {
                emit_line("    sub rsp, " + std::to_string(stack_size));
            }

            // Tiny internal calling convention:
            // caller pushes args right-to-left, callee copies them from [rbp+16+i*8].
            // This requires IRFunction::arg_count to be filled by IR generation.
            if (fn.arg_count > 0) {
                for (uint32_t i = 0; i < fn.arg_count; ++i) {
                    emit_line("    mov rax, qword [rbp + " + std::to_string(16u + i * 8u) + "]");
                    emit_line("    mov qword " + slot(i) + ", rax");
                }
            }

            for (const auto& inst : fn.body) {
                emit_inst(program, fn, inst);
            }

            // Fallback in case the IR fell through without an explicit return.
            emit_line("    xor rax, rax");
            emit_line("    leave");
            emit_line("    ret");
            emit_line();
        }

        return out.str();
    }
};

} // namespace quark::codegen
