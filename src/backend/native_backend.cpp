#include "quark/backend/native_backend.h"

#include "quark/backend/elf_writer.h"
#include "quark/backend/isel.h"
#ifdef _WIN32
#include "quark/backend/pe_writer.h"
#endif

namespace quark::codegen {

std::vector<uint8_t> NativeBackend::generate(const IRProgram& program) {
    ISel isel;
    isel.generate(program);
#ifdef _WIN32
    return pe::write(isel.obj);
#else
    return elf::write(isel.obj);
#endif
}

} // namespace quark::codegen
