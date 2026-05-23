#pragma once
#include "quark/modules/module.h"

namespace quark::linker{
    class Linker {
        private:
            modules::ModuleManager& modules;

            const ast::FuncStmt* find_main();
        public:
            void validate();

            Linker(modules::ModuleManager& m);
    };
}