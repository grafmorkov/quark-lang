#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "quark/frontend/ast.h"
#include "quark/semantic/symbol_table.h"
#include "quark-alloc/memory/alloc.h"

namespace quark {
struct CompilerContext;
}

namespace quark::modules {

namespace fs = std::filesystem;

struct Module {
    std::string name;
    fs::path path;

    std::string source;

    std::vector<ast::Stmt*> ast;
    std::vector<std::string> imports;
    std::vector<std::string> namespace_path;
    symb_t::Namespace* ns = nullptr;

    bool parsed = false;
    bool analyzed = false;
};

class ModuleManager {
public:
    explicit ModuleManager(::quark::CompilerContext& ctx);

    Module* load_entry(const fs::path& path);
    Module* load_module(const fs::path& path);

    void build_graph(Module* entry);

    const std::vector<Module*>& ordered_modules() const;
    const std::vector<ast::Stmt*>& get_ast(const fs::path& path) const;

private:
    void topo_sort();

    CompilerContext& ctx;
    std::unordered_map<std::string, Module*> modules;
    std::vector<Module*> ordered;
};

} // namespace quark::modules