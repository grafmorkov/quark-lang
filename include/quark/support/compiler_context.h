#pragma once

#include "quark/support/type_context.h"
#include "quark/semantic/symbol_table.h"
#include "third_party/quark-alloc/memory/alloc.h"

namespace quark{
    class CompilerContext{
        public:
            types::TypeContext types;
            symb_t::SymbolTable symbols;
            SourceLocation srcloc;
            memory::Arena arena;
    };
}