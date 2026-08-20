#include "quant/backend/native_backend.h"

#include "quant/backend/elf_writer.h"
#include "quant/backend/isel.h"
#include "quant/backend/aarch64_isel.h"
#ifdef _WIN32
#include "quant/backend/pe_writer.h"
#endif

namespace quant::codegen {

std::vector<uint8_t> NativeBackend::generate(const IRProgram& program, mc::TargetArch arch) {
    if (arch == mc::TargetArch::AARCH64) {
        AArch64ISel isel;
        isel.generate(program);
#ifdef _WIN32
        return pe::write(isel.obj, arch);
#else
        return elf::write(isel.obj, arch);
#endif
    }

    ISel isel;
    isel.generate(program);
#ifdef _WIN32
    return pe::write(isel.obj);
#else
    return elf::write(isel.obj);
#endif
}

} // namespace quant::codegen
