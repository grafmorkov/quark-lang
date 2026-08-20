#pragma once

#include <cstdint>
#include <vector>

#include "quant/ir/ir.h"
#include "quant/backend/mc.h"

namespace quant::codegen {

// Full native pipeline: IR -> instruction selection -> machine code -> ELF object.
struct NativeBackend {
    std::vector<uint8_t> generate(const IRProgram& program, mc::TargetArch arch = mc::TargetArch::X86_64);
};

} // namespace quant::codegen
