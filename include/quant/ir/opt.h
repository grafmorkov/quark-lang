#pragma once

#include "quant/ir/ir.h"

namespace quant::codegen {

enum class OptLevel {
    O0 = 0,
    O1 = 1,
    O2 = 2,
    O3 = 3,
};

void optimize(IRProgram& program, OptLevel level, bool keep_all_functions = false);

} // namespace quant::codegenы
