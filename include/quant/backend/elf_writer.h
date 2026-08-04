#pragma once

#include <cstdint>
#include <vector>

#include "quant/backend/mc.h"

namespace quant::codegen::elf {

// Serialize a machine-code object into an ELF64 relocatable file (.o).
std::vector<uint8_t> write(const mc::Object& obj);

} // namespace quant::codegen::elf
