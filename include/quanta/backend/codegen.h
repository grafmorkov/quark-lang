#pragma once
#include <iostream>
#include <string>

#include "quanta/ir/ir.h"

namespace quanta::codegen{
    struct CodeGenerator {
        virtual ~CodeGenerator() = default;
        virtual std::string generate(const IRProgram& ir) = 0;
    };
}