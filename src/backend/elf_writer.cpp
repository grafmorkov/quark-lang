#include "quant/backend/elf_writer.h"

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace quant::codegen::elf {

namespace {

enum : uint32_t {
    SHN_UNDEF = 0,
    SHT_PROGBITS = 1,
    SHT_SYMTAB = 2,
    SHT_STRTAB = 3,
    SHT_RELA = 4,
    SHF_WRITE = 0x1,
    SHF_ALLOC = 0x2,
    SHF_EXECINSTR = 0x4,
};

enum : uint32_t {
    SECTION_TEXT = 1,
    SECTION_RELA = 2,
    SECTION_DATA = 3,
    SECTION_SYMTAB = 4,
    SECTION_STRTAB = 5,
    SECTION_SHSTRTAB = 6,
    SECTION_COUNT = 7,
};

struct Buf {
    std::vector<uint8_t> data;

    void u8(uint8_t v) { data.push_back(v); }
    void u16(uint16_t v) {
        for (int i = 0; i < 2; ++i) u8(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) u8(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }
    void u64(uint64_t v) {
        for (int i = 0; i < 8; ++i) u8(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }
    void bytes(const void* p, std::size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        data.insert(data.end(), b, b + n);
    }
    void pad_to(std::size_t target) {
        while (data.size() < target) u8(0);
    }
};

std::size_t align8(std::size_t n) {
    return (n + 7u) & ~std::size_t(7u);
}
std::size_t align16(std::size_t n) {
    return (n + 15u) & ~std::size_t(15u);
}

// Build a string table ("\0" + entries) and return it with per-name offsets.
struct StringTable {
    std::vector<uint8_t> bytes;
    std::unordered_map<std::string, uint32_t> offsets;

    StringTable() {
        bytes.push_back(0);
    }
    uint32_t add(const std::string& name) {
        auto it = offsets.find(name);
        if (it != offsets.end()) return it->second;
        const uint32_t off = static_cast<uint32_t>(bytes.size());
        offsets[name] = off;
        bytes.insert(bytes.end(), name.begin(), name.end());
        bytes.push_back(0);
        return off;
    }
};

uint32_t elf_section_of(const mc::Symbol& s) {
    if (s.undefined) return SHN_UNDEF;
    return (s.section == 1) ? SECTION_DATA : SECTION_TEXT;
}

void write_shdr(Buf& b, uint32_t name, uint32_t type, uint64_t flags, uint64_t offset,
                uint64_t size, uint32_t link, uint32_t info, uint64_t addralign,
                uint64_t entsize) {
    b.u32(name);
    b.u32(type);
    b.u64(flags);
    b.u64(0); // sh_addr
    b.u64(offset);
    b.u64(size);
    b.u32(link);
    b.u32(info);
    b.u64(addralign);
    b.u64(entsize);
}

} // namespace

std::vector<uint8_t> write(const mc::Object& obj, mc::TargetArch arch) {
    // symbols (locals first, then globals)
    std::vector<const mc::Symbol*> syms;
    for (const auto& s : obj.symbols) {
        if (s.bind == mc::SymBind::Local) syms.push_back(&s);
    }
    const uint32_t first_global = static_cast<uint32_t>(syms.size()) + 1; // +1 for the null entry
    for (const auto& s : obj.symbols) {
        if (s.bind == mc::SymBind::Global) syms.push_back(&s);
    }

    // string tables
    StringTable strtab;
    for (const auto* s : syms) strtab.add(s->name);

    StringTable shstrtab;
    shstrtab.add(".text");
    shstrtab.add(".rela.text");
    shstrtab.add(".data");
    shstrtab.add(".symtab");
    shstrtab.add(".strtab");
    shstrtab.add(".shstrtab");

    // layout
    const uint32_t text_size = static_cast<uint32_t>(obj.text.size());
    const uint32_t rela_size = static_cast<uint32_t>(obj.relocs.size()) * 24;
    const uint32_t data_size = static_cast<uint32_t>(obj.data.size());
    const uint32_t symtab_size = static_cast<uint32_t>(1 + syms.size()) * 24;
    const uint32_t strtab_size = static_cast<uint32_t>(strtab.bytes.size());
    const uint32_t shstrtab_size = static_cast<uint32_t>(shstrtab.bytes.size());

    const uint32_t text_off = 64; // ehdr
    const uint32_t rela_off = static_cast<uint32_t>(align16(static_cast<std::size_t>(text_off) + text_size));
    const uint32_t data_off = static_cast<uint32_t>(align8(static_cast<std::size_t>(rela_off) + rela_size));
    const uint32_t symtab_off = static_cast<uint32_t>(align8(static_cast<std::size_t>(data_off) + data_size));
    const uint32_t strtab_off = symtab_off + symtab_size;
    const uint32_t shstrtab_off = strtab_off + strtab_size;
    const uint32_t shoff = static_cast<uint32_t>(align8(static_cast<std::size_t>(shstrtab_off) + shstrtab_size));

    Buf b;

    // ELF header
    b.u8(0x7F); b.u8('E'); b.u8('L'); b.u8('F');
    b.u8(2);   // EI_CLASS: ELFCLASS64
    b.u8(1);   // EI_DATA: ELFDATA2LSB
    b.u8(1);   // EI_VERSION
    b.u8(0);   // EI_OSABI
    b.u8(0);   // EI_ABIVERSION
    for (int i = 0; i < 7; ++i) b.u8(0); // padding
    b.u16(1);  // e_type: ET_REL
    b.u16(arch == mc::TargetArch::AARCH64 ? 183 : 62); // e_machine: EM_AARCH64 or EM_X86_64
    b.u32(1);  // e_version
    b.u64(0);  // e_entry
    b.u64(0);  // e_phoff
    b.u64(shoff); // e_shoff
    b.u32(0);  // e_flags
    b.u16(64); // e_ehsize
    b.u16(0);  // e_phentsize
    b.u16(0);  // e_phnum
    b.u16(64); // e_shentsize
    b.u16(SECTION_COUNT); // e_shnum
    b.u16(SECTION_SHSTRTAB); // e_shstrndx

    // sections
    b.pad_to(text_off);
    b.bytes(obj.text.data(), obj.text.size());

    b.pad_to(rela_off);
    {
        std::unordered_map<std::string, uint32_t> sym_index;
        for (uint32_t i = 0; i < syms.size(); ++i) {
            sym_index[syms[i]->name] = i + 1; // +1 for null entry
        }
        for (const auto& r : obj.relocs) {
            b.u64(r.offset);
            b.u64((static_cast<uint64_t>(sym_index.at(r.symbol)) << 32) | static_cast<uint32_t>(r.type));
            b.u64(static_cast<uint64_t>(r.addend));
        }
    }

    b.pad_to(data_off);
    b.bytes(obj.data.data(), obj.data.size());

    b.pad_to(symtab_off);
    {
        b.u32(0); b.u8(0); b.u8(0); b.u16(0); b.u64(0); b.u64(0); // null symbol
        for (const auto* s : syms) {
            b.u32(strtab.offsets.at(s->name));
            b.u8(static_cast<uint8_t>((static_cast<uint8_t>(s->bind) << 4) |
                                      static_cast<uint8_t>(s->type)));
            b.u8(0); // st_other
            b.u16(static_cast<uint16_t>(elf_section_of(*s)));
            b.u64(s->undefined ? 0 : s->value);
            b.u64(s->size);
        }
    }

    b.pad_to(strtab_off);
    b.bytes(strtab.bytes.data(), strtab.bytes.size());

    b.pad_to(shstrtab_off);
    b.bytes(shstrtab.bytes.data(), shstrtab.bytes.size());

    // section header table
    b.pad_to(shoff);
    write_shdr(b, 0, 0, 0, 0, 0, 0, 0, 0, 0); // null
    write_shdr(b, shstrtab.offsets.at(".text"), SHT_PROGBITS,
               SHF_ALLOC | SHF_EXECINSTR, text_off, text_size, 0, 0, 16, 0);
    write_shdr(b, shstrtab.offsets.at(".rela.text"), SHT_RELA,
               0, rela_off, rela_size, SECTION_SYMTAB, SECTION_TEXT, 8, 24);
    write_shdr(b, shstrtab.offsets.at(".data"), SHT_PROGBITS,
               SHF_ALLOC | SHF_WRITE, data_off, data_size, 0, 0, 8, 0);
    write_shdr(b, shstrtab.offsets.at(".symtab"), SHT_SYMTAB,
               0, symtab_off, symtab_size, SECTION_STRTAB, first_global, 8, 24);
    write_shdr(b, shstrtab.offsets.at(".strtab"), SHT_STRTAB,
               0, strtab_off, strtab_size, 0, 0, 1, 0);
    write_shdr(b, shstrtab.offsets.at(".shstrtab"), SHT_STRTAB,
               0, shstrtab_off, shstrtab_size, 0, 0, 1, 0);

    return std::move(b.data);
}

} // namespace quant::codegen::elf
