#pragma once

#include <filesystem>
#include <optional>

#include "quark/support/type_context.h"
#include "quark/semantic/symbol_table.h"
#include "quark-alloc/memory/alloc.h"
#include "quark/modules/module.h"
#include "utils/errors.h"

namespace quark {

    struct CompilerContext {
        memory::Arena module_arena;
        memory::Arena ast_arena;
        memory::Arena symbol_arena;

        types::TypeContext types;
        symb_t::SymbolTable symbols;

        SourceLocation srcloc;

        std::filesystem::path root_path;

        // Concrete instantiations of generic functions (monomorphization)
        std::vector<ast::FuncStmt> generic_instantiations;

        ErrorBag errors;

        CompilerContext()
            : symbols(symbol_arena)
        {}
    };

}
