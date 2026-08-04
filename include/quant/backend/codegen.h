#pragma once
#include <iostream>
#include <string>

#include "quant/ir/ir.h"

namespace quant::codegen{
    struct CodeGenerator {
        virtual ~CodeGenerator() = default;
        virtual std::string generate(const IRProgram& ir) = 0;
    };
}