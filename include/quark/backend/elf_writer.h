#pragma once

#include <cstdint>
#include <vector>

#include "quark/backend/mc.h"

namespace quark::codegen::elf {

// Serialize a machine-code object into an ELF64 relocatable file (.o).
std::vector<uint8_t> write(const mc::Object& obj);

} // namespace quark::codegen::elf
