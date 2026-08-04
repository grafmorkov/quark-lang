#pragma once

#include <cstdint>
#include <vector>

#include "quanta/ir/ir.h"

namespace quanta::codegen {

// Full native pipeline: IR -> instruction selection -> machine code -> ELF object.
struct NativeBackend {
    std::vector<uint8_t> generate(const IRProgram& program);
};

} // namespace quanta::codegen
