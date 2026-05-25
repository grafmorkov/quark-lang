#include "quark/linker/linker.h"

namespace quark::linker{
    const ast::FuncStmt* Linker::find_main() {
        for (auto* mod : modules.ordered_modules()) {
            for (auto* stmt : mod->ast) {
                if (auto* func = std::get_if<ast::FuncStmt>(&stmt->kind)){
                    if (func->name == "main") { // TODO 
                        return func;
                    }
                }
            }
        }
        return nullptr;
    }
    void Linker::validate() {
        if (!find_main()) {
            utils::logger::fatal("entry point 'main' not found");
        }
    }
    Linker::Linker(modules::ModuleManager& m): modules(m){}
}