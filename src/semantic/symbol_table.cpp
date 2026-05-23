#include "quark/semantic/symbol_table.h"
#include "utils/logger.h"
#include "quark/support/compiler_context.h"


#include <type_traits>
#include <utility>

using namespace utils::logger;

namespace quark::symb_t {
    SymbolTable::SymbolTable(memory::Arena& a) : arena(a){
        global_namespace = memory::make<Namespace>(arena);

        global_namespace->name = "::";

        current_namespace = global_namespace;
    }

    void SymbolTable::enter_scope() {
        scopes.emplace_back();
    }

    void SymbolTable::exit_scope() {
        if (scopes.empty()) {
            error("Trying to exit empty scope stack");
            return;
        }

        scopes.pop_back();
    }

    bool SymbolTable::enter_namespace(const std::string& name) {
        auto it = current_namespace->children.find(name);

        if (it == current_namespace->children.end()) {
            auto ns = memory::make<Namespace>(arena);
            ns->name = name;
            ns->parent = current_namespace;

            current_namespace->children.emplace(name, std::move(ns));
        }

        current_namespace = current_namespace->children[name];
        return true;
    }

    void SymbolTable::exit_namespace() {
        if (current_namespace == nullptr || current_namespace->parent == nullptr) {
            error("Cannot exit global namespace");
            return;
        }

        current_namespace = current_namespace->parent;
    }

    Namespace* SymbolTable::resolve_namespace(const std::vector<std::string>& path) {
        Namespace* current = global_namespace;

        for (const auto& part : path) {
            auto it = current->children.find(part);
            if (it == current->children.end()) {
                return nullptr;
            }

            current = it->second;
        }

        return current;
    }

    bool SymbolTable::declare_symbol(const std::string& name, Symbol symbol) {
        if (!scopes.empty()) {
            auto& current = scopes.back();

            if (current.contains(name)) {
                return false;
            }
            current.emplace(name, std::move(symbol));
            return true;
        }

        auto& current = current_namespace->symbols;

        if (current.contains(name)) {
            return false;
        }

        auto* sym = memory::make<Symbol>(arena, std::move(symbol));

        current.emplace(name, sym);
        return true;
    }

    bool SymbolTable::declare(const ast::VarDecl& decl) {
        return declare_symbol(decl.name, Symbol{
            decl.name,
            VarSymbol{
                decl.type,
                decl.is_mut,
                decl.value != nullptr
            },
            decl.attributes
        });
    }

    bool SymbolTable::declare(const ast::FuncArg& arg) {
        return declare_symbol(arg.name, Symbol{
            arg.name,
            FuncArgSymbol{
                arg.type,
                arg.is_mut
            },
            {}
        });
    }

    bool SymbolTable::declare(const ast::StructDecl& str) {
        StructSymbol sym;

        sym.field_names.reserve(str.fields.size());
        sym.field_types.reserve(str.fields.size());

        for (const auto& field : str.fields) {
            sym.field_names.push_back(field.name);
            sym.field_types.push_back(field.type);
        }

        return declare_symbol(str.name, Symbol{
            str.name,
            sym,
            str.attributes
        });
    }

    Symbol* SymbolTable::lookup(const std::string& name) {
        for (int i = static_cast<int>(scopes.size()) - 1; i >= 0; --i) {
            auto it = scopes[i].find(name);
            if (it != scopes[i].end()) {
                return &it->second;
            }
        }

        Namespace* ns = current_namespace;

        while (ns != nullptr) {
            auto it = ns->symbols.find(name);
            if (it != ns->symbols.end()) {
                return it->second;
            }

            ns = ns->parent;
        }

        return nullptr;
    }

    Symbol* SymbolTable::lookup_qualified(const std::vector<std::string>& path) {
        if (path.empty()) {
            return nullptr;
        }

        Namespace* current = global_namespace;

        for (size_t i = 0; i + 1 < path.size(); ++i) {
            auto it = current->children.find(path[i]);
            if (it == current->children.end()) {
                return nullptr;
            }

            current = it->second;
        }

        auto it = current->symbols.find(path.back());
        if (it == current->symbols.end()) {
            return nullptr;
        }

        return it->second;
    }

    void SymbolTable::mark_initialized(const std::string& name) {
        auto* sym = lookup(name);

        if (!sym) {
            error("There is no declared variable: " + name);
            return;
        }

        std::visit([&](auto& s) {
            using T = std::decay_t<decltype(s)>;

            if constexpr (std::is_same_v<T, VarSymbol>) {
                s.is_initialized = true;
            } else {
                error("Symbol is not a variable: " + name);
            }
        }, sym->data);
    }

}