#pragma once

#include <string>
#include <vector>

#include "quant/frontend/ast.h"

namespace quant::support {

std::vector<std::string> flatten_path(const ast::Expr* expr);

std::string join_namespace(const std::vector<std::string>& path);

std::vector<std::string> split_path(const std::string& qualified_name);

std::string qualify_name(
    const std::vector<std::string>& module_path,
    const std::vector<std::string>& namespace_stack,
    const std::string& name
);
} // namespace quant::support
