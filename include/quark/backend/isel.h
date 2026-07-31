#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "quark/backend/mc.h"
#include "quark/backend/x86_64.h"
#include "quark/ir/ir.h"

namespace quark::codegen {

// IR -> x86-64 machine instructions (+ relocations), before the ELF writer.
struct ISel {
    mc::Object obj;
    x86::Emitter text;

    uint32_t next_region_label = 0;

    // symbol name -> index in obj.symbols
    std::unordered_map<std::string, uint32_t> symbol_index;

    // label key -> text offset (IR labels and region labels are separate)
    std::unordered_map<uint32_t, uint32_t> label_pos;
    std::unordered_map<uint32_t, uint32_t> region_label_pos;

    struct Fixup {
        uint32_t field_offset;
        uint32_t key;
        bool region;
    };
    std::vector<Fixup> fixups;

    void generate(const IRProgram& program);

private:
    x86::Mem local_slot(Local l);
    x86::Mem temp_slot(Reg r, const IRFunction& fn);

    uint32_t ensure_symbol(const std::string& name, mc::SymBind bind, mc::SymType type,
                           bool undefined, uint32_t section, uint64_t value, uint64_t size,
                           const std::string& import_dll = {},
                           const std::string& import_name = {});
    uint32_t add_runtime_symbol(const std::string& name);
    uint32_t add_func_symbol(const IRFunction& fn);
    uint32_t add_import_symbol(const std::string& dll, const std::string& name);

    void emit_func(const IRProgram& program, const IRFunction& fn);
    void emit_syscall_stub(const IRFunction& fn);
    void emit_prologue(const IRFunction& fn);
    void emit_inst(const IRProgram& program, const IRFunction& fn, const IRInst& inst);
    void emit_binop(const IRBinary& x, const IRFunction& fn);
    void emit_call(const IRProgram& program, const IRFunction& fn, const IRCall& x);
    void emit_load(Reg r, const IRFunction& fn);
    void emit_store(Reg r, const IRFunction& fn);
    void emit_region_begin(const IRRegionBegin& x);
    void emit_region_alloc(const IRRegionAlloc& x, const IRFunction& fn);
    void emit_region_end(const IRRegionEnd& x);

    uint32_t region_alloc_label() { return next_region_label++; }

    void emit_start(const IRProgram& program);
    void emit_strings(const IRProgram& program);
    void emit_globals(const IRProgram& program);
    void patch_fixups();

    static const IRFunction* find_entry(const IRProgram& program);

    static std::string asm_mangle(std::string name);
    static std::string function_name(const IRFunction& fn);
    static std::string abi_name(const IRFunction& fn);
    static std::string string_label(uint32_t id);
    static std::string global_label(uint32_t id);
    static std::size_t align16(std::size_t n);
};

} // namespace quark::codegen
