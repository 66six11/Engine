#include "asharia/reflection/type_registry.hpp"

#include <algorithm>
#include <array>
#include <expected>
#include <initializer_list>
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

        [[nodiscard]] TypeInfo builtinType(std::string_view name, TypeKind kind) {
            return TypeInfo{
                .id = makeTypeId(name),
                .name = std::string{name},
                .version = 1,
                .kind = kind,
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

        [[nodiscard]] VoidResult validateFieldFlags(const TypeInfo& type, const FieldInfo& field) {
            if (field.flags.has(FieldFlag::Serializable) && field.flags.has(FieldFlag::Transient)) {
                return std::unexpected{
                    reflectionDiagnostic("Reflected field '" + type.name + "." + field.name +
                                             "' cannot be Serializable and Transient.",
                                         {
                                             {"operation", "register"},
                                             {"type", type.name},
                                             {"field", field.name},
                                             {"expected", "non-conflicting field flags"},
                                             {"actual", "Serializable+Transient"},
                                             {"version", std::to_string(type.version)},
                                         })};
            }

            if (field.flags.has(FieldFlag::EditorOnly) &&
                field.flags.has(FieldFlag::RuntimeVisible)) {
                return std::unexpected{
                    reflectionDiagnostic("Reflected field '" + type.name + "." + field.name +
                                             "' cannot be EditorOnly and RuntimeVisible.",
                                         {
                                             {"operation", "register"},
                                             {"type", type.name},
                                             {"field", field.name},
                                             {"expected", "non-conflicting field flags"},
                                             {"actual", "EditorOnly+RuntimeVisible"},
                                             {"version", std::to_string(type.version)},
                                         })};
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
                return std::unexpected{
                    reflectionDiagnostic("Reflected field '" + type.name + "." + field.name +
                                             "' references an unregistered field type.",
                                         {
                                             {"operation", "register"},
                                             {"type", type.name},
                                             {"field", field.name},
                                             {"expected", "registered field type"},
                                             {"actual", "missing"},
                                             {"version", std::to_string(type.version)},
                                         })};
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
