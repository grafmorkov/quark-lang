#include "quark/backend/fasmcodegen.h"
namespace quark::codegen{
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
    std::string abi_name(const IRFunction& fn) {
        if (fn.is_extern) {
            return "qk_" + asm_mangle(fn.name);
        }
        return function_name(fn);
    }

    std::size_t align16(std::size_t n) {
        return (n + 15u) & ~std::size_t(15u);
    }

    const IRFunction* find_entry(const IRProgram& program) {
        for (const auto& fn : program.functions) {
            if (fn.name.ends_with("main")) {
                return &fn;
            }
        }
        return nullptr;
    }
      std::string string_label(uint32_t id) {
        return "str_" + std::to_string(id);
    }

    std::string db_bytes(const std::string& s) {
        std::string out = "    db ";
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (i != 0) out += ", ";
            out += std::to_string(static_cast<unsigned>(
                static_cast<unsigned char>(s[i])
            ));
        }
        if (!s.empty()) out += ", ";
        out += "0";
        return out;
    }
} // namespace

// FasmCodeGenerator
    std::string FasmCodeGenerator::local_slot(Local l) {
            return "[rbp - " +
                std::to_string((static_cast<std::size_t>(l) + 1u) * 8u) +
                "]";
        }

    std::string FasmCodeGenerator::temp_slot(Reg r, const IRFunction& fn) {
            const std::size_t base = static_cast<std::size_t>(fn.local_count);
            return "[rbp - " +
                std::to_string((base + static_cast<std::size_t>(r) + 1u) * 8u) +
                "]";
    }
    void FasmCodeGenerator::emit_line(const std::string& s){
        out << s << '\n';
    }
    void FasmCodeGenerator::emit_load(Reg r, const IRFunction& fn) {
        emit_line("    mov rax, qword " + temp_slot(r, fn));
        emit_line("    push rax");
    }
    void FasmCodeGenerator::emit_store(Reg r, const IRFunction& fn) {
            emit_line("    pop rax");
            emit_line("    mov qword " + temp_slot(r, fn) + ", rax");
    }
    void FasmCodeGenerator::emit_binop(const IRBinary& x, const IRFunction& fn) {
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
    void FasmCodeGenerator::emit_call(const IRProgram& program, const IRFunction& fn, const IRCall& x) {
        if (x.func_id >= program.functions.size()) {
            crash("IRCall references invalid function id: " + std::to_string(x.func_id));
        }

        const auto& callee = program.functions[x.func_id];
        const std::size_t n = x.args.size();

#ifdef _WIN32
        constexpr std::size_t max_reg = 4;
        constexpr std::size_t shadow = 32;
#else
        constexpr std::size_t max_reg = 6;
        constexpr std::size_t shadow = 0;
#endif
        const std::size_t stack_count = (n > max_reg) ? (n - max_reg) : 0;
        const std::size_t frame = shadow + stack_count * 8;

        if (frame > 0) {
            emit_line("    sub rsp, " + std::to_string(frame));
        }

        // stack args (above shadow space or at [rsp])
        for (std::size_t i = n; i > max_reg; --i) {
            const std::size_t idx = i - 1;
            const std::size_t slot = idx - max_reg;
            emit_line("    mov rax, qword " + temp_slot(x.args[idx], fn));
            emit_line("    mov qword [rsp + " + std::to_string(shadow + slot * 8) + "], rax");
        }

        // register args
#ifdef _WIN32
        static const char* const win_regs[] = {"rcx", "rdx", "r8", "r9"};
        for (std::size_t i = 0; i < n && i < max_reg; ++i) {
            emit_line("    mov " + std::string(win_regs[i]) + ", qword " + temp_slot(x.args[i], fn));
        }
#else
        static const char* const linux_regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        for (std::size_t i = 0; i < n && i < max_reg; ++i) {
            emit_line("    mov " + std::string(linux_regs[i]) + ", qword " + temp_slot(x.args[i], fn));
        }
#endif

        emit_line("    call " + abi_name(callee));

        if (frame > 0) {
            emit_line("    add rsp, " + std::to_string(frame));
        }

        emit_line("    mov qword " + temp_slot(x.dst, fn) + ", rax");
    }
    void FasmCodeGenerator::emit_inst(const IRProgram& program, const IRFunction& fn, const IRInst& inst) {
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
            },
            [&](const IRLoadString& x) {
                if (x.string_id >= program.strings.size()) {
                    crash("IRLoadString references invalid string id: " + std::to_string(x.string_id));
                }

                const auto& lit = program.strings[x.string_id];
                emit_line("    lea rax, [" + string_label(lit.id) + "]");
                emit_line("    mov qword " + temp_slot(x.dst, fn) + ", rax");
            },
        }, inst);
    }
    std::string FasmCodeGenerator::generate(const IRProgram& program) {
        out.str("");
        out.clear();

#ifdef _WIN32
        emit_line("format PE64 console");
        emit_line("entry start");
        emit_line();
        emit_line("include 'qkrt\\common\\string.asm'");
        emit_line("include 'qkrt\\windows\\file.asm'");
        emit_line("include 'qkrt\\windows\\io.asm'");
        emit_line();
        emit_line("section '.text' code readable executable");
#else
        emit_line("format ELF64 executable 3");
        emit_line("entry start");
        emit_line("segment readable executable");
#endif
#ifndef _WIN32
        for (const auto& fn : program.functions) {
            if (fn.is_extern) {
                emit_line("extrn " + abi_name(fn));
            }
        }
#endif
        for (const auto& fn : program.functions) {
            if (fn.is_extern) {
                continue;
            }
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
#ifdef _WIN32
                if (i < 4) {
                    static const char* regs[] = {"rcx", "rdx", "r8", "r9"};
                    emit_line("    mov qword " + local_slot(i) + ", " + regs[i]);
                } else {
                    emit_line("    mov rax, qword [rbp + " + std::to_string(16u + (i - 4u) * 8u) + "]");
                    emit_line("    mov qword " + local_slot(i) + ", rax");
                }
#else
                if (i < 6) {
                    static const char* regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
                    emit_line("    mov qword " + local_slot(i) + ", " + regs[i]);
                } else {
                    emit_line("    mov rax, qword [rbp + " + std::to_string(16u + (i - 6u) * 8u) + "]");
                    emit_line("    mov qword " + local_slot(i) + ", rax");
                }
#endif
            }

            for (const auto& inst : fn.body) {
                emit_inst(program, fn, inst);
            }

            emit_line();
        }

        // const IRFunction* main_fn = find_main_function(program);
        // if (!main_fn) {
        //     crash("No main function found");
        // }
        // Removed because the linker is checking all the things now

#ifdef _WIN32
        emit_line("start:");
        emit_line("    call qk_io_init");
        emit_line("    call " + function_name(*find_entry(program)));
        emit_line("    mov rcx, rax");
        emit_line("    call [ExitProcess]");

        emit_line();
        emit_line("section '.idata' import data readable writeable");
        emit_line("idata:");
        emit_line("    dd 0,0,0,rva kernel_name,rva kernel_table");
        emit_line("    dd 0,0,0,0,0");
        emit_line("kernel_table:");
        emit_line("ExitProcess dq rva _ExitProcess");
        emit_line("CreateFileA dq rva _CreateFileA");
        emit_line("CloseHandle dq rva _CloseHandle");
        emit_line("SetFilePointerEx dq rva _SetFilePointerEx");
        emit_line("FlushFileBuffers dq rva _FlushFileBuffers");
        emit_line("WriteFile dq rva _WriteFile");
        emit_line("ReadFile dq rva _ReadFile");
        emit_line("GetLastError dq rva _GetLastError");
        emit_line("GetStdHandle dq rva _GetStdHandle");
        emit_line("    dq 0");
        emit_line("_ExitProcess:");
        emit_line("    dw 0");
        emit_line("    db 'ExitProcess',0");
        emit_line("_CreateFileA:");
        emit_line("    dw 0");
        emit_line("    db 'CreateFileA',0");
        emit_line("_CloseHandle:");
        emit_line("    dw 0");
        emit_line("    db 'CloseHandle',0");
        emit_line("_SetFilePointerEx:");
        emit_line("    dw 0");
        emit_line("    db 'SetFilePointerEx',0");
        emit_line("_FlushFileBuffers:");
        emit_line("    dw 0");
        emit_line("    db 'FlushFileBuffers',0");
        emit_line("_WriteFile:");
        emit_line("    dw 0");
        emit_line("    db 'WriteFile',0");
        emit_line("_ReadFile:");
        emit_line("    dw 0");
        emit_line("    db 'ReadFile',0");
        emit_line("_GetLastError:");
        emit_line("    dw 0");
        emit_line("    db 'GetLastError',0");
        emit_line("_GetStdHandle:");
        emit_line("    dw 0");
        emit_line("    db 'GetStdHandle',0");
        emit_line("kernel_name db 'KERNEL32.DLL',0");
#else
        emit_line("start:");
        emit_line("    call " + function_name(*find_entry(program)));
        emit_line("    mov rdi, rax");
        emit_line("    mov rax, 60");
        emit_line("    syscall");
#endif
#ifdef _WIN32
        emit_line("section '.data' data readable writeable");
#else
        emit_line("segment readable writeable");
#endif

        for (const auto& s : program.strings) {
            emit_line(string_label(s.id) + ":");
            emit_line(db_bytes(s.value));
        }
        return out.str();
    }
} // namespace quark::codegen
