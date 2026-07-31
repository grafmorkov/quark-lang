#pragma once

#include <cstdint>
#include <vector>

#include "quark/ir/ir.h"

namespace quark::codegen {

// Full native pipeline: IR -> instruction selection -> machine code -> ELF object.
struct NativeBackend {
    std::vector<uint8_t> generate(const IRProgram& program);
};

} // namespace quark::codegen
