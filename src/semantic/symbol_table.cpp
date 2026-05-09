#include "quark/symbol_table.h"
#include "utils/logger.h"

using namespace utils::logger;

namespace quark::symb_t {

    void SymbolTable::enter_scope() {
        scopes.emplace_back();
    }

    void SymbolTable::exit_scope() {
        if (scopes.empty()) {
            return;
        }
        scopes.pop_back();
    }
    bool SymbolTable::declare_symbol(const std::string& name, Symbol symbol) {
        if (scopes.empty()) {
            error("No active scope");
            return false;
        }

        auto& current = scopes.back();

        if (current.find(name) != current.end()){
            return false;
        }

        current.emplace(name, std::move(symbol));
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
        symb_t::StructSymbol sym;

        sym.field_names.reserve(str.fields.size());
        sym.field_types.reserve(str.fields.size());

        for (const auto& field_ptr : str.fields) {
            const auto& field = *field_ptr;

            sym.field_names.push_back(field.name);
            sym.field_types.push_back(field.type);
        }

        return declare_symbol(
            str.name,
            Symbol{
                str.name,
                sym,
                str.attributes
            }
        );
    }

    Symbol* SymbolTable::lookup(const std::string& name) {
        for (int i = static_cast<int>(scopes.size()) - 1; i >= 0; --i) {
            auto it = scopes[i].find(name);
            if (it != scopes[i].end()) {
                return &it->second;
            }
        }
        return nullptr;
    }
    void SymbolTable::mark_initialized(const std::string& name) {
        auto sym = lookup(name);

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