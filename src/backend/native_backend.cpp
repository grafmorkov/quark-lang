#include "quanta/backend/native_backend.h"

#include "quanta/backend/elf_writer.h"
#include "quanta/backend/isel.h"
#ifdef _WIN32
#include "quanta/backend/pe_writer.h"
#endif

namespace quanta::codegen {

std::vector<uint8_t> NativeBackend::generate(const IRProgram& program) {
    ISel isel;
    isel.generate(program);
#ifdef _WIN32
    return pe::write(isel.obj);
#else
    return elf::write(isel.obj);
#endif
}

} // namespace quanta::codegen
