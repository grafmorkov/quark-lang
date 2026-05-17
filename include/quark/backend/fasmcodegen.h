#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
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

const IRFunction* find_main_function(const IRProgram& program) {
    for (const auto& fn : program.functions) {
        if (fn.name == "main") {
            return &fn;
        }
    }
    return nullptr;
}

} // namespace

struct FasmCodeGenerator final : CodeGenerator {
    std::ostringstream out;

    static std::string local_slot(Local l) {
        return "[rbp - " +
            std::to_string((static_cast<std::size_t>(l) + 1u) * 8u) +
            "]";
    }

    static std::string temp_slot(Reg r, const IRFunction& fn) {
        const std::size_t base = static_cast<std::size_t>(fn.local_count);
        return "[rbp - " +
            std::to_string((base + static_cast<std::size_t>(r) + 1u) * 8u) +
            "]";
    }

    void emit_line(const std::string& s = {}) {
        out << s << '\n';
    }

    void emit_load(Reg r, const IRFunction& fn) {
        emit_line("    mov rax, qword " + temp_slot(r, fn));
        emit_line("    push rax");
    }

    void emit_store(Reg r, const IRFunction& fn) {
        emit_line("    pop rax");
        emit_line("    mov qword " + temp_slot(r, fn) + ", rax");
    }

    void emit_binop(const IRBinary& x, const IRFunction& fn) {
        emit_load(x.lhs, fn);
        emit_load(x.rhs, fn);

        emit_line("    pop rbx");
        emit_line("    pop rax");

        switch (x.op) {
            case IRBinaryOp::Add:
                emit_line("    add rax, rbx");
                break;
            case IRBinaryOp::Sub:
                emit_line("    sub rax, rbx");
                break;
            case IRBinaryOp::Mul:
                emit_line("    imul rax, rbx");
                break;
            case IRBinaryOp::Div:
                emit_line("    cqo");
                emit_line("    idiv rbx");
                break;
            case IRBinaryOp::Eq:
            case IRBinaryOp::NotEq:
            case IRBinaryOp::Lt:
            case IRBinaryOp::Lte:
            case IRBinaryOp::Gt:
            case IRBinaryOp::Gte:
                emit_line("    cmp rax, rbx");
                switch (x.op) {
                    case IRBinaryOp::Eq:    emit_line("    sete al"); break;
                    case IRBinaryOp::NotEq:  emit_line("    setne al"); break;
                    case IRBinaryOp::Lt:     emit_line("    setl al"); break;
                    case IRBinaryOp::Lte:    emit_line("    setle al"); break;
                    case IRBinaryOp::Gt:     emit_line("    setg al"); break;
                    case IRBinaryOp::Gte:    emit_line("    setge al"); break;
                    default: break;
                }
                emit_line("    movzx rax, al");
                break;
        }

        emit_line("    mov qword " + temp_slot(x.dst, fn) + ", rax");
    }

    void emit_call(const IRProgram& program, const IRFunction& fn, const IRCall& x) {
        for (auto it = x.args.rbegin(); it != x.args.rend(); ++it) {
            emit_load(*it, fn);
        }

        if (x.func_id >= program.functions.size()) {
            crash("IRCall references invalid function id: " + std::to_string(x.func_id));
        }

        const auto& callee = program.functions[x.func_id];
        emit_line("    call " + function_name(callee));

        if (!x.args.empty()) {
            emit_line("    add rsp, " + std::to_string(x.args.size() * 8u));
        }

        emit_line("    mov qword " + temp_slot(x.dst, fn) + ", rax");
    }

    void emit_inst(const IRProgram& program, const IRFunction& fn, const IRInst& inst) {
        std::visit(overloaded{
            [&](const IRLoadConst& x) {
                emit_line("    mov rax, " + std::to_string(x.value));
                emit_line("    mov qword " + temp_slot(x.dst, fn) + ", rax");
            },

            [&](const IRLoadLocal& x) {
                emit_line("    mov rax, qword " + local_slot(x.local));
                emit_line("    mov qword " + temp_slot(x.dst, fn) + ", rax");
            },

            [&](const IRStoreLocal& x) {
                emit_line("    mov rax, qword " + temp_slot(x.src, fn));
                emit_line("    mov qword " + local_slot(x.local) + ", rax");
            },

            [&](const IRBinary& x) {
                emit_binop(x, fn);
            },

            [&](const IRCall& x) {
                emit_call(program, fn, x);
            },

            [&](const IRReturn& x) {
                emit_line("    mov rax, qword " + temp_slot(x.value, fn));
                emit_line("    leave");
                emit_line("    ret");
            },

            [&](const IRJump& x) {
                emit_line("    jmp " + label_name(fn.id, x.target));
            },

            [&](const IRBranch& x) {
                emit_line("    mov rax, qword " + temp_slot(x.cond, fn));
                emit_line("    cmp rax, 0");
                emit_line("    jne " + label_name(fn.id, x.then_label));
                emit_line("    jmp " + label_name(fn.id, x.else_label));
            },

            [&](const IRLabel& x) {
                emit_line(label_name(fn.id, x.id) + ":");
            },

            [&](const IRGetField& x) {
                emit_line("    mov rax, qword " + temp_slot(x.base, fn));
                emit_line("    mov rax, qword [rax + " + std::to_string(x.offset) + "]");
                emit_line("    mov qword " + temp_slot(x.dst, fn) + ", rax");
            },

            [&](const IRSetField& x) {
                emit_line("    mov rax, qword " + temp_slot(x.base, fn));
                emit_line("    mov rbx, qword " + temp_slot(x.value, fn));
                emit_line("    mov qword [rax + " + std::to_string(x.offset) + "], rbx");
            }
        }, inst);
    }

    std::string generate(const IRProgram& program) override {
        out.str("");
        out.clear();

#ifdef _WIN32
        emit_line("format PE64 console");
        emit_line("entry start");
        emit_line();
        emit_line("section '.text' code readable executable");
#else
        emit_line("format ELF64 executable 3");
        emit_line("entry start");
        emit_line("segment readable executable");
#endif

        emit_line();

        for (const auto& fn : program.functions) {
            const std::size_t stack_size =
                align16(
                    (static_cast<std::size_t>(fn.local_count) +
                     static_cast<std::size_t>(fn.temp_count)) * 8u
                );

            emit_line(function_name(fn) + ":");
            emit_line("    push rbp");
            emit_line("    mov rbp, rsp");

            if (stack_size > 0) {
                emit_line("    sub rsp, " + std::to_string(stack_size));
            }

            for (uint32_t i = 0; i < fn.arg_count; ++i) {
                emit_line("    mov rax, qword [rbp + " + std::to_string(16u + i * 8u) + "]");
                emit_line("    mov qword " + local_slot(i) + ", rax");
            }

            for (const auto& inst : fn.body) {
                emit_inst(program, fn, inst);
            }

            emit_line();
        }

        const IRFunction* main_fn = find_main_function(program);
        if (!main_fn) {
            crash("No main function found");
        }

#ifdef _WIN32
        emit_line("start:");
        emit_line("    call " + function_name(program.functions.at(0)));
        emit_line("    mov rcx, rax");
        emit_line("    call [ExitProcess]");

        emit_line();
        emit_line("section '.idata' import data readable writeable");

        emit_line("idata:");
        emit_line("    dd 0,0,0,rva kernel_name,rva kernel_table");
        emit_line("    dd 0,0,0,0,0");

        emit_line("kernel_table:");
        emit_line("ExitProcess dq rva _ExitProcess");
        emit_line("    dq 0");

        emit_line("_ExitProcess:");
        emit_line("    dw 0");
        emit_line("    db 'ExitProcess',0");

        emit_line("kernel_name db 'KERNEL32.DLL',0");
#else
        emit_line("start:");
        emit_line("    call " + function_name(*main_fn));
        emit_line("    mov rdi, rax");
        emit_line("    mov rax, 60");
        emit_line("    syscall");
#endif

        return out.str();
    }
};

} // namespace quark::codegen