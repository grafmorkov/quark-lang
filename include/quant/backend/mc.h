#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace quant::codegen::mc {

enum class RelType : uint32_t {
    X86_64_64   = 1,   // S + A
    X86_64_PC32 = 2,   // S + A - P
    X86_64_PLT32 = 4,  // PLT(S) + A - P
};

struct Relocation {
    uint32_t offset = 0;      // byte offset in the .text section
    std::string symbol;
    RelType type = RelType::X86_64_PC32;
    int64_t addend = 0;
};

enum class SymBind : uint8_t {
    Local = 0,
    Global = 1,
};

enum class SymType : uint8_t {
    Notype = 0,
    Object = 1,
    Func = 2,
};

struct Symbol {
    std::string name;
    SymBind bind = SymBind::Local;
    SymType type = SymType::Notype;
    bool undefined = false;
    uint32_t section = 0;   // 0 = .text, 1 = .data (internal ids)
    uint64_t value = 0;     // offset within the section
    uint64_t size = 0;
    std::string import_dll;   // non-empty: imported from a PE DLL (undefined symbol)
    std::string import_name;  // imported symbol name (defaults to `name`)
};

struct Object {
    std::vector<uint8_t> text;
    std::vector<uint8_t> data;
    std::vector<Relocation> relocs;
    std::vector<Symbol> symbols;
};

} // namespace quant::codegen::mc
