#pragma once

#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vector>
#include <string>

#include "quanta/support/type_context.h"
#include "quanta/support/alloc.h"
#include "quanta/semantic/symbol_table.h"
#include "quanta/modules/module.h"
#include "utils/errors.h"

namespace quanta {

    struct CompilerContext {
        memory::Arena module_arena;
        memory::Arena ast_arena;
        memory::Arena symbol_arena;

        types::TypeContext types;
        symb_t::SymbolTable symbols;

        SourceLocation srcloc;

        std::filesystem::path root_path;

        // Concrete instantiations of generic functions (monomorphization)
        struct GenericInstantiation {
            ast::FuncStmt stmt;
            std::vector<std::string> module_namespace;
        };
        std::vector<GenericInstantiation> generic_instantiations;

        // Mangled concrete generic function name -> its concrete return type.
        // Needed by IR gen to detect struct returns (sret) for generic calls,
        // since the ephemeral concrete symbols are scoped per call site.
        std::unordered_map<std::string, const ast::Type*> generic_return_types;

        ErrorBag errors;

        CompilerContext()
            : symbols(symbol_arena)
        {}
    };

}
