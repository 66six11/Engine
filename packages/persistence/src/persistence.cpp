#include "asharia/persistence/persistence.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asharia/core/error.hpp"

namespace asharia::persistence {
    namespace {

        struct DiagnosticField {
            std::string key;
            std::string value;

            DiagnosticField(const char* fieldKey, std::string_view fieldValue)
                : key{fieldKey}, value{fieldValue} {}
        };

        [[nodiscard]] Error
        makePersistenceError(std::string message,
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
            return Error{ErrorDomain::Persistence, 0, std::move(message)};
        }

        [[nodiscard]] std::string appendFieldPath(std::string_view objectPath,
                                                  const schema::FieldSchema& field) {
            if (objectPath.empty()) {
                return field.key;
            }
            std::string path{objectPath};
            path += '.';
            path += field.key;
            return path;
        }

        [[nodiscard]] std::string appendMemberPath(std::string_view objectPath,
                                                   const archive::ArchiveMember& member) {
            if (objectPath.empty()) {
                return member.key;
            }
            std::string path{objectPath};
            path += '.';
            path += member.key;
            return path;
        }

        [[nodiscard]] std::string_view archiveKindName(archive::ArchiveValueKind kind) noexcept {
            switch (kind) {
            case archive::ArchiveValueKind::Null:
                return "null";
            case archive::ArchiveValueKind::Bool:
                return "bool";
            case archive::ArchiveValueKind::Integer:
                return "integer";
            case archive::ArchiveValueKind::Float:
                return "float";
            case archive::ArchiveValueKind::String:
                return "string";
            case archive::ArchiveValueKind::Array:
                return "array";
            case archive::ArchiveValueKind::Object:
                return "object";
            }
            return "unknown";
        }

        [[nodiscard]] std::string archiveActual(const archive::ArchiveValue* value) {
            if (value == nullptr) {
                return "missing";
            }
            if (value->kind == archive::ArchiveValueKind::Integer) {
                return "integer " + std::to_string(value->integerValue);
            }
            return std::string{archiveKindName(value->kind)};
        }

        [[nodiscard]] std::string_view unknownFieldPolicyName(UnknownFieldPolicy policy) noexcept {
            switch (policy) {
            case UnknownFieldPolicy::Error:
                return "error";
            case UnknownFieldPolicy::Drop:
                return "drop";
            case UnknownFieldPolicy::Preserve:
                return "preserve";
            }
            return "unknown";
        }

        [[nodiscard]] std::string_view schemaValueKindName(schema::ValueKind kind) noexcept {
            switch (kind) {
            case schema::ValueKind::Null:
                return "null";
            case schema::ValueKind::Bool:
                return "bool";
            case schema::ValueKind::Integer:
                return "integer";
            case schema::ValueKind::Float:
                return "float";
            case schema::ValueKind::String:
                return "string";
            case schema::ValueKind::Enum:
                return "enum";
            case schema::ValueKind::Array:
                return "array";
            case schema::ValueKind::Object:
                return "object";
            case schema::ValueKind::InlineStruct:
                return "inline struct";
            case schema::ValueKind::AssetReference:
                return "asset reference";
            case schema::ValueKind::EntityReference:
                return "entity reference";
            }
            return "unknown";
        }

        [[nodiscard]] VoidResult validateFrozen(const schema::SchemaRegistry& schemas,
                                                const cpp_binding::BindingRegistry& bindings,
                                                std::string_view operation) {
            if (!schemas.isFrozen()) {
                return std::unexpected{
                    makePersistenceError("Cannot persist with an unfrozen schema registry.",
                                         {{"operation", operation},
                                          {"expected", "frozen schema registry"},
                                          {"actual", "mutable schema registry"}})};
            }
            if (!bindings.isFrozen()) {
                return std::unexpected{
                    makePersistenceError("Cannot persist with an unfrozen C++ binding registry.",
                                         {{"operation", operation},
                                          {"expected", "frozen C++ binding registry"},
                                          {"actual", "mutable C++ binding registry"}})};
            }
            return {};
        }

        [[nodiscard]] Result<const schema::TypeSchema*>
        requireType(const schema::SchemaRegistry& schemas, const schema::TypeId& typeId,
                    std::string_view operation, std::string_view objectPath) {
            const schema::TypeSchema* type = schemas.findType(typeId);
            if (type == nullptr) {
                return std::unexpected{makePersistenceError("Persistence type is not registered.",
                                                            {{"operation", operation},
                                                             {"objectPath", objectPath},
                                                             {"type", typeId.stableName},
                                                             {"expected", "registered schema type"},
                                                             {"actual", "missing"}})};
            }
            return type;
        }

        [[nodiscard]] Error fieldError(std::string message, std::string_view operation,
                                       const schema::TypeSchema& type,
                                       const schema::FieldSchema* field,
                                       std::string_view objectPath, std::string_view expected,
                                       std::string_view actual) {
            return makePersistenceError(std::move(message),
                                        {{"operation", operation},
                                         {"objectPath", objectPath},
                                         {"type", type.id.stableName},
                                         {"field", field == nullptr ? std::string_view{"<null>"}
                                                                    : std::string_view{field->key}},
                                         {"expected", expected},
                                         {"actual", actual},
                                         {"version", std::to_string(type.version)}});
        }

        struct AlignedFieldStorageDeleter {
            std::size_t alignment{};

            void operator()(void* value) const noexcept {
                if (value != nullptr) {
                    ::operator delete(value, std::align_val_t{alignment});
                }
            }
        };

        class ScopedFieldValue {
        public:
            ScopedFieldValue() = default;

            ScopedFieldValue(const cpp_binding::FieldBinding& binding, void* value) noexcept
                : binding_{&binding},
                  storage_{value, AlignedFieldStorageDeleter{binding.alignment}} {}

            ScopedFieldValue(const ScopedFieldValue&) = delete;
            ScopedFieldValue& operator=(const ScopedFieldValue&) = delete;

            ScopedFieldValue(ScopedFieldValue&& other) noexcept
                : binding_{other.binding_}, storage_{std::move(other.storage_)},
                  constructed_{other.constructed_} {
                other.binding_ = nullptr;
                other.constructed_ = false;
            }

            ScopedFieldValue& operator=(ScopedFieldValue&& other) noexcept {
                if (this != &other) {
                    reset();
                    binding_ = other.binding_;
                    storage_ = std::move(other.storage_);
                    constructed_ = other.constructed_;
                    other.binding_ = nullptr;
                    other.constructed_ = false;
                }
                return *this;
            }

            ~ScopedFieldValue() {
                reset();
            }

            [[nodiscard]] void* get() noexcept {
                return storage_.get();
            }

            void markConstructed() noexcept {
                constructed_ = true;
            }

        private:
            void reset() noexcept {
                if (constructed_ && binding_ != nullptr && binding_->destroyValue) {
                    binding_->destroyValue(storage_.get());
                }
                binding_ = nullptr;
                constructed_ = false;
                storage_.reset();
            }

            const cpp_binding::FieldBinding* binding_{};
            std::unique_ptr<void, AlignedFieldStorageDeleter> storage_{
                nullptr, AlignedFieldStorageDeleter{}};
            bool constructed_{};
        };

        [[nodiscard]] constexpr bool isPowerOfTwo(std::size_t value) noexcept {
            return value != 0U && (value & (value - 1U)) == 0U;
        }

        [[nodiscard]] Result<ScopedFieldValue>
        makeTemporaryFieldValue(const schema::TypeSchema& type, const schema::FieldSchema& field,
                                const cpp_binding::FieldBinding& binding,
                                std::string_view objectPath, std::string_view operation) {
            if (!binding.constructValue || !binding.destroyValue || binding.size == 0U ||
                binding.alignment == 0U) {
                return std::unexpected{fieldError(
                    "C++ binding field has no temporary value constructor.", operation, type,
                    &field, objectPath, "construct/destroy value binding", "missing")};
            }
            if (!isPowerOfTwo(binding.alignment)) {
                return std::unexpected{
                    fieldError("C++ binding field temporary storage has invalid alignment.",
                               operation, type, &field, objectPath, "power-of-two alignment",
                               std::to_string(binding.alignment))};
            }

            void* value =
                ::operator new(binding.size, std::align_val_t{binding.alignment}, std::nothrow);
            if (value == nullptr) {
                return std::unexpected{fieldError(
                    "C++ binding field temporary storage allocation failed.", operation, type,
                    &field, objectPath, "allocated temporary field value", "allocation failure")};
            }

            ScopedFieldValue temporary{binding, value};
            auto constructed = binding.constructValue(temporary.get());
            if (!constructed) {
                return std::unexpected{std::move(constructed.error())};
            }
            temporary.markConstructed();
            return temporary;
        }

        [[nodiscard]] bool isType(const schema::TypeSchema& type, std::string_view name) {
            return type.id.stableName == name;
        }

        [[nodiscard]] Result<archive::ArchiveValue>
        saveValue(const schema::SchemaRegistry& schemas,
                  const cpp_binding::BindingRegistry& bindings, const schema::TypeSchema& type,
                  const void* object, bool includeTypeHeader, std::string_view objectPath);

        [[nodiscard]] VoidResult loadValue(const schema::SchemaRegistry& schemas,
                                           const cpp_binding::BindingRegistry& bindings,
                                           const schema::TypeSchema& type,
                                           const archive::ArchiveValue& value, void* object,
                                           bool expectTypeHeader, std::string_view objectPath,
                                           const PersistencePolicy& policy);

        [[nodiscard]] Result<archive::ArchiveValue> saveScalar(const schema::TypeSchema& type,
                                                               const void* object,
                                                               std::string_view objectPath) {
            if (object == nullptr) {
                return std::unexpected{makePersistenceError("Cannot save null scalar object.",
                                                            {{"operation", "save"},
                                                             {"objectPath", objectPath},
                                                             {"type", type.id.stableName},
                                                             {"expected", "object pointer"},
                                                             {"actual", "null"}})};
            }

            if (isType(type, schema::builtin::kBoolName)) {
                return archive::ArchiveValue::boolean(*static_cast<const bool*>(object));
            }
            if (isType(type, schema::builtin::kInt32Name)) {
                return archive::ArchiveValue::integer(*static_cast<const std::int32_t*>(object));
            }
            if (isType(type, schema::builtin::kUInt32Name)) {
                return archive::ArchiveValue::integer(*static_cast<const std::uint32_t*>(object));
            }
            if (isType(type, schema::builtin::kUInt64Name)) {
                const auto value = *static_cast<const std::uint64_t*>(object);
                if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                    return std::unexpected{
                        makePersistenceError("Archive integer cannot represent UInt64 value.",
                                             {{"operation", "save"},
                                              {"objectPath", objectPath},
                                              {"type", type.id.stableName},
                                              {"expected", "signed 64-bit range"},
                                              {"actual", "out of range"}})};
                }
                return archive::ArchiveValue::integer(static_cast<std::int64_t>(value));
            }
            if (isType(type, schema::builtin::kFloatName)) {
                return archive::ArchiveValue::floating(*static_cast<const float*>(object));
            }
            if (isType(type, schema::builtin::kDoubleName)) {
                return archive::ArchiveValue::floating(*static_cast<const double*>(object));
            }
            if (isType(type, schema::builtin::kStringName)) {
                return archive::ArchiveValue::string(*static_cast<const std::string*>(object));
            }

            return std::unexpected{makePersistenceError("Unsupported scalar schema type.",
                                                        {{"operation", "save"},
                                                         {"objectPath", objectPath},
                                                         {"type", type.id.stableName},
                                                         {"expected", "builtin scalar type"},
                                                         {"actual", "custom scalar"}})};
        }

        [[nodiscard]] Error scalarLoadTypeError(const schema::TypeSchema& type,
                                                const archive::ArchiveValue& value,
                                                std::string_view objectPath,
                                                std::string_view expected) {
            return makePersistenceError("Archive value has the wrong type.",
                                        {{"operation", "load"},
                                         {"objectPath", objectPath},
                                         {"type", type.id.stableName},
                                         {"expected", expected},
                                         {"actual", archiveActual(&value)}});
        }

        [[nodiscard]] VoidResult loadBoolScalar(const schema::TypeSchema& type,
                                                const archive::ArchiveValue& value, void* object,
                                                std::string_view objectPath) {
            if (value.kind != archive::ArchiveValueKind::Bool) {
                return std::unexpected{scalarLoadTypeError(type, value, objectPath, "bool")};
            }
            *static_cast<bool*>(object) = value.boolValue;
            return {};
        }

        [[nodiscard]] VoidResult loadInt32Scalar(const schema::TypeSchema& type,
                                                 const archive::ArchiveValue& value, void* object,
                                                 std::string_view objectPath) {
            if (value.kind != archive::ArchiveValueKind::Integer ||
                value.integerValue < std::numeric_limits<std::int32_t>::min() ||
                value.integerValue > std::numeric_limits<std::int32_t>::max()) {
                return std::unexpected{scalarLoadTypeError(type, value, objectPath, "int32")};
            }
            *static_cast<std::int32_t*>(object) = static_cast<std::int32_t>(value.integerValue);
            return {};
        }

        [[nodiscard]] VoidResult loadUInt32Scalar(const schema::TypeSchema& type,
                                                  const archive::ArchiveValue& value, void* object,
                                                  std::string_view objectPath) {
            if (value.kind != archive::ArchiveValueKind::Integer || value.integerValue < 0 ||
                value.integerValue > std::numeric_limits<std::uint32_t>::max()) {
                return std::unexpected{scalarLoadTypeError(type, value, objectPath, "uint32")};
            }
            *static_cast<std::uint32_t*>(object) = static_cast<std::uint32_t>(value.integerValue);
            return {};
        }

        [[nodiscard]] VoidResult loadUInt64Scalar(const schema::TypeSchema& type,
                                                  const archive::ArchiveValue& value, void* object,
                                                  std::string_view objectPath) {
            if (value.kind != archive::ArchiveValueKind::Integer || value.integerValue < 0) {
                return std::unexpected{scalarLoadTypeError(type, value, objectPath, "uint64")};
            }
            *static_cast<std::uint64_t*>(object) = static_cast<std::uint64_t>(value.integerValue);
            return {};
        }

        [[nodiscard]] VoidResult loadFloatScalar(const schema::TypeSchema& type,
                                                 const archive::ArchiveValue& value, void* object,
                                                 std::string_view objectPath) {
            if (value.kind != archive::ArchiveValueKind::Float &&
                value.kind != archive::ArchiveValueKind::Integer) {
                return std::unexpected{scalarLoadTypeError(type, value, objectPath, "float")};
            }
            *static_cast<float*>(object) =
                static_cast<float>(value.kind == archive::ArchiveValueKind::Float
                                       ? value.floatValue
                                       : static_cast<double>(value.integerValue));
            return {};
        }

        [[nodiscard]] VoidResult loadDoubleScalar(const schema::TypeSchema& type,
                                                  const archive::ArchiveValue& value, void* object,
                                                  std::string_view objectPath) {
            if (value.kind != archive::ArchiveValueKind::Float &&
                value.kind != archive::ArchiveValueKind::Integer) {
                return std::unexpected{scalarLoadTypeError(type, value, objectPath, "double")};
            }
            *static_cast<double*>(object) = value.kind == archive::ArchiveValueKind::Float
                                                ? value.floatValue
                                                : static_cast<double>(value.integerValue);
            return {};
        }

        [[nodiscard]] VoidResult loadStringScalar(const schema::TypeSchema& type,
                                                  const archive::ArchiveValue& value, void* object,
                                                  std::string_view objectPath) {
            if (value.kind != archive::ArchiveValueKind::String) {
                return std::unexpected{scalarLoadTypeError(type, value, objectPath, "string")};
            }
            *static_cast<std::string*>(object) = value.stringValue;
            return {};
        }

        [[nodiscard]] VoidResult loadScalar(const schema::TypeSchema& type,
                                            const archive::ArchiveValue& value, void* object,
                                            std::string_view objectPath) {
            if (object == nullptr) {
                return std::unexpected{makePersistenceError("Cannot load scalar into null object.",
                                                            {{"operation", "load"},
                                                             {"objectPath", objectPath},
                                                             {"type", type.id.stableName},
                                                             {"expected", "object pointer"},
                                                             {"actual", "null"}})};
            }

            if (isType(type, schema::builtin::kBoolName)) {
                return loadBoolScalar(type, value, object, objectPath);
            }
            if (isType(type, schema::builtin::kInt32Name)) {
                return loadInt32Scalar(type, value, object, objectPath);
            }
            if (isType(type, schema::builtin::kUInt32Name)) {
                return loadUInt32Scalar(type, value, object, objectPath);
            }
            if (isType(type, schema::builtin::kUInt64Name)) {
                return loadUInt64Scalar(type, value, object, objectPath);
            }
            if (isType(type, schema::builtin::kFloatName)) {
                return loadFloatScalar(type, value, object, objectPath);
            }
            if (isType(type, schema::builtin::kDoubleName)) {
                return loadDoubleScalar(type, value, object, objectPath);
            }
            if (isType(type, schema::builtin::kStringName)) {
                return loadStringScalar(type, value, object, objectPath);
            }

            return std::unexpected{makePersistenceError("Unsupported scalar schema type.",
                                                        {{"operation", "load"},
                                                         {"objectPath", objectPath},
                                                         {"type", type.id.stableName},
                                                         {"expected", "builtin scalar type"},
                                                         {"actual", "custom scalar"}})};
        }

        [[nodiscard]] bool isScalarType(const schema::TypeSchema& type) {
            return type.kind == schema::ValueKind::Bool ||
                   type.kind == schema::ValueKind::Integer ||
                   type.kind == schema::ValueKind::Float || type.kind == schema::ValueKind::String;
        }

        [[nodiscard]] bool isStructLikeType(const schema::TypeSchema& type) {
            return type.kind == schema::ValueKind::Object ||
                   type.kind == schema::ValueKind::InlineStruct;
        }

        [[nodiscard]] bool isSupportedPersistentFieldKind(schema::ValueKind kind) noexcept {
            switch (kind) {
            case schema::ValueKind::Bool:
            case schema::ValueKind::Integer:
            case schema::ValueKind::Float:
            case schema::ValueKind::String:
            case schema::ValueKind::Object:
            case schema::ValueKind::InlineStruct:
                return true;
            case schema::ValueKind::Null:
            case schema::ValueKind::Enum:
            case schema::ValueKind::Array:
            case schema::ValueKind::AssetReference:
            case schema::ValueKind::EntityReference:
                return false;
            }
            return false;
        }

        [[nodiscard]] VoidResult validatePersistentFieldKind(
            const schema::TypeSchema& ownerType, const schema::FieldSchema& field,
            const schema::TypeSchema& fieldType, std::string_view operation,
            std::string_view objectPath) {
            if (!isSupportedPersistentFieldKind(field.valueKind)) {
                return std::unexpected{
                    fieldError("Persistent schema field kind is not supported yet.", operation,
                               ownerType, &field, objectPath, "supported persistent field kind",
                               schemaValueKindName(field.valueKind))};
            }

            if (field.valueKind != fieldType.kind) {
                return std::unexpected{
                    fieldError("Persistent schema field kind does not match field type kind.",
                               operation, ownerType, &field, objectPath,
                               schemaValueKindName(fieldType.kind),
                               schemaValueKindName(field.valueKind))};
            }

            return {};
        }

        [[nodiscard]] bool fieldRequiresEnvelope(const schema::FieldSchema& field) noexcept {
            return field.valueKind == schema::ValueKind::Object;
        }

        [[nodiscard]] const archive::ArchiveValue*
        findMemberByKeyOrAlias(const archive::ArchiveValue& object,
                               const schema::FieldSchema& field) {
            if (const archive::ArchiveValue* value = object.findMemberValue(field.key);
                value != nullptr) {
                return value;
            }
            for (const std::string& alias : field.aliases) {
                if (const archive::ArchiveValue* value = object.findMemberValue(alias);
                    value != nullptr) {
                    return value;
                }
            }
            return nullptr;
        }

        [[nodiscard]] Result<archive::ArchiveValue>
        saveFieldValue(const schema::SchemaRegistry& schemas,
                       const cpp_binding::BindingRegistry& bindings,
                       const schema::TypeSchema& ownerType, const schema::FieldSchema& field,
                       const cpp_binding::FieldBinding& fieldBinding, const void* object,
                       std::string_view objectPath) {
            const schema::TypeSchema* fieldType = schemas.findType(field.valueType);
            if (fieldType == nullptr) {
                return std::unexpected{
                    fieldError("Persistent field references an unregistered schema type.", "save",
                               ownerType, &field, objectPath, "registered field type", "missing")};
            }
            if (auto validKind =
                    validatePersistentFieldKind(ownerType, field, *fieldType, "save", objectPath);
                !validKind) {
                return std::unexpected{std::move(validKind.error())};
            }

            if (fieldBinding.readAddress) {
                const void* fieldObject = fieldBinding.readAddress(object);
                if (fieldObject == nullptr) {
                    return std::unexpected{
                            fieldError("C++ binding field read address returned null.", "save",
                                       ownerType, &field, objectPath, "field object", "null")};
                }
                return saveValue(schemas, bindings, *fieldType, fieldObject,
                                 fieldRequiresEnvelope(field), objectPath);
            }

            if (fieldBinding.readValue) {
                auto temporary =
                    makeTemporaryFieldValue(ownerType, field, fieldBinding, objectPath, "save");
                if (!temporary) {
                    return std::unexpected{std::move(temporary.error())};
                }
                auto read = fieldBinding.readValue(object, temporary->get());
                if (!read) {
                    return std::unexpected{std::move(read.error())};
                }
                return saveValue(schemas, bindings, *fieldType, temporary->get(),
                                 fieldRequiresEnvelope(field), objectPath);
            }

            return std::unexpected{fieldError("Persistent field cannot be read.", "save", ownerType,
                                              &field, objectPath, "read binding", "missing")};
        }

        [[nodiscard]] Result<archive::ArchiveValue>
        saveValue(const schema::SchemaRegistry& schemas,
                  const cpp_binding::BindingRegistry& bindings, const schema::TypeSchema& type,
                  const void* object, bool includeTypeHeader, std::string_view objectPath) {
            if (isScalarType(type)) {
                return saveScalar(type, object, objectPath);
            }
            if (!isStructLikeType(type)) {
                return std::unexpected{
                    makePersistenceError("Persistence schema type kind is not supported yet.",
                                         {{"operation", "save"},
                                          {"objectPath", objectPath},
                                          {"type", type.id.stableName},
                                          {"expected", "scalar, inline struct, or object"},
                                          {"actual", schemaValueKindName(type.kind)}})};
            }

            const cpp_binding::CppTypeBinding* binding = bindings.findBinding(type.id);
            if (binding == nullptr) {
                return std::unexpected{makePersistenceError("Schema type has no C++ binding.",
                                                            {{"operation", "save"},
                                                             {"objectPath", objectPath},
                                                             {"type", type.id.stableName},
                                                             {"expected", "C++ type binding"},
                                                             {"actual", "missing"}})};
            }

            std::vector<archive::ArchiveMember> fields;
            for (const schema::FieldSchema* field :
                 schema::makePersistenceProjection(type).fields) {
                if (field == nullptr) {
                    continue;
                }
                const cpp_binding::FieldBinding* fieldBinding =
                    cpp_binding::findFieldBinding(*binding, field->id);
                if (fieldBinding == nullptr) {
                    return std::unexpected{fieldError(
                        "Persistent schema field has no C++ binding.", "save", type, field,
                        appendFieldPath(objectPath, *field), "C++ field binding", "missing")};
                }

                auto fieldValue = saveFieldValue(schemas, bindings, type, *field, *fieldBinding,
                                                 object, appendFieldPath(objectPath, *field));
                if (!fieldValue) {
                    return std::unexpected{std::move(fieldValue.error())};
                }
                fields.push_back(archive::ArchiveMember{
                    .key = field->key,
                    .value = std::move(*fieldValue),
                });
            }

            archive::ArchiveValue fieldsObject = archive::ArchiveValue::object(std::move(fields));
            if (!includeTypeHeader) {
                return fieldsObject;
            }

            return archive::ArchiveValue::object({
                archive::ArchiveMember{
                    .key = "type",
                    .value = archive::ArchiveValue::string(type.id.stableName),
                },
                archive::ArchiveMember{
                    .key = "version",
                    .value = archive::ArchiveValue::integer(type.version),
                },
                archive::ArchiveMember{
                    .key = "fields",
                    .value = std::move(fieldsObject),
                },
            });
        }

        [[nodiscard]] VoidResult handleUnknownFields(const schema::TypeSchema& type,
                                                     const archive::ArchiveValue& fieldsObject,
                                                     std::string_view objectPath,
                                                     const PersistencePolicy& policy) {
            for (const archive::ArchiveMember& member : fieldsObject.objectValue) {
                const schema::FieldSchema* field = schema::findFieldByKeyOrAlias(type, member.key);
                if (field != nullptr && field->metadata.persistence.stored) {
                    continue;
                }
                if (policy.unknownFields == UnknownFieldPolicy::Drop) {
                    continue;
                }
                if (policy.unknownFields == UnknownFieldPolicy::Preserve) {
                    return std::unexpected{makePersistenceError(
                        "Unknown field preserve policy is not implemented yet.",
                        {{"operation", "load"},
                         {"objectPath", appendMemberPath(objectPath, member)},
                         {"type", type.id.stableName},
                         {"field", member.key},
                         {"expected", "preserve policy support"},
                         {"actual", "unsupported"},
                         {"policy", unknownFieldPolicyName(policy.unknownFields)}})};
                }
                return std::unexpected{makePersistenceError(
                    "Archive object contains an unknown persistent field.",
                    {{"operation", "load"},
                     {"objectPath", appendMemberPath(objectPath, member)},
                     {"type", type.id.stableName},
                     {"field", member.key},
                     {"expected", "registered persistent schema field"},
                     {"actual", "unknown field"},
                     {"policy", unknownFieldPolicyName(policy.unknownFields)}})};
            }
            return {};
        }

        [[nodiscard]] VoidResult writeDefaultValue(const schema::SchemaRegistry& schemas,
                                                   const cpp_binding::BindingRegistry& bindings,
                                                   const schema::TypeSchema& ownerType,
                                                   const schema::FieldSchema& field,
                                                   const cpp_binding::FieldBinding& fieldBinding,
                                                   void* object, std::string_view objectPath) {
            const schema::TypeSchema* fieldType = schemas.findType(field.valueType);
            if (fieldType == nullptr) {
                return std::unexpected{
                    fieldError("Persistent field references an unregistered schema type.", "load",
                               ownerType, &field, objectPath, "registered field type", "missing")};
            }
            if (auto validKind =
                    validatePersistentFieldKind(ownerType, field, *fieldType, "load", objectPath);
                !validKind) {
                return validKind;
            }

            if (!fieldBinding.writeDefaultValue) {
                return std::unexpected{fieldError("Missing archive field has no C++ default value.",
                                                  "load", ownerType, &field, objectPath,
                                                  "C++ field default value", "missing")};
            }

            if (fieldBinding.writeAddress) {
                void* fieldObject = fieldBinding.writeAddress(object);
                if (fieldObject == nullptr) {
                    return std::unexpected{
                        fieldError("C++ binding field write address returned null.", "load",
                                   ownerType, &field, objectPath, "field object", "null")};
                }
                return fieldBinding.writeDefaultValue(fieldObject);
            }

            if (fieldBinding.writeValue) {
                auto temporary =
                    makeTemporaryFieldValue(ownerType, field, fieldBinding, objectPath, "load");
                if (!temporary) {
                    return std::unexpected{std::move(temporary.error())};
                }
                auto defaulted = fieldBinding.writeDefaultValue(temporary->get());
                if (!defaulted) {
                    return defaulted;
                }
                return fieldBinding.writeValue(object, temporary->get());
            }

            (void)bindings;
            (void)fieldType;
            return std::unexpected{fieldError("Persistent field cannot be written.", "load",
                                              ownerType, &field, objectPath, "write binding",
                                              "missing")};
        }

        [[nodiscard]] VoidResult loadFieldValue(const schema::SchemaRegistry& schemas,
                                                const cpp_binding::BindingRegistry& bindings,
                                                const schema::TypeSchema& ownerType,
                                                const schema::FieldSchema& field,
                                                const cpp_binding::FieldBinding& fieldBinding,
                                                const archive::ArchiveValue& archiveValue,
                                                void* object, std::string_view objectPath,
                                                const PersistencePolicy& policy) {
            const schema::TypeSchema* fieldType = schemas.findType(field.valueType);
            if (fieldType == nullptr) {
                return std::unexpected{
                    fieldError("Persistent field references an unregistered schema type.", "load",
                               ownerType, &field, objectPath, "registered field type", "missing")};
            }
            if (auto validKind =
                    validatePersistentFieldKind(ownerType, field, *fieldType, "load", objectPath);
                !validKind) {
                return validKind;
            }

            if (fieldBinding.writeAddress) {
                void* fieldObject = fieldBinding.writeAddress(object);
                if (fieldObject == nullptr) {
                    return std::unexpected{
                        fieldError("C++ binding field write address returned null.", "load",
                                   ownerType, &field, objectPath, "field object", "null")};
                }
                return loadValue(schemas, bindings, *fieldType, archiveValue, fieldObject,
                                 fieldRequiresEnvelope(field), objectPath, policy);
            }

            if (fieldBinding.writeValue) {
                auto temporary =
                    makeTemporaryFieldValue(ownerType, field, fieldBinding, objectPath, "load");
                if (!temporary) {
                    return std::unexpected{std::move(temporary.error())};
                }
                auto loaded = loadValue(schemas, bindings, *fieldType, archiveValue,
                                        temporary->get(), fieldRequiresEnvelope(field),
                                        objectPath, policy);
                if (!loaded) {
                    return loaded;
                }
                return fieldBinding.writeValue(object, temporary->get());
            }

            return std::unexpected{fieldError("Persistent field cannot be written.", "load",
                                              ownerType, &field, objectPath, "write binding",
                                              "missing")};
        }

        [[nodiscard]] Result<const archive::ArchiveValue*>
        normalizeInputObject(const schema::TypeSchema& type, const archive::ArchiveValue& value,
                             bool expectTypeHeader, std::string_view objectPath,
                             const PersistencePolicy& policy) {
            if (!expectTypeHeader) {
                if (value.kind != archive::ArchiveValueKind::Object) {
                    return std::unexpected{
                        makePersistenceError("Archive value has the wrong type.",
                                             {{"operation", "load"},
                                              {"objectPath", objectPath},
                                              {"type", type.id.stableName},
                                              {"expected", "object"},
                                              {"actual", archiveKindName(value.kind)}})};
                }
                return &value;
            }

            if (value.kind != archive::ArchiveValueKind::Object) {
                return std::unexpected{
                    makePersistenceError("Archive object envelope has the wrong type.",
                                         {{"operation", "load"},
                                          {"objectPath", objectPath},
                                          {"type", type.id.stableName},
                                          {"expected", "object envelope"},
                                          {"actual", archiveKindName(value.kind)}})};
            }

            const archive::ArchiveValue* archiveType = value.findMemberValue("type");
            const archive::ArchiveValue* archiveVersion = value.findMemberValue("version");
            const archive::ArchiveValue* fields = value.findMemberValue("fields");
            if (archiveType == nullptr || archiveType->kind != archive::ArchiveValueKind::String ||
                archiveType->stringValue != type.id.stableName) {
                return std::unexpected{makePersistenceError(
                    "Archive object envelope has the wrong type id.",
                    {{"operation", "load"},
                     {"objectPath", objectPath},
                     {"type", type.id.stableName},
                     {"field", "type"},
                     {"expected", type.id.stableName},
                     {"actual", archiveType == nullptr ? std::string{"missing"}
                                                       : archiveType->stringValue}})};
            }
            if (archiveVersion == nullptr ||
                archiveVersion->kind != archive::ArchiveValueKind::Integer) {
                return std::unexpected{
                    makePersistenceError("Archive object envelope has no version.",
                                         {{"operation", "load"},
                                          {"objectPath", objectPath},
                                          {"type", type.id.stableName},
                                          {"field", "version"},
                                          {"expected", "integer version"},
                                          {"actual", archiveActual(archiveVersion)}})};
            }
            if (archiveVersion->integerValue <= 0 ||
                archiveVersion->integerValue > std::numeric_limits<std::uint32_t>::max()) {
                return std::unexpected{makePersistenceError(
                    "Archive object envelope version is outside the supported range.",
                    {{"operation", "load"},
                     {"objectPath", objectPath},
                     {"type", type.id.stableName},
                     {"field", "version"},
                     {"expected", "uint32 version"},
                     {"actual", archiveActual(archiveVersion)}})};
            }

            const auto sourceVersion = static_cast<std::uint32_t>(archiveVersion->integerValue);
            if (sourceVersion != type.version) {
                if (policy.migrations == nullptr) {
                    return std::unexpected{
                        makePersistenceError("Archive object requires migration.",
                                             {{"operation", "load"},
                                              {"objectPath", objectPath},
                                              {"type", type.id.stableName},
                                              {"field", "version"},
                                              {"expected", "migration policy"},
                                              {"actual", "no migration registry"},
                                              {"version", std::to_string(sourceVersion)}})};
                }
                return nullptr;
            }

            if (fields == nullptr || fields->kind != archive::ArchiveValueKind::Object) {
                return std::unexpected{
                    makePersistenceError("Archive object envelope has no fields object.",
                                         {{"operation", "load"},
                                          {"objectPath", objectPath},
                                          {"type", type.id.stableName},
                                          {"field", "fields"},
                                          {"expected", "object"},
                                          {"actual", archiveActual(fields)}})};
            }
            return fields;
        }

        [[nodiscard]] VoidResult loadStructValue(const schema::SchemaRegistry& schemas,
                                                 const cpp_binding::BindingRegistry& bindings,
                                                 const schema::TypeSchema& type,
                                                 const archive::ArchiveValue& fieldsObject,
                                                 void* object, std::string_view objectPath,
                                                 const PersistencePolicy& policy) {
            if (fieldsObject.kind != archive::ArchiveValueKind::Object) {
                return std::unexpected{
                    makePersistenceError("Archive value has the wrong type.",
                                         {{"operation", "load"},
                                          {"objectPath", objectPath},
                                          {"type", type.id.stableName},
                                          {"expected", "object"},
                                          {"actual", archiveKindName(fieldsObject.kind)}})};
            }

            const cpp_binding::CppTypeBinding* binding = bindings.findBinding(type.id);
            if (binding == nullptr) {
                return std::unexpected{makePersistenceError("Schema type has no C++ binding.",
                                                            {{"operation", "load"},
                                                             {"objectPath", objectPath},
                                                             {"type", type.id.stableName},
                                                             {"expected", "C++ type binding"},
                                                             {"actual", "missing"}})};
            }

            if (auto unknown = handleUnknownFields(type, fieldsObject, objectPath, policy);
                !unknown) {
                return unknown;
            }

            for (const schema::FieldSchema* field :
                 schema::makePersistenceProjection(type).fields) {
                if (field == nullptr) {
                    continue;
                }
                const cpp_binding::FieldBinding* fieldBinding =
                    cpp_binding::findFieldBinding(*binding, field->id);
                if (fieldBinding == nullptr) {
                    return std::unexpected{fieldError(
                        "Persistent schema field has no C++ binding.", "load", type, field,
                        appendFieldPath(objectPath, *field), "C++ field binding", "missing")};
                }

                const std::string fieldPath = appendFieldPath(objectPath, *field);
                const archive::ArchiveValue* archiveField =
                    findMemberByKeyOrAlias(fieldsObject, *field);
                if (archiveField == nullptr) {
                    if (policy.missingFields == MissingFieldPolicy::KeepConstructedValue) {
                        continue;
                    }
                    if (policy.missingFields == MissingFieldPolicy::Error) {
                        return std::unexpected{
                            fieldError("Archive object is missing a persistent field.", "load",
                                       type, field, fieldPath, "archive field", "missing")};
                    }
                    auto defaulted = writeDefaultValue(schemas, bindings, type, *field,
                                                       *fieldBinding, object, fieldPath);
                    if (!defaulted) {
                        return defaulted;
                    }
                    continue;
                }

                auto loaded = loadFieldValue(schemas, bindings, type, *field, *fieldBinding,
                                             *archiveField, object, fieldPath, policy);
                if (!loaded) {
                    return loaded;
                }
            }
            return {};
        }

        [[nodiscard]] VoidResult loadValue(const schema::SchemaRegistry& schemas,
                                           const cpp_binding::BindingRegistry& bindings,
                                           const schema::TypeSchema& type,
                                           const archive::ArchiveValue& value, void* object,
                                           bool expectTypeHeader, std::string_view objectPath,
                                           const PersistencePolicy& policy) {
            if (isScalarType(type)) {
                return loadScalar(type, value, object, objectPath);
            }
            if (!isStructLikeType(type)) {
                return std::unexpected{
                    makePersistenceError("Persistence schema type kind is not supported yet.",
                                         {{"operation", "load"},
                                          {"objectPath", objectPath},
                                          {"type", type.id.stableName},
                                          {"expected", "scalar, inline struct, or object"},
                                          {"actual", schemaValueKindName(type.kind)}})};
            }

            auto normalized =
                normalizeInputObject(type, value, expectTypeHeader, objectPath, policy);
            if (!normalized) {
                return std::unexpected{std::move(normalized.error())};
            }
            if (*normalized == nullptr) {
                const archive::ArchiveValue* versionValue = value.findMemberValue("version");
                const auto sourceVersion = static_cast<std::uint32_t>(
                    versionValue == nullptr ? 0 : versionValue->integerValue);
                auto migrated = policy.migrations->migrateObject(
                    type.id, type.canonicalName, objectPath, policy.archivePath,
                    policy.migrationScenario, sourceVersion, type.version, value);
                if (!migrated) {
                    return std::unexpected{std::move(migrated.error())};
                }
                return loadValue(schemas, bindings, type, *migrated, object, expectTypeHeader,
                                 objectPath, policy);
            }
            return loadStructValue(schemas, bindings, type, **normalized, object, objectPath,
                                   policy);
        }

    } // namespace

    Error persistenceError(std::string message) {
        return Error{ErrorDomain::Persistence, 0, std::move(message)};
    }

    Result<archive::ArchiveValue> saveObject(const schema::SchemaRegistry& schemas,
                                             const cpp_binding::BindingRegistry& bindings,
                                             const schema::TypeId& type, const void* object,
                                             const PersistencePolicy& policy) {
        if (auto frozen = validateFrozen(schemas, bindings, "save"); !frozen) {
            return std::unexpected{std::move(frozen.error())};
        }
        if (object == nullptr) {
            return std::unexpected{
                makePersistenceError("Cannot save a null object.", {{"operation", "save"},
                                                                    {"type", type.stableName},
                                                                    {"expected", "object pointer"},
                                                                    {"actual", "null"}})};
        }

        auto typeSchema = requireType(schemas, type, "save", type.stableName);
        if (!typeSchema) {
            return std::unexpected{std::move(typeSchema.error())};
        }
        return saveValue(schemas, bindings, **typeSchema, object, policy.includeTypeHeader,
                         (*typeSchema)->id.stableName);
    }

    VoidResult loadObject(const schema::SchemaRegistry& schemas,
                          const cpp_binding::BindingRegistry& bindings, const schema::TypeId& type,
                          const archive::ArchiveValue& value, void* object,
                          const PersistencePolicy& policy) {
        if (auto frozen = validateFrozen(schemas, bindings, "load"); !frozen) {
            return frozen;
        }
        if (object == nullptr) {
            return std::unexpected{makePersistenceError("Cannot load into a null object.",
                                                        {{"operation", "load"},
                                                         {"type", type.stableName},
                                                         {"expected", "object pointer"},
                                                         {"actual", "null"}})};
        }

        auto typeSchema = requireType(schemas, type, "load", type.stableName);
        if (!typeSchema) {
            return std::unexpected{std::move(typeSchema.error())};
        }
        return loadValue(schemas, bindings, **typeSchema, value, object, policy.includeTypeHeader,
                         (*typeSchema)->id.stableName, policy);
    }

} // namespace asharia::persistence
