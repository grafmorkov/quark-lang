#pragma once

#include <filesystem>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

#include "quant/support/type_context.h"
#include "quant/support/alloc.h"
#include "quant/semantic/symbol_table.h"
#include "quant/modules/module.h"
#include "quant/backend/mc.h"
#include "utils/errors.h"

namespace quant {

    struct CompilerContext {
        memory::Arena module_arena;
        memory::Arena ast_arena;
        memory::Arena symbol_arena;

        types::TypeContext types;
        symb_t::SymbolTable symbols;

        SourceLocation srcloc;
        bool emit_start;

        std::filesystem::path root_path;

        // Target OS/ABI (set from --target). Selects the stdlib override
        // tree (std/zp/ for ZeroPoint) and the syscall lowering layer.
        codegen::mc::TargetOS target_os = codegen::mc::TargetOS::Linux;

        // When true, std modules are skipped during IR gen / codegen and
        // the pre-compiled static stdlib (.a) is linked instead.
        bool use_static_std = false;
        std::filesystem::path static_std_path;

        // Concrete instantiations of generic functions (monomorphization)
        struct GenericInstantiation {
            ast::FuncStmt stmt;
            std::vector<std::string> module_namespace;
            // Substitution map of the generic function's type params, needed
            // by IR gen to resolve nested generic calls whose explicit type
            // args are still the generic params (e.g. opt_none<T> inside a
            // body being instantiated as slice_at<i32>).
            std::unordered_map<std::string, const ast::Type*> type_subst;
        };
        std::vector<GenericInstantiation> generic_instantiations;

        // Mangled concrete generic function name -> its concrete return type.
        // Needed by IR gen to detect struct returns (sret) for generic calls,
        // since the ephemeral concrete symbols are scoped per call site.
        std::unordered_map<std::string, const ast::Type*> generic_return_types;

        // Mangled concrete generic function name -> its concrete argument types.
        // Needed by IR gen to emit implicit widening casts on call arguments.
        std::unordered_map<std::string, std::vector<const ast::Type*>> generic_arg_types;

        // Concrete generic struct names whose methods were already monomorphized
        // (e.g. "Box$4"), so lazy method instantiation is idempotent.
        std::unordered_set<std::string> instantiated_generic_methods;

        ErrorBag errors;

        CompilerContext()
            : symbols(symbol_arena)
        {
            symbols.set_error_bag(&errors);
        }
    };

}
