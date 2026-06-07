#include "quark/modules/module.h"
#include "quark/support/compiler_context.h"
#include "quark/frontend/lexer.h"
#include "quark/frontend/parser.h"
#include "quark/support/symbol_path.h"
#include "utils/file_manager.h"
#include "utils/logger.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <unordered_set>
#include <vector>

namespace quark::modules {

namespace fs = std::filesystem;

namespace {

std::vector<std::string> module_namespace_from_path(const fs::path& path, const fs::path& root) {
    fs::path canon = fs::weakly_canonical(path);
    fs::path rel = fs::relative(canon, root);
    rel.replace_extension();

    std::vector<std::string> out;
    for (const auto& part : rel) {
        out.push_back(part.string());
    }
    return out;
}

std::vector<std::string> collect_imports(const std::vector<ast::Stmt*>& ast) {
    std::vector<std::string> ret;
    for (auto* stmt : ast) {
        if (!stmt) continue;
        if (auto* load = std::get_if<ast::LoadStmt>(&stmt->kind)) {
            ret.push_back(load->module);
        }
    }
    return ret;
}

// Convert module name like "std::io" to a relative path "std/io.qk"
fs::path module_name_to_path(const std::string& name) {
    fs::path p;
    size_t start = 0;
    while (true) {
        size_t sep = name.find("::", start);
        if (sep == std::string::npos) {
            p /= name.substr(start);
            break;
        }
        p /= name.substr(start, sep - start);
        start = sep + 2;
    }
    p += ".qk";
    return p;
}

} // namespace

ModuleManager::ModuleManager(CompilerContext& ctx_)
    : ctx(ctx_) {}

Module* ModuleManager::load_entry(const fs::path& path) {
    fs::path abs = fs::absolute(path);
    if (!fs::exists(abs)) {
        utils::logger::crash("entry file not found: " + abs.string());
    }
    return load_module(abs);
}

Module* ModuleManager::load_module(const fs::path& path) {
    fs::path canon = fs::weakly_canonical(path);
    std::string file_key = canon.string();

    // Already loaded this file → return its module
    if (auto it = loaded_files.find(file_key); it != loaded_files.end()) {
        return it->second;
    }

    // Read and parse
    std::string source = utils::io::read_file(canon);
    lx::Lexer lex(source.c_str(), ctx);
    ps::Parser parser(lex, ctx);
    auto ast = parser.parse();
    auto imports = collect_imports(ast);

    // Extract module name from declaration (or fallback to path)
    std::string module_name;
    std::vector<std::string> namespace_path;
    std::vector<ast::Attribute> mod_attrs;

    auto* mod_decl = [&]() -> ast::ModuleDecl* {
        for (auto* stmt : ast) {
            if (!stmt) continue;
            if (auto* d = std::get_if<ast::ModuleDecl>(&stmt->kind)) {
                return d;
            }
        }
        return nullptr;
    }();

    if (mod_decl) {
        module_name = mod_decl->name;
        mod_attrs = std::move(mod_decl->attributes);

        // Split "std::io" → ["std", "io"]
        size_t start = 0;
        while (true) {
            size_t sep = module_name.find("::", start);
            if (sep == std::string::npos) {
                namespace_path.push_back(module_name.substr(start));
                break;
            }
            namespace_path.push_back(module_name.substr(start, sep - start));
            start = sep + 2;
        }
    } else {
        namespace_path = module_namespace_from_path(canon, ctx.root_path);
        module_name = support::join_namespace(namespace_path);
    }

    // Find or create module by name
    Module* mod = nullptr;
    auto mod_it = modules.find(module_name);

    if (mod_it != modules.end()) {
        mod = mod_it->second;
        // Merge AST
        mod->ast.insert(mod->ast.end(), ast.begin(), ast.end());
        mod->file_paths.push_back(canon);
        // Merge imports (dedup)
        for (const auto& imp : imports) {
            if (std::find(mod->imports.begin(), mod->imports.end(), imp) == mod->imports.end()) {
                mod->imports.push_back(imp);
            }
        }
    } else {
        mod = memory::make_default<Module>(ctx.module_arena);
        mod->name = module_name;
        mod->namespace_path = namespace_path;
        mod->path = canon;
        mod->file_paths = { canon };
        mod->ast = std::move(ast);
        mod->imports = std::move(imports);
        mod->attributes = std::move(mod_attrs);
        mod->ns = ctx.symbols.create_namespace_path(namespace_path);
        modules.emplace(module_name, mod);
    }

    loaded_files.emplace(file_key, mod);
    return mod;
}

void ModuleManager::topo_sort() {
    ordered.clear();

    std::unordered_set<Module*> visited;
    std::unordered_set<Module*> stack;

    std::function<void(Module*)> dfs = [&](Module* mod) {
        if (!mod) return;

        if (visited.find(mod) != visited.end()) return;

        if (stack.find(mod) != stack.end()) {
            utils::logger::crash("cyclic dependency detected at module: " + mod->name);
        }

        stack.insert(mod);

        for (Module* dep : mod->dependencies) {
            dfs(dep);
        }

        stack.erase(mod);
        visited.insert(mod);
        ordered.push_back(mod);
    };

    // Collect roots (sorted for determinism)
    std::vector<Module*> roots;
    roots.reserve(modules.size());
    for (auto& [_, mod] : modules) {
        if (mod) roots.push_back(mod);
    }
    std::sort(roots.begin(), roots.end(),
        [](Module* a, Module* b) { return a->name < b->name; });

    for (Module* mod : roots) {
        dfs(mod);
    }

}

void ModuleManager::build_graph(Module* entry) {
    if (!entry) {
        utils::logger::crash("entry module is null");
    }

    std::unordered_set<Module*> visited;

    std::function<void(Module*)> dfs = [&](Module* mod) {
        if (!mod) return;
        if (visited.find(mod) != visited.end()) return;
        visited.insert(mod);

        for (const auto& imp : mod->imports) {
            Module* dep = nullptr;

            // Try loading as module name (e.g. "std::io")
            fs::path mod_rel = module_name_to_path(imp);     // e.g. "std/io.qk"
            fs::path mod_dir_rel = mod_rel.parent_path() / mod_rel.stem(); // e.g. "std/io"

            const std::vector<fs::path> bases = {
                mod->path.parent_path(),
                fs::current_path(),
                ctx.root_path
            };

            for (const auto& base : bases) {
                fs::path primary = base / mod_rel;      // "base/std/io.qk"
                fs::path dir = base / mod_dir_rel;      // "base/std/io/"

                // 1. Primary file
                if (fs::exists(primary)) {
                    dep = load_module(primary);
                }

                // 2. Module directory - scan for additional .qk files
                if (fs::exists(dir) && fs::is_directory(dir)) {
                    for (const auto& dirent : fs::directory_iterator(dir)) {
                        if (dirent.path().extension() != ".qk") continue;
                        auto* m = load_module(dirent.path());
                        if (m->name == imp) dep = m;
                    }
                }

                if (dep) break;
            }

            if (!dep) {
                utils::logger::crash("Unknown imported module: " + imp);
            }

            mod->dependencies.push_back(dep);
            dfs(dep);
        }
    };

    dfs(entry);

    topo_sort();
}

const std::vector<Module*>& ModuleManager::ordered_modules() const {
    return ordered;
}

} // namespace quark::modules
