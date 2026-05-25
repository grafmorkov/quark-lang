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

std::string canonical_key(const fs::path& p) {
    return fs::weakly_canonical(p).string();
}

std::vector<std::string> module_namespace_from_path(const fs::path& path) {
    fs::path canon = fs::weakly_canonical(path);

    fs::path rel =
        fs::relative(
            canon,
            fs::path(QUARK_ROOT)
        );

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
        if (!stmt) {
            continue;
        }

        if (auto* load = std::get_if<ast::LoadStmt>(&stmt->kind)) {
            ret.push_back(load->module);
        }
    }

    return ret;
}

fs::path resolve_import_from(const fs::path& base_dir, const std::string& name) {
    fs::path requested(name);

    if (requested.is_absolute() && fs::exists(requested)) {
        return fs::weakly_canonical(requested);
    }

    const std::vector<fs::path> bases = {
        base_dir,
        fs::current_path(),
        fs::path(QUARK_ROOT)
    };

    for (const auto& base : bases) {
        fs::path full = base / requested;

        if (fs::exists(full)) {
            return fs::weakly_canonical(full);
        }
    }

    utils::logger::crash("module not found: " + name);
    return {};
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
    std::string key = canon.string();

    if (auto it = modules.find(key); it != modules.end()) {
        return it->second;
    }

    Module* mod = memory::make_default<Module>(ctx.module_arena);

    mod->path = canon;

    mod->namespace_path = module_namespace_from_path(canon);
    mod->name = support::join_namespace(mod->namespace_path);
    mod->ns = ctx.symbols.create_namespace_path(mod->namespace_path);

    modules.emplace(key, mod);

    return mod;
}

void ModuleManager::topo_sort() {
    ordered.clear();

    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> stack;

    std::function<void(Module*)> dfs = [&](Module* mod) {
        if (!mod) {
            return;
        }

        std::string key = canonical_key(mod->path);

        if (visited.find(key) != visited.end()) {
            return;
        }

        if (stack.find(key) != stack.end()) {
            utils::logger::crash(
                "cyclic dependency detected at module: " +
                mod->path.string()
            );
        }

        stack.insert(key);

        for (const auto& imp : mod->imports) {
            fs::path imp_path =
                resolve_import_from(
                    mod->path.parent_path(),
                    imp
                );

            std::string imp_key =
                canonical_key(imp_path);

            auto it = modules.find(imp_key);

            if (it == modules.end() || !it->second) {
                utils::logger::crash(
                    "Unknown imported module: " + imp
                );
            }

            dfs(it->second);
        }

        stack.erase(key);
        visited.insert(key);

        ordered.push_back(mod);
    };

    std::vector<Module*> roots;

    roots.reserve(modules.size());

    for (auto& [_, mod] : modules) {
        if (mod) {
            roots.push_back(mod);
        }
    }

    std::sort(
        roots.begin(),
        roots.end(),
        [](Module* a, Module* b) {
            return a->path.string() < b->path.string();
        }
    );

    for (Module* mod : roots) {
        dfs(mod);
    }
}

void ModuleManager::build_graph(Module* entry) {
    if (!entry) {
        utils::logger::crash("entry module is null");
    }

    std::unordered_set<std::string> visited;

    std::function<void(Module*)> dfs = [&](Module* mod) {
        if (!mod) {
            return;
        }

        std::string key =
            canonical_key(mod->path);

        if (visited.find(key) != visited.end()) {
            return;
        }

        visited.insert(key);

        if (mod->source.empty()) {
            mod->source =
                utils::io::read_file(mod->path);
        }

        if (!mod->parsed) {
            lx::Lexer lex(mod->source.c_str(), ctx);
            ps::Parser parser(lex, ctx);

            mod->ast = parser.parse();
            mod->imports = collect_imports(mod->ast);

            mod->parsed = true;
        }

        for (const auto& imp : mod->imports) {
            fs::path imp_path =
                resolve_import_from(
                    mod->path.parent_path(),
                    imp
                );

            Module* imp_mod =
                load_module(imp_path);

            dfs(imp_mod);
        }
    };

    dfs(entry);

    topo_sort();
}

const std::vector<Module*>& ModuleManager::ordered_modules() const {
    return ordered;
}

const std::vector<ast::Stmt*>& ModuleManager::get_ast(const fs::path& path) const {
    static const std::vector<ast::Stmt*> empty;

    std::string key =
        fs::weakly_canonical(path).string();

    auto it = modules.find(key);

    if (it == modules.end() || !it->second) {
        return empty;
    }

    return it->second->ast;
}

} // namespace quark::modules