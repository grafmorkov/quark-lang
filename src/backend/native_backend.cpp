#include "quant/backend/native_backend.h"

#include "quant/backend/mc.h"

#ifdef QUANT_HAS_ELF
#include "quant/backend/elf_writer.h"
#endif
#ifdef QUANT_HAS_PE
#include "quant/backend/pe_writer.h"
#endif
#ifdef QUANT_HAS_X86_64
#include "quant/backend/isel.h"
#endif
#ifdef QUANT_HAS_AARCH64
#include "quant/backend/aarch64_isel.h"
#endif

namespace quant::codegen {

namespace {

[[noreturn]] void backend_not_compiled(const char* name) {
    utils::logger::crash(std::string(name) +
                         " code generator is not compiled into this binary "
                         "(enable it via -DQUANT_BACKENDS=...)");
}

} // namespace

std::vector<uint8_t> NativeBackend::generate(const IRProgram& program, mc::TargetArch arch, mc::TargetOS os) {
    // Executable flavor is decided by the target OS, not by the host:
    // the elf/pe writers are portable byte emitters. CMake compiles in only
    // the writers and instruction selectors that some enabled backend needs.
    const bool pe = (os == mc::TargetOS::Windows);

    if (arch == mc::TargetArch::AARCH64) {
#ifdef QUANT_HAS_AARCH64
        AArch64ISel isel;
        isel.target_os = os;
        isel.generate(program);
#else
        backend_not_compiled("AArch64");
#endif
#if defined(QUANT_HAS_AARCH64) && defined(QUANT_HAS_PE)
        if (pe) return pe::write(isel.obj);
#endif
#if defined(QUANT_HAS_AARCH64) && defined(QUANT_HAS_ELF)
        return elf::write(isel.obj, arch);
#endif
        backend_not_compiled("executable writer");
    }

#ifdef QUANT_HAS_X86_64
    ISel isel;
    isel.target_os = os;
    isel.generate(program);
#else
    backend_not_compiled("x86-64");
#endif
#if defined(QUANT_HAS_X86_64) && defined(QUANT_HAS_PE)
    if (pe) return pe::write(isel.obj);
#endif
#if defined(QUANT_HAS_X86_64) && defined(QUANT_HAS_ELF)
    return elf::write(isel.obj);
#endif
    backend_not_compiled("executable writer");
}

} // namespace quant::codegen
