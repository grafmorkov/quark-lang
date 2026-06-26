#pragma once
#include "quark/modules/module.h"

struct CompilerContext;

namespace quark::linker{
    class Linker {
        private:
            modules::ModuleManager& modules;
            CompilerContext& ctx;

            const ast::FuncStmt* find_entry();
        public:
            void validate();

            Linker(modules::ModuleManager& m, CompilerContext& c);
    };
}