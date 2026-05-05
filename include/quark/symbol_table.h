#pragma once

#include <unordered_map>

#include "ast.h"

namespace quark::symb_t{
    struct VarSymbol {
        const ast::Type* type;
        bool is_mut;
        bool is_initialized;
    };

    struct FuncArgSymbol {
        const ast::Type* type;
        bool is_mut;
    };

    struct StructSymbol {
        std::vector<std::string> field_names;
        std::vector<const ast::Type*> field_types;
    };
    using SymbolData = std::variant<
        VarSymbol,
        FuncArgSymbol,
        StructSymbol
    >;

    struct Symbol {
        std::string name;
        SymbolData data;
        std::vector<ast::Attribute> attributes;
    };
    class SymbolTable {
        public:
            void enter_scope();
            void exit_scope();

            bool declare(const ast::VarDecl& decl);          
            bool declare(const ast::FuncArg& fnArg);
            bool declare(const ast::StructDecl& str);
            bool declare_symbol(const std::string& name, Symbol symbol);
            void mark_initialized(const std::string& name);
            Symbol* lookup(const std::string& name);

        private:
            std::vector<std::unordered_map<std::string, Symbol>> scopes;
    };
}