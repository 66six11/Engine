#include "asharia/reflection/type_registry.hpp"

#include <algorithm>
#include <array>
#include <expected>
#include <string>
#include <utility>

namespace asharia::reflection {
    namespace {

        [[nodiscard]] bool hasDuplicateFieldName(const TypeInfo& type, const FieldInfo& field) {
            return std::ranges::count_if(type.fields, [&field](const FieldInfo& other) {
                       return other.name == field.name;
                   }) > 1;
        }

        [[nodiscard]] bool hasDuplicateFieldId(const TypeInfo& type, const FieldInfo& field) {
            return std::ranges::count_if(type.fields, [&field](const FieldInfo& other) {
                       return other.id == field.id;
                   }) > 1;
        }

        [[nodiscard]] TypeInfo builtinType(std::string_view name, TypeKind kind) {
            return TypeInfo{
                .id = makeTypeId(name),
                .name = std::string{name},
                .version = 1,
                .kind = kind,
                .fields = {},
            };
        }

        [[nodiscard]] VoidResult validateTypeHeader(const TypeInfo& type) {
            if (type.name.empty()) {
                return std::unexpected{reflectionError("Cannot register reflected type with no name.")};
            }

            if (!type.id) {
                return std::unexpected{
                    reflectionError("Cannot register reflected type with invalid id: " + type.name)};
            }

            return {};
        }

        [[nodiscard]] VoidResult validateTypeIdentity(std::span<const TypeInfo> types,
                                                      const TypeInfo& type) {
            for (const TypeInfo& existing : types) {
                if (existing.name == type.name) {
                    return std::unexpected{
                        reflectionError("Duplicate reflected type name: " + type.name)};
                }
                if (existing.id == type.id) {
                    return std::unexpected{reflectionError("Reflected type id collision between '" +
                                                           existing.name + "' and '" + type.name +
                                                           "'.")};
                }
            }

            return {};
        }

        [[nodiscard]] VoidResult validateFieldIdentity(const TypeInfo& type,
                                                       const FieldInfo& field) {
            if (field.name.empty()) {
                return std::unexpected{reflectionError("Reflected type '" + type.name +
                                                       "' has a field with no name.")};
            }
            if (!field.id) {
                return std::unexpected{reflectionError("Reflected field '" + type.name + "." +
                                                       field.name + "' has an invalid id.")};
            }
            if (!field.type) {
                return std::unexpected{reflectionError("Reflected field '" + type.name + "." +
                                                       field.name + "' has an unknown type id.")};
            }
            if (hasDuplicateFieldName(type, field)) {
                return std::unexpected{reflectionError("Reflected type '" + type.name +
                                                       "' has duplicate field name '" +
                                                       field.name + "'.")};
            }
            if (hasDuplicateFieldId(type, field)) {
                return std::unexpected{reflectionError("Reflected type '" + type.name +
                                                       "' has duplicate field id for '" +
                                                       field.name + "'.")};
            }

            return {};
        }

        [[nodiscard]] VoidResult validateFieldFlags(const TypeInfo& type,
                                                    const FieldInfo& field) {
            if (field.flags.has(FieldFlag::Serializable) &&
                field.flags.has(FieldFlag::Transient)) {
                return std::unexpected{reflectionError("Reflected field '" + type.name + "." +
                                                       field.name +
                                                       "' cannot be Serializable and Transient.")};
            }

            if (field.flags.has(FieldFlag::EditorOnly) &&
                field.flags.has(FieldFlag::RuntimeVisible)) {
                return std::unexpected{reflectionError("Reflected field '" + type.name + "." +
                                                       field.name +
                                                       "' cannot be EditorOnly and RuntimeVisible.")};
            }

            return {};
        }

    } // namespace

    Error reflectionError(std::string message) {
        return Error{ErrorDomain::Reflection, 0, std::move(message)};
    }

    VoidResult TypeRegistry::registerType(TypeInfo type) {
        if (frozen_) {
            return std::unexpected{
                reflectionError("Cannot register reflected type after registry freeze: " +
                                type.name)};
        }

        auto validHeader = validateTypeHeader(type);
        if (!validHeader) {
            return validHeader;
        }

        auto validIdentity = validateTypeIdentity(types_, type);
        if (!validIdentity) {
            return validIdentity;
        }

        for (const FieldInfo& field : type.fields) {
            auto validFieldIdentity = validateFieldIdentity(type, field);
            if (!validFieldIdentity) {
                return validFieldIdentity;
            }

            auto validFlags = validateFieldFlags(type, field);
            if (!validFlags) {
                return validFlags;
            }

            if (findType(field.type) == nullptr) {
                return std::unexpected{reflectionError("Reflected field '" + type.name + "." +
                                                       field.name +
                                                       "' references an unregistered field type.")};
            }
        }

        types_.push_back(std::move(type));
        return {};
    }

    VoidResult TypeRegistry::freeze() {
        frozen_ = true;
        return {};
    }

    const TypeInfo* TypeRegistry::findType(TypeId typeId) const {
        const auto found = std::ranges::find_if(
            types_, [typeId](const TypeInfo& type) { return type.id == typeId; });
        return found == types_.end() ? nullptr : &*found;
    }

    const TypeInfo* TypeRegistry::findType(std::string_view name) const {
        const auto found = std::ranges::find_if(
            types_, [name](const TypeInfo& type) { return type.name == name; });
        return found == types_.end() ? nullptr : &*found;
    }

    VoidResult registerBuiltinTypes(TypeRegistry& registry) {
        std::array builtins{
            builtinType(builtin::kBoolName, TypeKind::Scalar),
            builtinType(builtin::kInt32Name, TypeKind::Scalar),
            builtinType(builtin::kUInt32Name, TypeKind::Scalar),
            builtinType(builtin::kUInt64Name, TypeKind::Scalar),
            builtinType(builtin::kFloatName, TypeKind::Scalar),
            builtinType(builtin::kDoubleName, TypeKind::Scalar),
            builtinType(builtin::kStringName, TypeKind::String),
        };

        for (TypeInfo& builtin : builtins) {
            auto registered = registry.registerType(std::move(builtin));
            if (!registered) {
                return std::unexpected{std::move(registered.error())};
            }
        }
        return {};
    }

} // namespace asharia::reflection
