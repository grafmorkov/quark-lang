#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include "quark/frontend/ast.h"

using namespace quark::ast;

namespace quark::types {

class TypeContext {
public:
    const Type* get_int() const;
    const Type* get_string() const;
    const Type* get_void() const;

    const Type* get_struct(const std::string& name);

    void register_struct(const std::string& name, 
        const std::vector<std::pair<std::string, const Type*>>& fields);

    const std::vector<std::pair<std::string, const Type*>>* get_struct_fields(const std::string& name) const;

    int get_field_index(const std::string& struct_name, const std::string& field) const;

    const Type* get_field_type(const std::string& struct_name, const std::string& field) const;

private:
    Type int_type{ TypeKind::Int };
    Type string_type{ TypeKind::String };
    Type void_type{ TypeKind::Void };

    std::unordered_map<std::string, Type> struct_types;

    std::unordered_map<
        std::string,
        std::vector<std::pair<std::string, const Type*>>
    > structs;
};

}