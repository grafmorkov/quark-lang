#pragma once

#include <cstdint>
#include <vector>

#include "quant/ir/ir.h"

namespace quant::codegen {

// Full native pipeline: IR -> instruction selection -> machine code -> ELF object.
struct NativeBackend {
    std::vector<uint8_t> generate(const IRProgram& program);
};

} // namespace quant::codegen
