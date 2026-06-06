#include "quark/support/type_context.h"

namespace quark::types {

namespace {
    std::string mangle_name(const std::string& base, const std::vector<const Type*>& args) {
        std::string out = base;
        for (const auto* t : args) {
            out += "$";
            if (t->kind == TypeKind::Struct) {
                out += t->struct_name;
            } else if (t->kind == TypeKind::Generic) {
                out += t->struct_name;
            } else {
                out += std::to_string((int)t->kind);
            }
        }
        return out;
    }
}

    TypeContext::TypeContext() {
        builtin_types[(size_t)TypeKind::Void].kind = TypeKind::Void;
        builtin_types[(size_t)TypeKind::Bool].kind = TypeKind::Bool;

        builtin_types[(size_t)TypeKind::I8].kind  = TypeKind::I8;
        builtin_types[(size_t)TypeKind::I16].kind = TypeKind::I16;
        builtin_types[(size_t)TypeKind::I32].kind = TypeKind::I32;
        builtin_types[(size_t)TypeKind::I64].kind = TypeKind::I64;

        builtin_types[(size_t)TypeKind::U8].kind  = TypeKind::U8;
        builtin_types[(size_t)TypeKind::U16].kind = TypeKind::U16;
        builtin_types[(size_t)TypeKind::U32].kind = TypeKind::U32;
        builtin_types[(size_t)TypeKind::U64].kind = TypeKind::U64;

        builtin_types[(size_t)TypeKind::F32].kind = TypeKind::F32;
        builtin_types[(size_t)TypeKind::F64].kind = TypeKind::F64;

        builtin_types[(size_t)TypeKind::String].kind = TypeKind::String;
    }

    const Type* TypeContext::get_builtin(TypeKind kind) {
        return &builtin_types[(int)kind];
    }

    const Type* TypeContext::get_struct(const std::string& name) {
        auto it = struct_types.find(name);
        if (it != struct_types.end())
            return &it->second;

        Type t;
        t.kind = TypeKind::Struct;
        t.struct_name = name;

        auto [iter, _] = struct_types.emplace(name, std::move(t));
        return &iter->second;
    }

    const Type* TypeContext::get_generic_param(const std::string& name) {
        auto it = generic_param_types.find(name);
        if (it != generic_param_types.end())
            return &it->second;

        Type t;
        t.kind = TypeKind::Generic;
        t.struct_name = name;

        auto [iter, _] = generic_param_types.emplace(name, std::move(t));
        return &iter->second;
    }

    const Type* TypeContext::get_generic_instantiation(
        const std::string& name,
        const std::vector<const Type*>& args
    ) {
        auto def_it = generic_defs.find(name);
        if (def_it == generic_defs.end()) {
            return get_struct(name); // fallback — not a generic
        }

        std::string mangled = mangle_name(name, args);

        // Already registered with concrete fields? Skip.
        if (structs.find(mangled) != structs.end()) {
            auto existing = struct_types.find(mangled);
            if (existing != struct_types.end())
                return &existing->second;
        }

        // Build a substitution map: param_name → concrete type
        std::unordered_map<std::string, const Type*> subst;
        for (size_t i = 0; i < def_it->second.params.size() && i < args.size(); ++i) {
            subst[def_it->second.params[i]] = args[i];
        }

        // Create concrete fields by substituting type params
        std::vector<std::pair<std::string, const Type*>> concrete_fields;
        for (const auto& f : def_it->second.fields) {
            const Type* field_type = f.type;
            if (field_type && field_type->kind == TypeKind::Generic) {
                auto sub_it = subst.find(field_type->struct_name);
                if (sub_it != subst.end()) {
                    field_type = sub_it->second;
                }
            }
            concrete_fields.emplace_back(f.name, field_type);
        }

        register_struct(mangled, concrete_fields);
        mangled_to_base[mangled] = name;

        return get_struct(mangled);
    }

    const Type* TypeContext::get_deferred_generic(const std::string& name, const std::vector<const Type*>& args) {
        std::string mangled = mangle_name(name, args);
        auto existing = struct_types.find(mangled);
        if (existing != struct_types.end())
            return &existing->second;

        Type t;
        t.kind = TypeKind::Struct;
        t.struct_name = mangled;
        t.type_args = args;

        auto [iter, _] = struct_types.emplace(mangled, std::move(t));
        mangled_to_base[mangled] = name;
        return &iter->second;
    }

    bool TypeContext::try_instantiate(const std::string& mangled, const std::vector<const Type*>& type_args) {
        // Check if this mangled name was already instantiated
        if (structs.find(mangled) != structs.end())
            return true;

        // Iterate all generic defs to find the matching base name
        for (const auto& [base_name, def] : generic_defs) {
            std::string candidate = mangle_name(base_name, type_args);
            if (candidate == mangled) {
                // Found the matching generic — instantiate
                get_generic_instantiation(base_name, type_args);
                return true;
            }
        }
        return false;
    }

    bool TypeContext::is_mangled_name(const std::string& mangled, std::string& out_base) const {
        auto it = mangled_to_base.find(mangled);
        if (it != mangled_to_base.end()) {
            out_base = it->second;
            return true;
        }
        return false;
    }

    void TypeContext::register_struct(
        const std::string& name,
        const std::vector<std::pair<std::string, const Type*>>& fields
    ) {
        structs[name] = fields;
    }

    void TypeContext::register_generic_struct(const std::string& name, const GenericStructDef& def) {
        generic_defs[name] = def;
    }

    const std::vector<std::pair<std::string, const Type*>>* TypeContext::get_struct_fields(const std::string& name) const{
        auto it = structs.find(name);
        if (it == structs.end())
            return nullptr;
        return &it->second;
    }

    int TypeContext::get_field_index(const std::string& struct_name, const std::string& field) const {
        auto fields = get_struct_fields(struct_name);
        if (!fields) return -1;

        for (int i = 0; i < (int)fields->size(); i++) {
            if ((*fields)[i].first == field)
                return i;
        }
        return -1;
    }

    const Type* TypeContext::get_field_type(const std::string& struct_name, const std::string& field) const {
        auto fields = get_struct_fields(struct_name);
        if (!fields) return nullptr;

        for (auto& [name, type] : *fields) {
            if (name == field)
                return type;
        }
        return nullptr;
    }

    const Type* TypeContext::get_pointer(const Type* base) {
        auto it = pointer_cache.find(base);
        if (it != pointer_cache.end())
            return &it->second;

        Type t;
        t.kind = TypeKind::Pointer;
        t.pointed = base;

        auto [iter, _] = pointer_cache.emplace(base, std::move(t));
        return &iter->second;
    }

}
