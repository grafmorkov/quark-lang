#pragma once

#include <cstdint>
#include <vector>

#include "quanta/backend/mc.h"

namespace quanta::codegen::elf {

// Serialize a machine-code object into an ELF64 relocatable file (.o).
std::vector<uint8_t> write(const mc::Object& obj);

} // namespace quanta::codegen::elf
