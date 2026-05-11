#include "asharia/serialization/serializer.hpp"

#include <expected>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/core/error.hpp"
#include "asharia/reflection/context_view.hpp"

namespace asharia::serialization {
    namespace {

        struct DiagnosticField {
            std::string_view key;
            std::string_view value;

            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
            DiagnosticField(std::string_view fieldKey, std::string_view fieldValue) noexcept
                : key{fieldKey}
                , value{fieldValue} {}
        };

        [[nodiscard]] Error
        serializationError(std::string message,
                           std::initializer_list<DiagnosticField> details = {}) {
            bool wroteHeader = false;
            for (const DiagnosticField detail : details) {
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
            return Error{ErrorDomain::Serialization, 0, std::move(message)};
        }

        [[nodiscard]] std::string_view archiveKindName(ArchiveValueKind kind) noexcept {
            switch (kind) {
            case ArchiveValueKind::Null:
                return "null";
            case ArchiveValueKind::Bool:
                return "bool";
            case ArchiveValueKind::Integer:
                return "integer";
            case ArchiveValueKind::Float:
                return "float";
            case ArchiveValueKind::String:
                return "string";
            case ArchiveValueKind::Array:
                return "array";
            case ArchiveValueKind::Object:
                return "object";
            }
            return "unknown";
        }

        [[nodiscard]] std::string archiveValueActual(const ArchiveValue* value) {
            if (value == nullptr) {
                return "missing";
            }
            if (value->kind == ArchiveValueKind::Integer) {
                return "integer " + std::to_string(value->integerValue);
            }
            return std::string{archiveKindName(value->kind)};
        }

        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        [[nodiscard]] std::string appendFieldPath(std::string_view objectPath,
                                                  std::string_view fieldName) {
            std::string path{objectPath};
            if (path.empty()) {
                return std::string{fieldName};
            }
            path += '.';
            path += fieldName;
            return path;
        }

        [[nodiscard]] Result<ArchiveValue> serializeValue(const reflection::TypeRegistry& registry,
                                                          reflection::TypeId type,
                                                          const void* object,
                                                          const SerializationPolicy& policy,
                                                          std::string_view path);

        [[nodiscard]] VoidResult deserializeValue(const reflection::TypeRegistry& registry,
                                                  reflection::TypeId type,
                                                  const ArchiveValue& value, void* object,
                                                  const SerializationPolicy& policy,
                                                  std::string_view path);

        [[nodiscard]] bool isIntegerKind(ArchiveValueKind kind) {
            return kind == ArchiveValueKind::Integer;
        }

        [[nodiscard]] bool isFloatCompatibleKind(ArchiveValueKind kind) {
            return kind == ArchiveValueKind::Float || kind == ArchiveValueKind::Integer;
        }

        [[nodiscard]] Result<std::uint32_t> readArchiveVersion(const reflection::TypeInfo& type,
                                                               const ArchiveValue& value,
                                                               std::string_view objectPath) {
            const ArchiveValue* archiveType = value.findMemberValue("type");
            if (archiveType == nullptr || archiveType->kind != ArchiveValueKind::String ||
                archiveType->stringValue != type.name) {
                return std::unexpected{serializationError(
                    "Archive type header is missing or does not match requested type '" +
                        type.name + "'.",
                    {
                        {"operation", "deserialize"},
                        {"objectPath", objectPath},
                        {"type", type.name},
                        {"field", "type"},
                        {"expected", type.name},
                        {"actual", archiveValueActual(archiveType)},
                        {"version", std::to_string(type.version)},
                    })};
            }

            const ArchiveValue* archiveVersion = value.findMemberValue("version");
            if (archiveVersion == nullptr || archiveVersion->kind != ArchiveValueKind::Integer ||
                archiveVersion->integerValue < 0 ||
                static_cast<std::uint64_t>(archiveVersion->integerValue) >
                    std::numeric_limits<std::uint32_t>::max()) {
                return std::unexpected{serializationError(
                    "Archive version header is missing or invalid for requested type '" +
                        type.name + "'.",
                    {
                        {"operation", "deserialize"},
                        {"objectPath", objectPath},
                        {"type", type.name},
                        {"field", "version"},
                        {"expected", "uint32 schema version"},
                        {"actual", archiveValueActual(archiveVersion)},
                        {"version", std::to_string(type.version)},
                    })};
            }

            return static_cast<std::uint32_t>(archiveVersion->integerValue);
        }

        [[nodiscard]] Result<ArchiveValue>
        migrateArchiveIfNeeded(const reflection::TypeInfo& type, const ArchiveValue& value,
                               const SerializationPolicy& policy,
                               std::string_view objectPath) {
            if (!policy.includeTypeHeader) {
                return value;
            }

            auto archiveVersion = readArchiveVersion(type, value, objectPath);
            if (!archiveVersion) {
                return std::unexpected{std::move(archiveVersion.error())};
            }
            if (*archiveVersion == type.version) {
                return value;
            }
            if (policy.migrations == nullptr) {
                return std::unexpected{serializationError(
                    "Archive version " + std::to_string(*archiveVersion) + " for type '" +
                        type.name + "' requires migration to version " +
                        std::to_string(type.version) + ".",
                    {
                        {"operation", "deserialize"},
                        {"objectPath", objectPath},
                        {"type", type.name},
                        {"field", "version"},
                        {"expected", "migration policy"},
                        {"actual", "no migration registry"},
                        {"version", std::to_string(*archiveVersion)},
                    })};
            }

            auto migrated =
                policy.migrations->migrateObject(type.id, *archiveVersion, type.version, value);
            if (!migrated) {
                return std::unexpected{std::move(migrated.error())};
            }

            auto migratedVersion = readArchiveVersion(type, *migrated, objectPath);
            if (!migratedVersion) {
                return std::unexpected{std::move(migratedVersion.error())};
            }
            if (*migratedVersion != type.version) {
                return std::unexpected{serializationError(
                    "Serialization migration for type '" + type.name +
                        "' did not produce requested version " + std::to_string(type.version) + ".",
                    {
                        {"operation", "migrate"},
                        {"objectPath", objectPath},
                        {"type", type.name},
                        {"field", "version"},
                        {"expected", std::to_string(type.version)},
                        {"actual", std::to_string(*migratedVersion)},
                    })};
            }

            return migrated;
        }

        [[nodiscard]] Result<ArchiveValue> serializeStruct(const reflection::TypeRegistry& registry,
                                                           const reflection::TypeInfo& type,
                                                           const void* object,
                                                           const SerializationPolicy& policy,
                                                           std::string_view path) {
            const std::string objectPath = path.empty() ? type.name : std::string{path};
            if (object == nullptr) {
                return std::unexpected{serializationError(
                    "Cannot serialize null object for type '" + type.name + "'.",
                    {
                        {"operation", "serialize"},
                        {"objectPath", objectPath},
                        {"type", type.name},
                        {"expected", "non-null object"},
                        {"actual", "null"},
                        {"version", std::to_string(type.version)},
                    })};
            }

            std::vector<ArchiveMember> fieldMembers;
            const reflection::ContextFieldView serializeView =
                reflection::makeSerializeContextView(type);
            fieldMembers.reserve(serializeView.fields.size());

            for (const reflection::FieldInfo* field : serializeView.fields) {
                const std::string fieldObjectPath =
                    appendFieldPath(objectPath, field == nullptr ? "<null>" : field->name);
                if (field == nullptr || !field->accessor.readAddress) {
                    return std::unexpected{serializationError(
                        "Serializable field has no read accessor.",
                        {
                            {"operation", "serialize"},
                            {"objectPath", fieldObjectPath},
                            {"type", type.name},
                            {"field", field == nullptr ? "<null>" : field->name},
                            {"expected", "read accessor"},
                            {"actual", "missing"},
                            {"version", std::to_string(type.version)},
                        })};
                }

                const void* fieldAddress = field->accessor.readAddress(object);
                if (fieldAddress == nullptr) {
                    return std::unexpected{serializationError(
                        "Serializable field read returned null.",
                        {
                            {"operation", "serialize"},
                            {"objectPath", fieldObjectPath},
                            {"type", type.name},
                            {"field", field->name},
                            {"expected", "field address"},
                            {"actual", "null"},
                            {"version", std::to_string(type.version)},
                        })};
                }

                auto fieldValue =
                    serializeValue(registry, field->type, fieldAddress, policy, fieldObjectPath);
                if (!fieldValue) {
                    return std::unexpected{std::move(fieldValue.error())};
                }

                fieldMembers.push_back(ArchiveMember{
                    .key = field->name,
                    .value = std::move(*fieldValue),
                });
            }

            std::vector<ArchiveMember> objectMembers;
            if (policy.includeTypeHeader) {
                objectMembers.push_back(ArchiveMember{
                    .key = "type",
                    .value = ArchiveValue::string(type.name),
                });
                objectMembers.push_back(ArchiveMember{
                    .key = "version",
                    .value = ArchiveValue::integer(type.version),
                });
            }
            objectMembers.push_back(ArchiveMember{
                .key = "fields",
                .value = ArchiveValue::object(std::move(fieldMembers)),
            });
            return ArchiveValue::object(std::move(objectMembers));
        }

        [[nodiscard]] Result<ArchiveValue> serializeValue(const reflection::TypeRegistry& registry,
                                                          reflection::TypeId type,
                                                          const void* object,
                                                          const SerializationPolicy& policy,
                                                          std::string_view path) {
            if (object == nullptr) {
                return std::unexpected{serializationError(
                    "Cannot serialize null value.",
                    {
                        {"operation", "serialize"},
                        {"objectPath", path},
                        {"expected", "non-null value"},
                        {"actual", "null"},
                    })};
            }
            if (type == reflection::builtin::boolTypeId()) {
                return ArchiveValue::boolean(*static_cast<const bool*>(object));
            }
            if (type == reflection::builtin::int32TypeId()) {
                return ArchiveValue::integer(*static_cast<const std::int32_t*>(object));
            }
            if (type == reflection::builtin::uint32TypeId()) {
                return ArchiveValue::integer(*static_cast<const std::uint32_t*>(object));
            }
            if (type == reflection::builtin::uint64TypeId()) {
                const auto value = *static_cast<const std::uint64_t*>(object);
                if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                    return std::unexpected{serializationError(
                        "Cannot serialize uint64 value outside archive integer range.",
                        {
                            {"operation", "serialize"},
                            {"objectPath", path},
                            {"type", reflection::builtin::kUInt64Name},
                            {"expected", "uint64 <= int64 max"},
                            {"actual", "uint64 out of archive integer range"},
                        })};
                }
                return ArchiveValue::integer(static_cast<std::int64_t>(value));
            }
            if (type == reflection::builtin::floatTypeId()) {
                return ArchiveValue::floating(*static_cast<const float*>(object));
            }
            if (type == reflection::builtin::doubleTypeId()) {
                return ArchiveValue::floating(*static_cast<const double*>(object));
            }
            if (type == reflection::builtin::stringTypeId()) {
                return ArchiveValue::string(*static_cast<const std::string*>(object));
            }

            const reflection::TypeInfo* typeInfo = registry.findType(type);
            if (typeInfo == nullptr) {
                return std::unexpected{serializationError(
                    "Cannot serialize unknown reflected type id.",
                    {
                        {"operation", "serialize"},
                        {"objectPath", path},
                        {"typeId", std::to_string(type.value)},
                        {"expected", "registered reflected type"},
                        {"actual", "missing"},
                    })};
            }
            return serializeStruct(registry, *typeInfo, object, policy, path);
        }

        [[nodiscard]] VoidResult validateNoUnknownFields(const reflection::TypeInfo& type,
                                                         const ArchiveValue& fieldsValue,
                                                         std::string_view objectPath) {
            if (fieldsValue.kind != ArchiveValueKind::Object) {
                return {};
            }

            for (const ArchiveMember& member : fieldsValue.objectValue) {
                bool known = false;
                for (const reflection::FieldInfo& field : type.fields) {
                    if (reflection::isSerializableField(field) && field.name == member.key) {
                        known = true;
                        break;
                    }
                }
                if (!known) {
                    return std::unexpected{serializationError(
                        "Archive contains unknown field '" + type.name + "." + member.key + "'.",
                        {
                            {"operation", "deserialize"},
                            {"objectPath", appendFieldPath(objectPath, member.key)},
                            {"type", type.name},
                            {"field", member.key},
                            {"expected", "registered serializable field"},
                            {"actual", "unknown field"},
                            {"version", std::to_string(type.version)},
                        })};
                }
            }
            return {};
        }

        [[nodiscard]] VoidResult deserializeStruct(const reflection::TypeRegistry& registry,
                                                   const reflection::TypeInfo& type,
                                                   const ArchiveValue& value, void* object,
                                                   const SerializationPolicy& policy,
                                                   std::string_view path) {
            const std::string objectPath = path.empty() ? type.name : std::string{path};
            if (object == nullptr) {
                return std::unexpected{serializationError(
                    "Cannot deserialize null object for type '" + type.name + "'.",
                    {
                        {"operation", "deserialize"},
                        {"objectPath", objectPath},
                        {"type", type.name},
                        {"expected", "non-null object"},
                        {"actual", "null"},
                        {"version", std::to_string(type.version)},
                    })};
            }
            if (value.kind != ArchiveValueKind::Object) {
                return std::unexpected{serializationError(
                    "Expected object archive for type '" + type.name + "'.",
                    {
                        {"operation", "deserialize"},
                        {"objectPath", objectPath},
                        {"type", type.name},
                        {"expected", "object archive"},
                        {"actual", archiveKindName(value.kind)},
                        {"version", std::to_string(type.version)},
                    })};
            }

            auto migratedValue = migrateArchiveIfNeeded(type, value, policy, objectPath);
            if (!migratedValue) {
                return std::unexpected{std::move(migratedValue.error())};
            }

            const ArchiveValue* fieldsValue = migratedValue->findMemberValue("fields");
            if (fieldsValue == nullptr || fieldsValue->kind != ArchiveValueKind::Object) {
                return std::unexpected{serializationError(
                    "Archive object for type '" + type.name +
                        "' is missing an object 'fields' member.",
                    {
                        {"operation", "deserialize"},
                        {"objectPath", objectPath},
                        {"type", type.name},
                        {"field", "fields"},
                        {"expected", "object"},
                        {"actual", archiveValueActual(fieldsValue)},
                        {"version", std::to_string(type.version)},
                    })};
            }

            if (!policy.allowUnknownFields) {
                auto unknownFields = validateNoUnknownFields(type, *fieldsValue, objectPath);
                if (!unknownFields) {
                    return unknownFields;
                }
            }

            const reflection::ContextFieldView serializeView =
                reflection::makeSerializeContextView(type);
            for (const reflection::FieldInfo* field : serializeView.fields) {
                const std::string fieldObjectPath = appendFieldPath(objectPath, field->name);
                const ArchiveValue* fieldValue = fieldsValue->findMemberValue(field->name);
                if (fieldValue == nullptr) {
                    continue;
                }
                if (!field->accessor.writeAddress) {
                    return std::unexpected{serializationError(
                        "Serializable field has no write accessor.",
                        {
                            {"operation", "deserialize"},
                            {"objectPath", fieldObjectPath},
                            {"type", type.name},
                            {"field", field->name},
                            {"expected", "write accessor"},
                            {"actual", "missing"},
                            {"version", std::to_string(type.version)},
                        })};
                }

                void* fieldAddress = field->accessor.writeAddress(object);
                if (fieldAddress == nullptr) {
                    return std::unexpected{serializationError(
                        "Serializable field write returned null.",
                        {
                            {"operation", "deserialize"},
                            {"objectPath", fieldObjectPath},
                            {"type", type.name},
                            {"field", field->name},
                            {"expected", "field address"},
                            {"actual", "null"},
                            {"version", std::to_string(type.version)},
                        })};
                }

                auto written =
                    deserializeValue(registry, field->type, *fieldValue, fieldAddress, policy,
                                     fieldObjectPath);
                if (!written) {
                    return written;
                }
            }
            return {};
        }

        [[nodiscard]] VoidResult deserializeBoolValue(const ArchiveValue& value, void* object,
                                                      std::string_view path) {
            if (value.kind != ArchiveValueKind::Bool) {
                return std::unexpected{serializationError(
                    "Expected bool archive value for field '" + std::string{path} + "'.",
                    {
                        {"operation", "deserialize"},
                        {"objectPath", path},
                        {"type", reflection::builtin::kBoolName},
                        {"expected", "bool"},
                        {"actual", archiveKindName(value.kind)},
                    })};
            }
            *static_cast<bool*>(object) = value.boolValue;
            return {};
        }

        [[nodiscard]] VoidResult deserializeInt32Value(const ArchiveValue& value, void* object,
                                                       std::string_view path) {
            if (!isIntegerKind(value.kind) ||
                value.integerValue < std::numeric_limits<std::int32_t>::min() ||
                value.integerValue > std::numeric_limits<std::int32_t>::max()) {
                return std::unexpected{serializationError(
                    "Expected int32 archive value for field '" + std::string{path} + "'.",
                    {
                        {"operation", "deserialize"},
                        {"objectPath", path},
                        {"type", reflection::builtin::kInt32Name},
                        {"expected", "int32"},
                        {"actual", archiveKindName(value.kind)},
                    })};
            }
            *static_cast<std::int32_t*>(object) = static_cast<std::int32_t>(value.integerValue);
            return {};
        }

        [[nodiscard]] VoidResult deserializeUInt32Value(const ArchiveValue& value, void* object,
                                                        std::string_view path) {
            if (!isIntegerKind(value.kind) || value.integerValue < 0 ||
                static_cast<std::uint64_t>(value.integerValue) >
                    std::numeric_limits<std::uint32_t>::max()) {
                return std::unexpected{serializationError(
                    "Expected uint32 archive value for field '" + std::string{path} + "'.",
                    {
                        {"operation", "deserialize"},
                        {"objectPath", path},
                        {"type", reflection::builtin::kUInt32Name},
                        {"expected", "uint32"},
                        {"actual", archiveKindName(value.kind)},
                    })};
            }
            *static_cast<std::uint32_t*>(object) = static_cast<std::uint32_t>(value.integerValue);
            return {};
        }

        [[nodiscard]] VoidResult deserializeUInt64Value(const ArchiveValue& value, void* object,
                                                        std::string_view path) {
            if (!isIntegerKind(value.kind) || value.integerValue < 0) {
                return std::unexpected{serializationError(
                    "Expected uint64 archive value for field '" + std::string{path} + "'.",
                    {
                        {"operation", "deserialize"},
                        {"objectPath", path},
                        {"type", reflection::builtin::kUInt64Name},
                        {"expected", "uint64"},
                        {"actual", archiveKindName(value.kind)},
                    })};
            }
            *static_cast<std::uint64_t*>(object) = static_cast<std::uint64_t>(value.integerValue);
            return {};
        }

        [[nodiscard]] VoidResult deserializeFloatValue(const ArchiveValue& value, void* object,
                                                       std::string_view path) {
            if (!isFloatCompatibleKind(value.kind)) {
                return std::unexpected{serializationError(
                    "Expected float archive value for field '" + std::string{path} + "'.",
                    {
                        {"operation", "deserialize"},
                        {"objectPath", path},
                        {"type", reflection::builtin::kFloatName},
                        {"expected", "float"},
                        {"actual", archiveKindName(value.kind)},
                    })};
            }
            *static_cast<float*>(object) = value.kind == ArchiveValueKind::Integer
                                               ? static_cast<float>(value.integerValue)
                                               : static_cast<float>(value.floatValue);
            return {};
        }

        [[nodiscard]] VoidResult deserializeDoubleValue(const ArchiveValue& value, void* object,
                                                        std::string_view path) {
            if (!isFloatCompatibleKind(value.kind)) {
                return std::unexpected{serializationError(
                    "Expected double archive value for field '" + std::string{path} + "'.",
                    {
                        {"operation", "deserialize"},
                        {"objectPath", path},
                        {"type", reflection::builtin::kDoubleName},
                        {"expected", "double"},
                        {"actual", archiveKindName(value.kind)},
                    })};
            }
            *static_cast<double*>(object) = value.kind == ArchiveValueKind::Integer
                                                ? static_cast<double>(value.integerValue)
                                                : value.floatValue;
            return {};
        }

        [[nodiscard]] VoidResult deserializeStringValue(const ArchiveValue& value, void* object,
                                                        std::string_view path) {
            if (value.kind != ArchiveValueKind::String) {
                return std::unexpected{serializationError(
                    "Expected string archive value for field '" + std::string{path} + "'.",
                    {
                        {"operation", "deserialize"},
                        {"objectPath", path},
                        {"type", reflection::builtin::kStringName},
                        {"expected", "string"},
                        {"actual", archiveKindName(value.kind)},
                    })};
            }
            *static_cast<std::string*>(object) = value.stringValue;
            return {};
        }

        [[nodiscard]] VoidResult deserializeValue(const reflection::TypeRegistry& registry,
                                                  reflection::TypeId type,
                                                  const ArchiveValue& value, void* object,
                                                  const SerializationPolicy& policy,
                                                  std::string_view path) {
            if (object == nullptr) {
                return std::unexpected{serializationError(
                    "Cannot deserialize null value.",
                    {
                        {"operation", "deserialize"},
                        {"objectPath", path},
                        {"expected", "non-null value"},
                        {"actual", "null"},
                    })};
            }
            if (type == reflection::builtin::boolTypeId()) {
                return deserializeBoolValue(value, object, path);
            }
            if (type == reflection::builtin::int32TypeId()) {
                return deserializeInt32Value(value, object, path);
            }
            if (type == reflection::builtin::uint32TypeId()) {
                return deserializeUInt32Value(value, object, path);
            }
            if (type == reflection::builtin::uint64TypeId()) {
                return deserializeUInt64Value(value, object, path);
            }
            if (type == reflection::builtin::floatTypeId()) {
                return deserializeFloatValue(value, object, path);
            }
            if (type == reflection::builtin::doubleTypeId()) {
                return deserializeDoubleValue(value, object, path);
            }
            if (type == reflection::builtin::stringTypeId()) {
                return deserializeStringValue(value, object, path);
            }

            const reflection::TypeInfo* typeInfo = registry.findType(type);
            if (typeInfo == nullptr) {
                return std::unexpected{serializationError(
                    "Cannot deserialize unknown reflected type id for field '" + std::string{path} +
                        "'.",
                    {
                        {"operation", "deserialize"},
                        {"objectPath", path},
                        {"typeId", std::to_string(type.value)},
                        {"expected", "registered reflected type"},
                        {"actual", "missing"},
                    })};
            }
            if (value.kind != ArchiveValueKind::Object) {
                return std::unexpected{serializationError(
                    "Expected object archive value for field '" + std::string{path} +
                        "' of type '" + typeInfo->name + "'.",
                    {
                        {"operation", "deserialize"},
                        {"objectPath", path},
                        {"type", typeInfo->name},
                        {"expected", "object archive"},
                        {"actual", archiveKindName(value.kind)},
                        {"version", std::to_string(typeInfo->version)},
                    })};
            }
            return deserializeStruct(registry, *typeInfo, value, object, policy, path);
        }

    } // namespace

    Result<ArchiveValue> serializeObject(const reflection::TypeRegistry& registry,
                                         reflection::TypeId type, const void* object,
                                         const SerializationPolicy& policy) {
        return serializeValue(registry, type, object, policy, {});
    }

    VoidResult deserializeObject(const reflection::TypeRegistry& registry, reflection::TypeId type,
                                 const ArchiveValue& value, void* object,
                                 const SerializationPolicy& policy) {
        return deserializeValue(registry, type, value, object, policy, {});
    }

} // namespace asharia::serialization
