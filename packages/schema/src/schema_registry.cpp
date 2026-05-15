#include "asharia/schema/schema_registry.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace asharia::schema {
    namespace {

        struct DiagnosticField {
            std::string key;
            std::string value;

            DiagnosticField(const char* fieldKey, std::string_view fieldValue)
                : key{fieldKey}, value{fieldValue} {}
        };

        [[nodiscard]] std::string fieldIdText(FieldId fieldId) {
            return std::to_string(fieldId.value);
        }

        [[nodiscard]] Error makeSchemaError(std::string message,
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
            return Error{ErrorDomain::Schema, 0, std::move(message)};
        }

        [[nodiscard]] bool containsReservedFieldId(std::span<const FieldId> reserved,
                                                   FieldId fieldId) {
            return std::ranges::find(reserved, fieldId) != reserved.end();
        }

        [[nodiscard]] bool aliasMatches(std::span<const std::string> aliases,
                                        std::string_view key) {
            return std::ranges::any_of(aliases,
                                       [key](const std::string& alias) { return alias == key; });
        }

        [[nodiscard]] VoidResult validateTypeHeader(const TypeSchema& type) {
            if (!type.id) {
                return std::unexpected{makeSchemaError("Schema type has no stable id.",
                                                       {{"operation", "register"},
                                                        {"expected", "stable type id"},
                                                        {"actual", "empty"}})};
            }
            if (type.canonicalName.empty()) {
                return std::unexpected{makeSchemaError("Schema type has no canonical name.",
                                                       {{"operation", "register"},
                                                        {"type", type.id.stableName},
                                                        {"expected", "canonical name"},
                                                        {"actual", "empty"}})};
            }
            if (type.version == 0U) {
                return std::unexpected{makeSchemaError("Schema type version must be non-zero.",
                                                       {{"operation", "register"},
                                                        {"type", type.id.stableName},
                                                        {"expected", "non-zero version"},
                                                        {"actual", "0"}})};
            }
            return {};
        }

        [[nodiscard]] VoidResult validateFieldShape(const TypeSchema& type,
                                                    const FieldSchema& field) {
            if (!field.id) {
                return std::unexpected{makeSchemaError("Schema field has no explicit id.",
                                                       {{"operation", "register"},
                                                        {"type", type.id.stableName},
                                                        {"field", field.key},
                                                        {"expected", "non-zero field id"},
                                                        {"actual", "0"}})};
            }
            if (field.key.empty()) {
                return std::unexpected{
                    makeSchemaError("Schema field has no key.", {{"operation", "register"},
                                                                 {"type", type.id.stableName},
                                                                 {"fieldId", fieldIdText(field.id)},
                                                                 {"expected", "field key"},
                                                                 {"actual", "empty"}})};
            }
            if (!field.valueType) {
                return std::unexpected{makeSchemaError("Schema field has no value type.",
                                                       {{"operation", "register"},
                                                        {"type", type.id.stableName},
                                                        {"field", field.key},
                                                        {"expected", "schema value type"},
                                                        {"actual", "empty"}})};
            }
            if (containsReservedFieldId(type.reservedFieldIds, field.id)) {
                return std::unexpected{makeSchemaError("Schema field reuses a reserved field id.",
                                                       {{"operation", "register"},
                                                        {"type", type.id.stableName},
                                                        {"field", field.key},
                                                        {"fieldId", fieldIdText(field.id)},
                                                        {"expected", "unused field id"},
                                                        {"actual", "reserved"}})};
            }
            return {};
        }

        [[nodiscard]] VoidResult validateFieldDuplicates(const TypeSchema& type,
                                                         const FieldSchema& field) {
            const auto duplicateId = std::ranges::count_if(
                type.fields, [&field](const FieldSchema& other) { return other.id == field.id; });
            if (duplicateId > 1) {
                return std::unexpected{makeSchemaError("Schema type has duplicate field ids.",
                                                       {{"operation", "register"},
                                                        {"type", type.id.stableName},
                                                        {"fieldId", fieldIdText(field.id)},
                                                        {"expected", "unique field id"},
                                                        {"actual", "duplicate"}})};
            }

            const auto duplicateKey =
                std::ranges::count_if(type.fields, [&field](const FieldSchema& other) {
                    return other.key == field.key || aliasMatches(other.aliases, field.key);
                });
            if (duplicateKey > 1) {
                return std::unexpected{
                    makeSchemaError("Schema type has duplicate field keys or aliases.",
                                    {{"operation", "register"},
                                     {"type", type.id.stableName},
                                     {"field", field.key},
                                     {"expected", "unique field key"},
                                     {"actual", "duplicate"}})};
            }
            return {};
        }

        [[nodiscard]] VoidResult validateFieldAliases(const TypeSchema& type,
                                                      const FieldSchema& field) {
            for (const std::string& alias : field.aliases) {
                if (alias.empty()) {
                    return std::unexpected{makeSchemaError("Schema field alias is empty.",
                                                           {{"operation", "register"},
                                                            {"type", type.id.stableName},
                                                            {"field", field.key},
                                                            {"expected", "non-empty alias"},
                                                            {"actual", "empty"}})};
                }
                const bool collides =
                    std::ranges::any_of(type.fields, [&field, &alias](const FieldSchema& other) {
                        return &other != &field &&
                               (other.key == alias || aliasMatches(other.aliases, alias));
                    });
                if (collides) {
                    return std::unexpected{
                        makeSchemaError("Schema field alias collides with another field.",
                                        {{"operation", "register"},
                                         {"type", type.id.stableName},
                                         {"field", field.key},
                                         {"alias", alias},
                                         {"expected", "unique alias"},
                                         {"actual", "collision"}})};
                }
            }
            return {};
        }

        [[nodiscard]] VoidResult validateReservedFieldIds(const TypeSchema& type) {
            for (const FieldId reservedId : type.reservedFieldIds) {
                if (!reservedId) {
                    return std::unexpected{
                        makeSchemaError("Schema reserved field id must be non-zero.",
                                        {{"operation", "register"},
                                         {"type", type.id.stableName},
                                         {"expected", "non-zero reserved field id"},
                                         {"actual", "0"}})};
                }
                const auto duplicateReserved =
                    std::ranges::count(type.reservedFieldIds, reservedId);
                if (duplicateReserved > 1) {
                    return std::unexpected{
                        makeSchemaError("Schema type has duplicate reserved field ids.",
                                        {{"operation", "register"},
                                         {"type", type.id.stableName},
                                         {"fieldId", fieldIdText(reservedId)},
                                         {"expected", "unique reserved field id"},
                                         {"actual", "duplicate"}})};
                }
            }
            return {};
        }

        [[nodiscard]] VoidResult validateFieldIdentity(const TypeSchema& type) {
            for (const FieldSchema& field : type.fields) {
                if (auto valid = validateFieldShape(type, field); !valid) {
                    return valid;
                }
                if (auto unique = validateFieldDuplicates(type, field); !unique) {
                    return unique;
                }
                if (auto aliases = validateFieldAliases(type, field); !aliases) {
                    return aliases;
                }
            }
            return validateReservedFieldIds(type);
        }

        [[nodiscard]] VoidResult validateUniqueType(const TypeSchema& type,
                                                    std::span<const TypeSchema> registered) {
            const auto collidesWithLookupName = [&type](const TypeSchema& other) {
                return other.id.stableName == type.id.stableName ||
                       other.id.stableName == type.canonicalName ||
                       other.canonicalName == type.id.stableName ||
                       other.canonicalName == type.canonicalName;
            };

            for (const TypeSchema& other : registered) {
                if (collidesWithLookupName(other)) {
                    return std::unexpected{
                        makeSchemaError("Schema registry already contains this type.",
                                        {{"operation", "register"},
                                         {"type", type.id.stableName},
                                         {"canonicalName", type.canonicalName},
                                         {"expected", "unique type stable name and canonical name"},
                                         {"actual", "duplicate"}})};
                }
            }
            return {};
        }

        [[nodiscard]] bool isRegisteredType(std::span<const TypeSchema> types,
                                            const TypeId& typeId) {
            return std::ranges::any_of(
                types, [&typeId](const TypeSchema& type) { return type.id == typeId; });
        }

        [[nodiscard]] VoidResult validateFieldTypeReferences(std::span<const TypeSchema> types) {
            for (const TypeSchema& type : types) {
                for (const FieldSchema& field : type.fields) {
                    if (!isRegisteredType(types, field.valueType)) {
                        return std::unexpected{
                            makeSchemaError("Schema field references an unregistered type.",
                                            {{"operation", "freeze"},
                                             {"type", type.id.stableName},
                                             {"field", field.key},
                                             {"fieldType", field.valueType.stableName},
                                             {"expected", "registered field type"},
                                             {"actual", "missing"}})};
                    }
                }
            }
            return {};
        }

        [[nodiscard]] TypeSchema makeBuiltin(std::string_view name, ValueKind kind) {
            return TypeSchema{
                .id = makeTypeId(name),
                .canonicalName = std::string{name},
                .version = 1,
                .kind = kind,
                .fields = {},
                .reservedFieldIds = {},
                .metadata = {},
            };
        }

    } // namespace

    Error schemaError(std::string message) {
        return Error{ErrorDomain::Schema, 0, std::move(message)};
    }

    VoidResult SchemaRegistry::registerType(TypeSchema type) {
        if (frozen_) {
            return std::unexpected{makeSchemaError("Cannot register schema type after freeze.",
                                                   {{"operation", "register"},
                                                    {"type", type.id.stableName},
                                                    {"expected", "mutable registry"},
                                                    {"actual", "frozen"}})};
        }

        if (auto valid = validateTypeHeader(type); !valid) {
            return valid;
        }
        if (auto valid = validateFieldIdentity(type); !valid) {
            return valid;
        }
        if (auto unique = validateUniqueType(type, types_); !unique) {
            return unique;
        }

        types_.push_back(std::move(type));
        return {};
    }

    VoidResult SchemaRegistry::freeze() {
        if (auto valid = validateFieldTypeReferences(types_); !valid) {
            return valid;
        }
        frozen_ = true;
        return {};
    }

    const TypeSchema* SchemaRegistry::findType(const TypeId& typeId) const {
        if (!typeId) {
            return nullptr;
        }
        return findType(typeId.stableName);
    }

    const TypeSchema* SchemaRegistry::findType(std::string_view stableName) const {
        const auto found = std::ranges::find_if(types_, [stableName](const TypeSchema& type) {
            return type.id.stableName == stableName || type.canonicalName == stableName;
        });
        return found == types_.end() ? nullptr : &*found;
    }

    VoidResult registerBuiltinSchemas(SchemaRegistry& registry) {
        const std::array builtins{
            makeBuiltin(builtin::kBoolName, ValueKind::Bool),
            makeBuiltin(builtin::kInt32Name, ValueKind::Integer),
            makeBuiltin(builtin::kUInt32Name, ValueKind::Integer),
            makeBuiltin(builtin::kUInt64Name, ValueKind::Integer),
            makeBuiltin(builtin::kFloatName, ValueKind::Float),
            makeBuiltin(builtin::kDoubleName, ValueKind::Float),
            makeBuiltin(builtin::kStringName, ValueKind::String),
        };

        for (TypeSchema type : builtins) {
            auto registered = registry.registerType(std::move(type));
            if (!registered) {
                return registered;
            }
        }
        return {};
    }

} // namespace asharia::schema
