#include "quark/support/symbol_path.h"
#include "utils/logger.h"

#include <variant>

namespace quark::support {

namespace {

template<class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

} // namespace

std::vector<std::string> flatten_path(const ast::Expr* expr) {
    if (!expr) return {};

    return std::visit(overloaded{
        [&](const ast::VarExpr& v) -> std::vector<std::string> {
            return { v.name };
        },

        [&](const ast::NamespaceExpr& n) -> std::vector<std::string> {
            auto left = flatten_path(n.left);
            auto right = flatten_path(n.right);

            left.insert(left.end(), right.begin(), right.end());
            return left;
        },

        [&](const auto&) -> std::vector<std::string> {
            utils::logger::crash("Expression is not a valid path");
            return {};
        }
    }, expr->kind);
}

std::string join_namespace(const std::vector<std::string>& path) {
    std::string out;

    for (size_t i = 0; i < path.size(); ++i) {
        if (i) out += "::";
        out += path[i];
    }

    return out;
}

std::vector<std::string> split_path(const std::string& qualified_name) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t sep = qualified_name.find("::", start);
        if (sep == std::string::npos) {
            parts.push_back(qualified_name.substr(start));
            break;
        }
        parts.push_back(qualified_name.substr(start, sep - start));
        start = sep + 2;
    }
    return parts;
}

// module + nested namespaces + name
std::string qualify_name(
    const std::vector<std::string>& module_path,
    const std::vector<std::string>& namespace_stack,
    const std::string& name
) {
    std::vector<std::string> path;

    path.insert(path.end(), module_path.begin(), module_path.end());
    path.insert(path.end(), namespace_stack.begin(), namespace_stack.end());
    path.push_back(name);

    return join_namespace(path);
}

} // namespace quark::support