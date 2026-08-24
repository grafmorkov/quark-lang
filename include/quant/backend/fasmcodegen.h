#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <variant>

#include "quant/ir/ir.h"
#include "quant/backend/mc.h"
#include "codegen.h"
#include "utils/logger.h"

using namespace utils::logger;

namespace quant::codegen {

struct FasmCodeGenerator final : CodeGenerator {
    public:
        std::ostringstream out;
        // Output OS (not host): selects PE64 vs ELF64 and the calling
        // convention used when lowering calls.
        mc::TargetOS target_os = mc::TargetOS::Linux;
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

        void emit_region_begin(const IRRegionBegin& x, const IRFunction& fn);
        void emit_region_alloc(const IRRegionAlloc& x, const IRFunction& fn);
        void emit_region_end(const IRRegionEnd& x, const IRFunction& fn);
        uint32_t region_alloc_label();
        uint32_t next_region_label = 0;

};

} // namespace quant::codegen