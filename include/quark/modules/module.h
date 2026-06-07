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
    std::string name;                           // e.g. "std::io"
    fs::path path;                              // primary file
    std::vector<fs::path> file_paths;            // all files in this module

    std::vector<ast::Stmt*> ast;                // merged AST
    std::vector<ast::Attribute> attributes;
    std::vector<std::string> imports;
    std::vector<std::string> namespace_path;
    std::vector<Module*> dependencies;           // resolved dependencies
    symb_t::Namespace* ns = nullptr;

    bool analyzed = false;
};

class ModuleManager {
public:
    explicit ModuleManager(::quark::CompilerContext& ctx);

    Module* load_entry(const fs::path& path);
    Module* load_module(const fs::path& path);

    void build_graph(Module* entry);

    const std::vector<Module*>& ordered_modules() const;

private:
    std::string extract_module_name(const std::vector<ast::Stmt*>& ast,
                                    std::vector<ast::Attribute>& out_attrs) const;
    void topo_sort();

    CompilerContext& ctx;
    // Key: module name (e.g. "std::io")
    std::unordered_map<std::string, Module*> modules;
    // Key: canonical file path — for dedup
    std::unordered_map<std::string, Module*> loaded_files;
    std::vector<Module*> ordered;
};

} // namespace quark::modules