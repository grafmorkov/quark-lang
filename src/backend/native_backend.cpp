#include "quark/backend/native_backend.h"

#include "quark/backend/elf_writer.h"
#include "quark/backend/isel.h"

namespace quark::codegen {

std::vector<uint8_t> NativeBackend::generate(const IRProgram& program) {
    ISel isel;
    isel.generate(program);
    return elf::write(isel.obj);
}

} // namespace quark::codegen
