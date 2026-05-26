#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <array>
#include "quark/frontend/ast.h"

using namespace quark::ast;

namespace quark::types {

class TypeContext {
public:
    const Type* get_builtin(TypeKind kind);
    const Type* get_struct(const std::string& name);
    const Type* get_pointer(const Type* base);

    void register_struct(const std::string& name, 
        const std::vector<std::pair<std::string, const Type*>>& fields);

    const std::vector<std::pair<std::string, const Type*>>* get_struct_fields(const std::string& name) const;

    int get_field_index(const std::string& struct_name, const std::string& field) const;

    const Type* get_field_type(const std::string& struct_name, const std::string& field) const;

    TypeContext();

private:
    std::array<Type, (size_t)TypeKind::Count> builtin_types;
    std::unordered_map<std::string, Type> struct_types;
    std::unordered_map<const Type*, Type> pointer_cache;

    std::unordered_map<
        std::string,
        std::vector<std::pair<std::string, const Type*>>
    > structs;
};

}