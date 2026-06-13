#pragma once

#include <unordered_map>
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
        std::optional<int64_t> const_value;
    };

    struct FuncArgSymbol {
        const ast::Type* type;
        bool is_mut;
    };

    struct StructSymbol {
        std::vector<std::string> field_names;
        std::vector<const ast::Type*> field_types;
        std::vector<std::vector<ast::Attribute>> field_attributes;
    };
    struct FuncSymbol {
        std::vector<const ast::Type*> arg_types;
        const ast::Type* return_type;

        bool is_extern;
        bool is_defined;
        bool is_entry;
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
        std::vector<std::string> owning_module;
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
        bool declare(const ast::FuncStmt& fn);
        bool declare(const ast::StructDecl& str);
        bool declare_struct(const std::string& name,
            const std::vector<std::pair<std::string, const ast::Type*>>& fields,
            const std::vector<ast::Attribute>& attrs = {},
            const std::vector<std::vector<ast::Attribute>>& field_attrs = {});
        bool declare_struct_global(const std::string& name,
            const std::vector<std::pair<std::string, const ast::Type*>>& fields,
            const std::vector<ast::Attribute>& attrs = {},
            const std::vector<std::vector<ast::Attribute>>& field_attrs = {});
        bool declare(const ast::RegionStmt& reg);

        bool declare_symbol(const std::string& name, Symbol symbol);

        Symbol* lookup(const std::string& name);
        Symbol* lookup_qualified(const std::vector<std::string>& path);
        Symbol* lookup_current_namespace(const std::string& name);

        void mark_initialized(const std::string& name);
        Namespace* create_namespace_path(const std::vector<std::string>& path);

        void set_current_module_ns(const std::vector<std::string>& ns);
        const std::vector<std::string>& get_current_module_ns() const;

    private:
        memory::Arena& arena;
        Namespace* global_namespace;
        Namespace* current_namespace = nullptr;

        std::vector<std::unordered_map<std::string, Symbol>> scopes;
        std::vector<std::string> current_module_ns;
    };

}
