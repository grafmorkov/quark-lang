#pragma once
#include <iostream>
#include <string>

#include "quark/ir/ir.h"

namespace quark::codegen{
    struct CodeGenerator {
        virtual ~CodeGenerator() = default;
        virtual std::string generate(const IRProgram& ir) = 0;
    };
}