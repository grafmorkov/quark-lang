#include "quant/backend/native_backend.h"

#include "quant/backend/elf_writer.h"
#include "quant/backend/isel.h"
#ifdef _WIN32
#include "quant/backend/pe_writer.h"
#endif

namespace quant::codegen {

std::vector<uint8_t> NativeBackend::generate(const IRProgram& program) {
    ISel isel;
    isel.generate(program);
#ifdef _WIN32
    return pe::write(isel.obj);
#else
    return elf::write(isel.obj);
#endif
}

} // namespace quant::codegen
