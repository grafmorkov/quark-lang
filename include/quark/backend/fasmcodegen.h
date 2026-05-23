#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <variant>

#include "quark/ir/ir.h"
#include "codegen.h"
#include "utils/logger.h"

using namespace utils::logger;

namespace quark::codegen {

struct FasmCodeGenerator final : CodeGenerator {
    public:
        std::ostringstream out;
        std::string generate(const IRProgram& program) override;

    private:
        std::string local_slot(Local l);
        std::string temp_slot(Reg r, const IRFunction& fn);

        void emit_line(const std::string& s = {});
        void emit_load(Reg r, const IRFunction& fn);

        void emit_store(Reg r, const IRFunction& fn);

        void emit_binop(const IRBinary& x, const IRFunction& fn);

        void emit_call(const IRProgram& program, const IRFunction& fn, const IRCall& x);

        void emit_inst(const IRProgram& program, const IRFunction& fn, const IRInst& inst);

};

} // namespace quark::codegen