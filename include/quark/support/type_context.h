#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include "quark/frontend/ast.h"

using namespace quark::ast;

namespace quark::types {

class TypeContext {
public:
    const Type* get_int() const    { return &int_type; }
    const Type* get_string() const { return &string_type; }
    const Type* get_void() const   { return &void_type; }

    const Type* get_struct(const std::string& name) {
        auto it = struct_types.find(name);
        if (it != struct_types.end())
            return &it->second;

        Type t;
        t.kind = TypeKind::Struct;
        t.struct_name = name;

        auto [iter, _] = struct_types.emplace(name, std::move(t));
        return &iter->second;
    }

    void register_struct(
        const std::string& name,
        const std::vector<std::pair<std::string, const Type*>>& fields
    ) {
        structs[name] = fields;
    }

    const std::vector<std::pair<std::string, const Type*>>*
    get_struct_fields(const std::string& name) const {
        auto it = structs.find(name);
        if (it == structs.end())
            return nullptr;
        return &it->second;
    }

    int get_field_index(const std::string& struct_name,
                        const std::string& field) const {
        auto fields = get_struct_fields(struct_name);
        if (!fields) return -1;

        for (int i = 0; i < (int)fields->size(); i++) {
            if ((*fields)[i].first == field)
                return i;
        }
        return -1;
    }

    const Type* get_field_type(const std::string& struct_name,
                               const std::string& field) const {
        auto fields = get_struct_fields(struct_name);
        if (!fields) return nullptr;

        for (auto& [name, type] : *fields) {
            if (name == field)
                return type;
        }
        return nullptr;
    }

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