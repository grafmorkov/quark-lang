#pragma once
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <string>

#include "codegen.h"
#include "quark/frontend/ast.h"
#include "quark/support/type_context.h"
#include "utils/logger.h"

using namespace quark::ast;
using namespace quark::types;
using namespace utils::logger;

namespace quark::codegen {

    struct CGenerator : CodeGenerator {
        std::ostringstream out;
        const TypeContext& type_ctx;
        const IRBuilder* builder;
        
        CGenerator(const TypeContext& ctx, const IRBuilder& _builder) : type_ctx(ctx), builder(&_builder) {}

        std::string generate(const IRBuilder& builder) override;

        void emit_struct_defs(const IRBuilder& builder);

        void emit_block(const IRBlock& block);

        template<typename T>
        void emit_inst(const T&) {
            static_assert(sizeof(T) == 0, "Unhandled IR node");
        }
        void emit_inst(const IRBinary& node);
        void emit_inst(const IRStore& node);
        void emit_inst(const IRReturn& node);
        void emit_inst(const IRJump& node);
        void emit_inst(const IRBranch& node);
        void emit_inst(const IRCall& node);
        void emit_inst(const IRAlloc& node);
        void emit_inst(const IRGetField& node);
        void emit_inst(const IRSetField& node);
    };
}