#pragma once
#include "quanta/modules/module.h"

struct CompilerContext;

namespace quanta::linker{
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