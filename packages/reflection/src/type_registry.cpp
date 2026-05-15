#include "asharia/reflection/type_registry.hpp"

#include <algorithm>
#include <array>
#include <expected>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
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

        [[nodiscard]] bool hasDuplicateAttributeKey(std::span<const FieldAttribute> attributes,
                                                    const FieldAttribute& attribute) {
            return std::ranges::count_if(attributes, [&attribute](const FieldAttribute& other) {
                       return other.key == attribute.key;
                   }) > 1;
        }

        [[nodiscard]] bool hasAttributeNamespace(std::string_view key) {
            return key.find('.') != std::string_view::npos;
        }

        [[nodiscard]] TypeInfo builtinType(std::string_view name, TypeKind kind) {
            return TypeInfo{
                .id = makeTypeId(name),
                .name = std::string{name},
                .version = 1,
                .kind = kind,
                .attributes = {},
                .fields = {},
            };
        }

        struct DiagnosticField {
            std::string key;
            std::string value;

            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
            DiagnosticField(std::string_view fieldKey, std::string_view fieldValue)
                : key{fieldKey}, value{fieldValue} {}
        };

        [[nodiscard]] Error
        reflectionDiagnostic(std::string message,
                             std::initializer_list<DiagnosticField> details = {}) {
            bool wroteHeader = false;
            for (const DiagnosticField& detail : details) {
                if (detail.value.empty()) {
                    continue;
                }
                if (!wroteHeader) {
                    message += " [";
                    wroteHeader = true;
                } else {
                    message += "; ";
                }
                message += detail.key;
                message += '=';
                message += detail.value;
            }
            if (wroteHeader) {
                message += ']';
            }
            return Error{ErrorDomain::Reflection, 0, std::move(message)};
        }

        [[nodiscard]] const TypeInfo* findRegisteredType(std::span<const TypeInfo> types,
                                                         TypeId typeId) {
            const auto found = std::ranges::find_if(
                types, [typeId](const TypeInfo& type) { return type.id == typeId; });
            return found == types.end() ? nullptr : &*found;
        }

        [[nodiscard]] VoidResult validateTypeHeader(const TypeInfo& type) {
            if (type.name.empty()) {
                return std::unexpected{
                    reflectionDiagnostic("Cannot register reflected type with no name.",
                                         {
                                             {"operation", "register"},
                                             {"expected", "non-empty type name"},
                                             {"actual", "empty"},
                                         })};
            }

            if (!type.id) {
                return std::unexpected{reflectionDiagnostic(
                    "Cannot register reflected type with invalid id: " + type.name,
                    {
                        {"operation", "register"},
                        {"type", type.name},
                        {"expected", "valid type id"},
                        {"actual", "0"},
                        {"version", std::to_string(type.version)},
                    })};
            }

            return {};
        }

        [[nodiscard]] VoidResult validateTypeIdentity(std::span<const TypeInfo> types,
                                                      const TypeInfo& type) {
            for (const TypeInfo& existing : types) {
                if (existing.name == type.name) {
                    return std::unexpected{
                        reflectionDiagnostic("Duplicate reflected type name: " + type.name,
                                             {
                                                 {"operation", "register"},
                                                 {"type", type.name},
                                                 {"expected", "unique type name"},
                                                 {"actual", "duplicate"},
                                                 {"version", std::to_string(type.version)},
                                             })};
                }
                if (existing.id == type.id) {
                    return std::unexpected{
                        reflectionDiagnostic("Reflected type id collision between '" +
                                                 existing.name + "' and '" + type.name + "'.",
                                             {
                                                 {"operation", "register"},
                                                 {"type", type.name},
                                                 {"expected", "unique type id"},
                                                 {"actual", existing.name},
                                                 {"version", std::to_string(type.version)},
                                             })};
                }
            }

            return {};
        }

        [[nodiscard]] VoidResult validateTypeAttributes(const TypeInfo& type) {
            for (const FieldAttribute& attribute : type.attributes) {
                if (attribute.key.empty()) {
                    return std::unexpected{reflectionDiagnostic(
                        "Reflected type '" + type.name + "' has an attribute with no key.",
                        {
                            {"operation", "register"},
                            {"type", type.name},
                            {"expected", "non-empty attribute key"},
                            {"actual", "empty"},
                            {"version", std::to_string(type.version)},
                        })};
                }
                if (!hasAttributeNamespace(attribute.key)) {
                    return std::unexpected{reflectionDiagnostic(
                        "Reflected type '" + type.name + "' has non-namespaced attribute key '" +
                            attribute.key + "'.",
                        {
                            {"operation", "register"},
                            {"type", type.name},
                            {"attribute", attribute.key},
                            {"expected", "namespaced attribute key"},
                            {"actual", attribute.key},
                            {"version", std::to_string(type.version)},
                        })};
                }
                if (hasDuplicateAttributeKey(type.attributes, attribute)) {
                    return std::unexpected{reflectionDiagnostic(
                        "Reflected type '" + type.name + "' has duplicate attribute key '" +
                            attribute.key + "'.",
                        {
                            {"operation", "register"},
                            {"type", type.name},
                            {"attribute", attribute.key},
                            {"expected", "unique attribute key"},
                            {"actual", "duplicate"},
                            {"version", std::to_string(type.version)},
                        })};
                }
            }

            return {};
        }

        [[nodiscard]] VoidResult validateFieldIdentity(const TypeInfo& type,
                                                       const FieldInfo& field) {
            if (field.name.empty()) {
                return std::unexpected{reflectionDiagnostic(
                    "Reflected type '" + type.name + "' has a field with no name.",
                    {
                        {"operation", "register"},
                        {"type", type.name},
                        {"expected", "non-empty field name"},
                        {"actual", "empty"},
                        {"version", std::to_string(type.version)},
                    })};
            }
            if (!field.id) {
                return std::unexpected{reflectionDiagnostic(
                    "Reflected field '" + type.name + "." + field.name + "' has an invalid id.",
                    {
                        {"operation", "register"},
                        {"type", type.name},
                        {"field", field.name},
                        {"expected", "valid field id"},
                        {"actual", "0"},
                        {"version", std::to_string(type.version)},
                    })};
            }
            if (!field.type) {
                return std::unexpected{
                    reflectionDiagnostic("Reflected field '" + type.name + "." + field.name +
                                             "' has an unknown type id.",
                                         {
                                             {"operation", "register"},
                                             {"type", type.name},
                                             {"field", field.name},
                                             {"expected", "valid field type id"},
                                             {"actual", "0"},
                                             {"version", std::to_string(type.version)},
                                         })};
            }
            if (hasDuplicateFieldName(type, field)) {
                return std::unexpected{
                    reflectionDiagnostic("Reflected type '" + type.name +
                                             "' has duplicate field name '" + field.name + "'.",
                                         {
                                             {"operation", "register"},
                                             {"type", type.name},
                                             {"field", field.name},
                                             {"expected", "unique field name"},
                                             {"actual", "duplicate"},
                                             {"version", std::to_string(type.version)},
                                         })};
            }
            if (hasDuplicateFieldId(type, field)) {
                return std::unexpected{
                    reflectionDiagnostic("Reflected type '" + type.name +
                                             "' has duplicate field id for '" + field.name + "'.",
                                         {
                                             {"operation", "register"},
                                             {"type", type.name},
                                             {"field", field.name},
                                             {"expected", "unique field id"},
                                             {"actual", "duplicate"},
                                             {"version", std::to_string(type.version)},
                                         })};
            }

            return {};
        }

        [[nodiscard]] VoidResult validateFieldAttributes(const TypeInfo& type,
                                                         const FieldInfo& field) {
            for (const FieldAttribute& attribute : field.attributes) {
                if (attribute.key.empty()) {
                    return std::unexpected{
                        reflectionDiagnostic("Reflected field '" + type.name + "." + field.name +
                                                 "' has an attribute with no key.",
                                             {
                                                 {"operation", "register"},
                                                 {"type", type.name},
                                                 {"field", field.name},
                                                 {"expected", "non-empty attribute key"},
                                                 {"actual", "empty"},
                                                 {"version", std::to_string(type.version)},
                                             })};
                }
                if (!hasAttributeNamespace(attribute.key)) {
                    return std::unexpected{reflectionDiagnostic(
                        "Reflected field '" + type.name + "." + field.name +
                            "' has non-namespaced attribute key '" + attribute.key + "'.",
                        {
                            {"operation", "register"},
                            {"type", type.name},
                            {"field", field.name},
                            {"attribute", attribute.key},
                            {"expected", "namespaced attribute key"},
                            {"actual", attribute.key},
                            {"version", std::to_string(type.version)},
                        })};
                }
                if (hasDuplicateAttributeKey(field.attributes, attribute)) {
                    return std::unexpected{reflectionDiagnostic(
                        "Reflected field '" + type.name + "." + field.name +
                            "' has duplicate attribute key '" + attribute.key + "'.",
                        {
                            {"operation", "register"},
                            {"type", type.name},
                            {"field", field.name},
                            {"attribute", attribute.key},
                            {"expected", "unique attribute key"},
                            {"actual", "duplicate"},
                            {"version", std::to_string(type.version)},
                        })};
                }
            }

            return {};
        }

        [[nodiscard]] VoidResult validateFieldAccessor(const TypeInfo& type,
                                                       const FieldInfo& field) {
            if (field.accessor.ownerType != type.id) {
                return std::unexpected{reflectionDiagnostic(
                    "Reflected field '" + type.name + "." + field.name +
                        "' has an accessor owner type mismatch.",
                    {
                        {"operation", "register"},
                        {"type", type.name},
                        {"field", field.name},
                        {"expected", std::to_string(type.id.value)},
                        {"actual", std::to_string(field.accessor.ownerType.value)},
                        {"version", std::to_string(type.version)},
                    })};
            }

            if (field.accessor.fieldType != field.type) {
                return std::unexpected{reflectionDiagnostic(
                    "Reflected field '" + type.name + "." + field.name +
                        "' has an accessor field type mismatch.",
                    {
                        {"operation", "register"},
                        {"type", type.name},
                        {"field", field.name},
                        {"expected", std::to_string(field.type.value)},
                        {"actual", std::to_string(field.accessor.fieldType.value)},
                        {"version", std::to_string(type.version)},
                    })};
            }

            if (!field.accessor.readAddress && !field.accessor.readValue) {
                return std::unexpected{
                    reflectionDiagnostic("Reflected field '" + type.name + "." + field.name +
                                             "' has no readable accessor.",
                                         {
                                             {"operation", "register"},
                                             {"type", type.name},
                                             {"field", field.name},
                                             {"expected", "read address or read value accessor"},
                                             {"actual", "missing"},
                                             {"version", std::to_string(type.version)},
                                         })};
            }

            const bool requiresTemporaryValue =
                field.accessor.readValue || field.accessor.writeValue;
            if (requiresTemporaryValue &&
                (!field.accessor.constructValue || !field.accessor.destroyValue ||
                 field.accessor.size == 0 || field.accessor.alignment == 0)) {
                return std::unexpected{reflectionDiagnostic(
                    "Reflected field '" + type.name + "." + field.name +
                        "' has value accessors without complete temporary value support.",
                    {
                        {"operation", "register"},
                        {"type", type.name},
                        {"field", field.name},
                        {"expected", "construct/destroy value accessor with size and alignment"},
                        {"actual", "incomplete temporary value accessor"},
                        {"version", std::to_string(type.version)},
                    })};
            }

            if (field.defaultProvider && field.defaultProvider.valueType != field.type) {
                return std::unexpected{reflectionDiagnostic(
                    "Reflected field '" + type.name + "." + field.name +
                        "' has a default provider type mismatch.",
                    {
                        {"operation", "register"},
                        {"type", type.name},
                        {"field", field.name},
                        {"expected", std::to_string(field.type.value)},
                        {"actual", std::to_string(field.defaultProvider.valueType.value)},
                        {"version", std::to_string(type.version)},
                    })};
            }

            if (field.defaultProvider && !field.accessor.writeAddress &&
                !field.accessor.writeValue) {
                return std::unexpected{
                    reflectionDiagnostic("Reflected field '" + type.name + "." + field.name +
                                             "' has a default provider but no writable accessor.",
                                         {
                                             {"operation", "register"},
                                             {"type", type.name},
                                             {"field", field.name},
                                             {"expected", "write address or write value accessor"},
                                             {"actual", "missing"},
                                             {"version", std::to_string(type.version)},
                                         })};
            }

            if (field.validator && field.validator.valueType != field.type) {
                return std::unexpected{reflectionDiagnostic(
                    "Reflected field '" + type.name + "." + field.name +
                        "' has a validator type mismatch.",
                    {
                        {"operation", "register"},
                        {"type", type.name},
                        {"field", field.name},
                        {"expected", std::to_string(field.type.value)},
                        {"actual", std::to_string(field.validator.valueType.value)},
                        {"version", std::to_string(type.version)},
                    })};
            }

            return {};
        }

        [[nodiscard]] VoidResult validateFieldTypeReferences(std::span<const TypeInfo> types) {
            for (const TypeInfo& type : types) {
                for (const FieldInfo& field : type.fields) {
                    if (findRegisteredType(types, field.type) != nullptr) {
                        continue;
                    }

                    return std::unexpected{reflectionDiagnostic(
                        "Cannot freeze reflected registry because field '" + type.name + "." +
                            field.name + "' references an unregistered field type.",
                        {
                            {"operation", "freeze"},
                            {"type", type.name},
                            {"field", field.name},
                            {"typeId", std::to_string(field.type.value)},
                            {"expected", "registered field type"},
                            {"actual", "missing"},
                            {"version", std::to_string(type.version)},
                        })};
                }
            }

            return {};
        }

    } // namespace

    Error reflectionError(std::string message) {
        return Error{ErrorDomain::Reflection, 0, std::move(message)};
    }

    VoidResult TypeRegistry::registerType(TypeInfo type) {
        if (frozen_) {
            return std::unexpected{reflectionDiagnostic(
                "Cannot register reflected type after registry freeze: " + type.name,
                {
                    {"operation", "register"},
                    {"type", type.name},
                    {"expected", "mutable registry"},
                    {"actual", "frozen"},
                    {"version", std::to_string(type.version)},
                })};
        }

        auto validHeader = validateTypeHeader(type);
        if (!validHeader) {
            return validHeader;
        }

        auto validIdentity = validateTypeIdentity(types_, type);
        if (!validIdentity) {
            return validIdentity;
        }

        auto validTypeAttributes = validateTypeAttributes(type);
        if (!validTypeAttributes) {
            return validTypeAttributes;
        }

        for (const FieldInfo& field : type.fields) {
            auto validFieldIdentity = validateFieldIdentity(type, field);
            if (!validFieldIdentity) {
                return validFieldIdentity;
            }

            auto validFieldAttributes = validateFieldAttributes(type, field);
            if (!validFieldAttributes) {
                return validFieldAttributes;
            }

            auto validFieldAccessor = validateFieldAccessor(type, field);
            if (!validFieldAccessor) {
                return validFieldAccessor;
            }
        }

        types_.push_back(std::move(type));
        return {};
    }

    VoidResult TypeRegistry::freeze() {
        if (frozen_) {
            return {};
        }

        auto validFieldTypes = validateFieldTypeReferences(types_);
        if (!validFieldTypes) {
            return validFieldTypes;
        }

        frozen_ = true;
        return {};
    }

    const TypeInfo* TypeRegistry::findType(TypeId typeId) const {
        return findRegisteredType(types_, typeId);
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
