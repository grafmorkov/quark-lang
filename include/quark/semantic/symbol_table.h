#pragma once

#include <unordered_map>
#include <memory>
#include <variant>
#include <vector>
#include <string>

#include "quark/frontend/ast.h"
#include "quark-alloc/memory/alloc.h"

namespace quark::symb_t {

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
    struct FuncSymbol {
        std::vector<const ast::Type*> arg_types;
        const ast::Type* return_type;

        bool is_extern;
        bool is_exported;
    };

    using SymbolData = std::variant<
        VarSymbol,
        FuncArgSymbol,
        FuncSymbol,
        StructSymbol
    >;

    struct Symbol {
        std::string name;
        SymbolData data;
        std::vector<ast::Attribute> attributes;
    };

    struct Namespace {
        std::string name;
        Namespace* parent = nullptr;

        std::unordered_map<std::string, Symbol*> symbols;
        std::unordered_map<std::string, Namespace*> children;
    };
    class SymbolTable {
    public:
        SymbolTable(memory::Arena& a);

        void enter_scope();
        void exit_scope();

        bool enter_namespace(const std::string& name);
        void exit_namespace();

        Namespace* resolve_namespace(const std::vector<std::string>& path);

        bool declare(const ast::VarDecl& decl);
        bool declare(const ast::FuncArg& arg);
        bool declare(const ast::StructDecl& str);

        bool declare_symbol(const std::string& name, Symbol symbol);

        Symbol* lookup(const std::string& name);
        Symbol* lookup_qualified(const std::vector<std::string>& path);

        void mark_initialized(const std::string& name);

    private:
        memory::Arena& arena;
        Namespace* global_namespace;
        Namespace* current_namespace = nullptr;

        std::vector<std::unordered_map<std::string, Symbol>> scopes;
    };

}