#include "asharia/schema/schema_document.hpp"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asharia/core/error.hpp"
#include "asharia/core/file_io.hpp"
#include "asharia/schema/ids.hpp"

namespace asharia::schema {
    namespace {

        using OrderedJson = nlohmann::ordered_json;

        struct DiagnosticField {
            std::string key;
            std::string value;

            DiagnosticField(const char* fieldKey, std::string_view fieldValue)
                : key{fieldKey}, value{fieldValue} {}
        };

        struct DuplicateKeyError final : std::runtime_error {
            explicit DuplicateKeyError(const std::string& key)
                : std::runtime_error{"JSON object contains duplicate key: " + key} {}
        };

        [[nodiscard]] Error
        makeSchemaDocumentError(std::string message,
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

        [[nodiscard]] bool containsAllowedKey(std::initializer_list<std::string_view> allowed,
                                              std::string_view key) {
            return std::ranges::find(allowed, key) != allowed.end();
        }

        [[nodiscard]] VoidResult
        validateObjectKeys(const OrderedJson& value, std::string_view context,
                           std::initializer_list<std::string_view> allowed) {
            if (!value.is_object()) {
                return std::unexpected{makeSchemaDocumentError(
                    "Schema document section must be an object.",
                    {{"section", context}, {"expected", "object"}, {"actual", value.type_name()}})};
            }

            for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
                if (!containsAllowedKey(allowed, iterator.key())) {
                    return std::unexpected{
                        makeSchemaDocumentError("Schema document contains an unknown member.",
                                                {{"section", context},
                                                 {"field", iterator.key()},
                                                 {"expected", "known schema member"},
                                                 {"actual", "unknown"}})};
                }
            }
            return {};
        }

        [[nodiscard]] const OrderedJson* findMember(const OrderedJson& object,
                                                    std::string_view key) {
            const auto found = object.find(std::string{key});
            return found == object.end() ? nullptr : &found.value();
        }

        [[nodiscard]] Result<const OrderedJson*>
        requireMember(const OrderedJson& object, std::string_view key, std::string_view context) {
            const OrderedJson* member = findMember(object, key);
            if (member == nullptr) {
                return std::unexpected{makeSchemaDocumentError(
                    "Schema document is missing a required member.", {{"section", context},
                                                                      {"field", key},
                                                                      {"expected", "present"},
                                                                      {"actual", "missing"}})};
            }
            return member;
        }

        [[nodiscard]] Result<std::string> readStringValue(const OrderedJson& value,
                                                          std::string_view context) {
            if (!value.is_string()) {
                return std::unexpected{makeSchemaDocumentError(
                    "Schema document member must be a string.",
                    {{"section", context}, {"expected", "string"}, {"actual", value.type_name()}})};
            }
            return value.get<std::string>();
        }

        [[nodiscard]] Result<bool> readBoolValue(const OrderedJson& value,
                                                 std::string_view context) {
            if (!value.is_boolean()) {
                return std::unexpected{makeSchemaDocumentError(
                    "Schema document member must be a bool.",
                    {{"section", context}, {"expected", "bool"}, {"actual", value.type_name()}})};
            }
            return value.get<bool>();
        }

        [[nodiscard]] Result<std::uint32_t> readUint32Value(const OrderedJson& value,
                                                            std::string_view context) {
            std::uint64_t parsed = 0;
            if (value.is_number_unsigned()) {
                parsed = value.get<std::uint64_t>();
            } else if (value.is_number_integer()) {
                const auto signedValue = value.get<std::int64_t>();
                if (signedValue < 0) {
                    return std::unexpected{
                        makeSchemaDocumentError("Schema document member must be non-negative.",
                                                {{"section", context},
                                                 {"expected", "uint32"},
                                                 {"actual", std::to_string(signedValue)}})};
                }
                parsed = static_cast<std::uint64_t>(signedValue);
            } else {
                return std::unexpected{makeSchemaDocumentError(
                    "Schema document member must be an integer.",
                    {{"section", context}, {"expected", "uint32"}, {"actual", value.type_name()}})};
            }

            if (parsed > std::numeric_limits<std::uint32_t>::max()) {
                return std::unexpected{
                    makeSchemaDocumentError("Schema document integer is out of range.",
                                            {{"section", context},
                                             {"expected", "uint32"},
                                             {"actual", std::to_string(parsed)}})};
            }
            return static_cast<std::uint32_t>(parsed);
        }

        [[nodiscard]] Result<double> readDoubleValue(const OrderedJson& value,
                                                     std::string_view context) {
            if (!value.is_number()) {
                return std::unexpected{makeSchemaDocumentError(
                    "Schema document member must be numeric.",
                    {{"section", context}, {"expected", "number"}, {"actual", value.type_name()}})};
            }
            return value.get<double>();
        }

        [[nodiscard]] Result<std::string> readStringMember(const OrderedJson& object,
                                                           std::string_view key,
                                                           std::string_view context) {
            auto member = requireMember(object, key, context);
            if (!member) {
                return std::unexpected{member.error()};
            }
            return readStringValue(**member, context);
        }

        [[nodiscard]] Result<std::uint32_t> readUint32Member(const OrderedJson& object,
                                                             std::string_view key,
                                                             std::string_view context) {
            auto member = requireMember(object, key, context);
            if (!member) {
                return std::unexpected{member.error()};
            }
            return readUint32Value(**member, context);
        }

        [[nodiscard]] VoidResult readOptionalBoolMember(const OrderedJson& object,
                                                        std::string_view key, bool& output,
                                                        std::string_view context) {
            const OrderedJson* member = findMember(object, key);
            if (member == nullptr) {
                return {};
            }
            auto value = readBoolValue(*member, context);
            if (!value) {
                return std::unexpected{value.error()};
            }
            output = *value;
            return {};
        }

        [[nodiscard]] VoidResult readOptionalUint32Member(const OrderedJson& object,
                                                          std::string_view key,
                                                          std::uint32_t& output,
                                                          std::string_view context) {
            const OrderedJson* member = findMember(object, key);
            if (member == nullptr) {
                return {};
            }
            auto value = readUint32Value(*member, context);
            if (!value) {
                return std::unexpected{value.error()};
            }
            output = *value;
            return {};
        }

        [[nodiscard]] VoidResult readOptionalDoubleMember(const OrderedJson& object,
                                                          std::string_view key, double& output,
                                                          std::string_view context) {
            const OrderedJson* member = findMember(object, key);
            if (member == nullptr) {
                return {};
            }
            auto value = readDoubleValue(*member, context);
            if (!value) {
                return std::unexpected{value.error()};
            }
            output = *value;
            return {};
        }

        [[nodiscard]] VoidResult readOptionalStringMember(const OrderedJson& object,
                                                          std::string_view key, std::string& output,
                                                          std::string_view context) {
            const OrderedJson* member = findMember(object, key);
            if (member == nullptr) {
                return {};
            }
            auto value = readStringValue(*member, context);
            if (!value) {
                return std::unexpected{value.error()};
            }
            output = std::move(*value);
            return {};
        }

        [[nodiscard]] Result<ValueKind> readValueKindString(std::string_view name,
                                                            std::string_view context) {
            if (name == "Null") {
                return ValueKind::Null;
            }
            if (name == "Bool") {
                return ValueKind::Bool;
            }
            if (name == "Integer") {
                return ValueKind::Integer;
            }
            if (name == "Float") {
                return ValueKind::Float;
            }
            if (name == "String") {
                return ValueKind::String;
            }
            if (name == "Enum") {
                return ValueKind::Enum;
            }
            if (name == "Array") {
                return ValueKind::Array;
            }
            if (name == "Object") {
                return ValueKind::Object;
            }
            if (name == "InlineStruct") {
                return ValueKind::InlineStruct;
            }
            if (name == "AssetReference") {
                return ValueKind::AssetReference;
            }
            if (name == "EntityReference") {
                return ValueKind::EntityReference;
            }
            return std::unexpected{makeSchemaDocumentError(
                "Unknown schema value kind.",
                {{"section", context}, {"expected", "known ValueKind name"}, {"actual", name}})};
        }

        [[nodiscard]] Result<ValueKind> readValueKindMember(const OrderedJson& object,
                                                            std::string_view key,
                                                            std::string_view context) {
            auto text = readStringMember(object, key, context);
            if (!text) {
                return std::unexpected{text.error()};
            }
            return readValueKindString(*text, context);
        }

        [[nodiscard]] VoidResult readPersistenceSpec(const OrderedJson& object,
                                                     PersistenceSpec& output,
                                                     std::string_view context) {
            if (auto valid = validateObjectKeys(
                    object, context,
                    {"stored", "required", "hasDefault", "sinceVersion", "deprecatedSince"});
                !valid) {
                return valid;
            }
            if (auto read = readOptionalBoolMember(object, "stored", output.stored, context);
                !read) {
                return read;
            }
            if (auto read = readOptionalBoolMember(object, "required", output.required, context);
                !read) {
                return read;
            }
            if (auto read =
                    readOptionalBoolMember(object, "hasDefault", output.hasDefault, context);
                !read) {
                return read;
            }
            if (auto read =
                    readOptionalUint32Member(object, "sinceVersion", output.sinceVersion, context);
                !read) {
                return read;
            }
            return readOptionalUint32Member(object, "deprecatedSince", output.deprecatedSince,
                                            context);
        }

        [[nodiscard]] VoidResult readEditorSpec(const OrderedJson& object, EditorSpec& output,
                                                std::string_view context) {
            if (auto valid = validateObjectKeys(object, context,
                                                {"visible", "readOnly", "displayName", "category",
                                                 "tooltip", "readOnlyReason"});
                !valid) {
                return valid;
            }
            if (auto read = readOptionalBoolMember(object, "visible", output.visible, context);
                !read) {
                return read;
            }
            if (auto read = readOptionalBoolMember(object, "readOnly", output.readOnly, context);
                !read) {
                return read;
            }
            if (auto read =
                    readOptionalStringMember(object, "displayName", output.displayName, context);
                !read) {
                return read;
            }
            if (auto read = readOptionalStringMember(object, "category", output.category, context);
                !read) {
                return read;
            }
            if (auto read = readOptionalStringMember(object, "tooltip", output.tooltip, context);
                !read) {
                return read;
            }
            return readOptionalStringMember(object, "readOnlyReason", output.readOnlyReason,
                                            context);
        }

        [[nodiscard]] VoidResult readScriptSpec(const OrderedJson& object, ScriptSpec& output,
                                                std::string_view context) {
            if (auto valid = validateObjectKeys(
                    object, context,
                    {"visible", "read", "write", "context", "threadAffinity", "lifetime"});
                !valid) {
                return valid;
            }
            if (auto read = readOptionalBoolMember(object, "visible", output.visible, context);
                !read) {
                return read;
            }
            if (auto read = readOptionalBoolMember(object, "read", output.read, context); !read) {
                return read;
            }
            if (auto read = readOptionalBoolMember(object, "write", output.write, context); !read) {
                return read;
            }
            if (auto read = readOptionalStringMember(object, "context", output.context, context);
                !read) {
                return read;
            }
            if (auto read = readOptionalStringMember(object, "threadAffinity",
                                                     output.threadAffinity, context);
                !read) {
                return read;
            }
            return readOptionalStringMember(object, "lifetime", output.lifetime, context);
        }

        [[nodiscard]] VoidResult readNumericSpec(const OrderedJson& object, NumericSpec& output,
                                                 std::string_view context) {
            if (auto valid = validateObjectKeys(object, context,
                                                {"hasMin", "hasMax", "min", "max", "step", "unit"});
                !valid) {
                return valid;
            }
            if (auto read = readOptionalBoolMember(object, "hasMin", output.hasMin, context);
                !read) {
                return read;
            }
            if (auto read = readOptionalBoolMember(object, "hasMax", output.hasMax, context);
                !read) {
                return read;
            }
            if (auto read = readOptionalDoubleMember(object, "min", output.min, context); !read) {
                return read;
            }
            if (auto read = readOptionalDoubleMember(object, "max", output.max, context); !read) {
                return read;
            }
            if (auto read = readOptionalDoubleMember(object, "step", output.step, context); !read) {
                return read;
            }
            return readOptionalStringMember(object, "unit", output.unit, context);
        }

        [[nodiscard]] Result<TypedMetadata> readMetadata(const OrderedJson& object,
                                                         std::string_view context) {
            if (auto valid = validateObjectKeys(object, context,
                                                {"persistence", "editor", "script", "numeric"});
                !valid) {
                return std::unexpected{valid.error()};
            }

            TypedMetadata metadata;
            if (const OrderedJson* persistence = findMember(object, "persistence");
                persistence != nullptr) {
                if (auto read = readPersistenceSpec(*persistence, metadata.persistence, context);
                    !read) {
                    return std::unexpected{read.error()};
                }
            }
            if (const OrderedJson* editor = findMember(object, "editor"); editor != nullptr) {
                if (auto read = readEditorSpec(*editor, metadata.editor, context); !read) {
                    return std::unexpected{read.error()};
                }
            }
            if (const OrderedJson* script = findMember(object, "script"); script != nullptr) {
                if (auto read = readScriptSpec(*script, metadata.script, context); !read) {
                    return std::unexpected{read.error()};
                }
            }
            if (const OrderedJson* numeric = findMember(object, "numeric"); numeric != nullptr) {
                if (auto read = readNumericSpec(*numeric, metadata.numeric, context); !read) {
                    return std::unexpected{read.error()};
                }
            }
            return metadata;
        }

        [[nodiscard]] Result<std::vector<std::string>> readAliases(const OrderedJson& object,
                                                                   std::string_view context) {
            std::vector<std::string> aliases;
            const OrderedJson* member = findMember(object, "aliases");
            if (member == nullptr) {
                return aliases;
            }
            if (!member->is_array()) {
                return std::unexpected{makeSchemaDocumentError(
                    "Schema field aliases must be an array.", {{"section", context},
                                                               {"expected", "array"},
                                                               {"actual", member->type_name()}})};
            }
            aliases.reserve(member->size());
            for (const OrderedJson& aliasValue : *member) {
                auto alias = readStringValue(aliasValue, context);
                if (!alias) {
                    return std::unexpected{alias.error()};
                }
                aliases.push_back(std::move(*alias));
            }
            return aliases;
        }

        [[nodiscard]] Result<std::vector<FieldId>> readReservedFieldIds(const OrderedJson& object,
                                                                        std::string_view context) {
            std::vector<FieldId> reserved;
            const OrderedJson* member = findMember(object, "reservedFieldIds");
            if (member == nullptr) {
                return reserved;
            }
            if (!member->is_array()) {
                return std::unexpected{
                    makeSchemaDocumentError("Schema reserved field ids must be an array.",
                                            {{"section", context},
                                             {"expected", "array"},
                                             {"actual", member->type_name()}})};
            }
            reserved.reserve(member->size());
            for (const OrderedJson& reservedValue : *member) {
                auto value = readUint32Value(reservedValue, context);
                if (!value) {
                    return std::unexpected{value.error()};
                }
                reserved.push_back(makeFieldId(*value));
            }
            return reserved;
        }

        [[nodiscard]] Result<FieldSchema> readField(const OrderedJson& object,
                                                    std::string_view typeName, std::size_t index) {
            const std::string context =
                std::string{typeName} + ".fields[" + std::to_string(index) + "]";
            if (auto valid = validateObjectKeys(
                    object, context,
                    {"id", "key", "valueType", "valueKind", "aliases", "metadata"});
                !valid) {
                return std::unexpected{valid.error()};
            }

            auto fieldIdValue = readUint32Member(object, "id", context);
            if (!fieldIdValue) {
                return std::unexpected{fieldIdValue.error()};
            }
            auto key = readStringMember(object, "key", context);
            if (!key) {
                return std::unexpected{key.error()};
            }
            auto valueType = readStringMember(object, "valueType", context);
            if (!valueType) {
                return std::unexpected{valueType.error()};
            }
            auto valueKind = readValueKindMember(object, "valueKind", context);
            if (!valueKind) {
                return std::unexpected{valueKind.error()};
            }
            auto aliases = readAliases(object, context);
            if (!aliases) {
                return std::unexpected{aliases.error()};
            }

            TypedMetadata metadata;
            if (const OrderedJson* metadataValue = findMember(object, "metadata");
                metadataValue != nullptr) {
                auto parsed = readMetadata(*metadataValue, context);
                if (!parsed) {
                    return std::unexpected{parsed.error()};
                }
                metadata = std::move(*parsed);
            }

            return FieldSchema{
                .id = makeFieldId(*fieldIdValue),
                .key = std::move(*key),
                .valueType = makeTypeId(*valueType),
                .valueKind = *valueKind,
                .aliases = std::move(*aliases),
                .metadata = std::move(metadata),
            };
        }

        [[nodiscard]] Result<std::vector<FieldSchema>> readFields(const OrderedJson& object,
                                                                  std::string_view typeName) {
            std::vector<FieldSchema> fields;
            const OrderedJson* member = findMember(object, "fields");
            if (member == nullptr) {
                return fields;
            }
            if (!member->is_array()) {
                return std::unexpected{makeSchemaDocumentError(
                    "Schema type fields must be an array.", {{"section", typeName},
                                                             {"expected", "array"},
                                                             {"actual", member->type_name()}})};
            }
            fields.reserve(member->size());
            for (std::size_t index = 0; index < member->size(); ++index) {
                auto field = readField((*member)[index], typeName, index);
                if (!field) {
                    return std::unexpected{field.error()};
                }
                fields.push_back(std::move(*field));
            }
            return fields;
        }

        [[nodiscard]] Result<TypeSchema> readType(const OrderedJson& object, std::size_t index) {
            const std::string context = "types[" + std::to_string(index) + "]";
            if (auto valid = validateObjectKeys(object, context,
                                                {"stableName", "canonicalName", "version", "kind",
                                                 "fields", "reservedFieldIds", "metadata"});
                !valid) {
                return std::unexpected{valid.error()};
            }

            auto stableName = readStringMember(object, "stableName", context);
            if (!stableName) {
                return std::unexpected{stableName.error()};
            }
            auto canonicalName = readStringMember(object, "canonicalName", context);
            if (!canonicalName) {
                return std::unexpected{canonicalName.error()};
            }
            auto version = readUint32Member(object, "version", context);
            if (!version) {
                return std::unexpected{version.error()};
            }
            auto kind = readValueKindMember(object, "kind", context);
            if (!kind) {
                return std::unexpected{kind.error()};
            }
            auto fields = readFields(object, *stableName);
            if (!fields) {
                return std::unexpected{fields.error()};
            }
            auto reservedFieldIds = readReservedFieldIds(object, context);
            if (!reservedFieldIds) {
                return std::unexpected{reservedFieldIds.error()};
            }

            TypedMetadata metadata;
            if (const OrderedJson* metadataValue = findMember(object, "metadata");
                metadataValue != nullptr) {
                auto parsed = readMetadata(*metadataValue, context);
                if (!parsed) {
                    return std::unexpected{parsed.error()};
                }
                metadata = std::move(*parsed);
            }

            return TypeSchema{
                .id = makeTypeId(*stableName),
                .canonicalName = std::move(*canonicalName),
                .version = *version,
                .kind = *kind,
                .fields = std::move(*fields),
                .reservedFieldIds = std::move(*reservedFieldIds),
                .metadata = std::move(metadata),
            };
        }

        [[nodiscard]] OrderedJson parseStrictJson(std::string_view text) {
            std::vector<std::vector<std::string>> objectKeyStack;
            const OrderedJson::parser_callback_t rejectDuplicateKeys =
                [&objectKeyStack](int, OrderedJson::parse_event_t event,
                                  OrderedJson& parsed) -> bool {
                switch (event) {
                case OrderedJson::parse_event_t::object_start:
                    objectKeyStack.emplace_back();
                    break;
                case OrderedJson::parse_event_t::object_end:
                    if (!objectKeyStack.empty()) {
                        objectKeyStack.pop_back();
                    }
                    break;
                case OrderedJson::parse_event_t::key: {
                    if (objectKeyStack.empty()) {
                        break;
                    }
                    const std::string key = parsed.get<std::string>();
                    std::vector<std::string>& keys = objectKeyStack.back();
                    if (std::ranges::find(keys, key) != keys.end()) {
                        throw DuplicateKeyError{key};
                    }
                    keys.push_back(key);
                    break;
                }
                default:
                    break;
                }
                return true;
            };

            return OrderedJson::parse(text, rejectDuplicateKeys, true, false);
        }

    } // namespace

    Result<std::vector<TypeSchema>> readSchemaDocument(std::string_view text) {
        try {
            const OrderedJson root = parseStrictJson(text);
            if (auto valid = validateObjectKeys(root, "root", {"schemaVersion", "types"}); !valid) {
                return std::unexpected{valid.error()};
            }

            auto schemaVersion = readUint32Member(root, "schemaVersion", "root");
            if (!schemaVersion) {
                return std::unexpected{schemaVersion.error()};
            }
            if (*schemaVersion != 1U) {
                return std::unexpected{
                    makeSchemaDocumentError("Unsupported schema document version.",
                                            {{"section", "root"},
                                             {"expected", "1"},
                                             {"actual", std::to_string(*schemaVersion)}})};
            }

            auto typesMember = requireMember(root, "types", "root");
            if (!typesMember) {
                return std::unexpected{typesMember.error()};
            }
            if (!(**typesMember).is_array()) {
                return std::unexpected{
                    makeSchemaDocumentError("Schema document types must be an array.",
                                            {{"section", "root"},
                                             {"expected", "array"},
                                             {"actual", (**typesMember).type_name()}})};
            }

            std::vector<TypeSchema> types;
            types.reserve((**typesMember).size());
            for (std::size_t index = 0; index < (**typesMember).size(); ++index) {
                auto type = readType((**typesMember)[index], index);
                if (!type) {
                    return std::unexpected{type.error()};
                }
                types.push_back(std::move(*type));
            }
            return types;
        } catch (const DuplicateKeyError& exception) {
            return std::unexpected{makeSchemaDocumentError(exception.what())};
        } catch (const nlohmann::json::parse_error& exception) {
            return std::unexpected{
                makeSchemaDocumentError("Failed to parse schema document at byte " +
                                        std::to_string(exception.byte) + ": " + exception.what())};
        } catch (const nlohmann::json::exception& exception) {
            return std::unexpected{makeSchemaDocumentError("Failed to read schema document: " +
                                                           std::string{exception.what()})};
        }
    }

    Result<std::vector<TypeSchema>> readSchemaDocumentFile(const std::filesystem::path& path,
                                                           SchemaDocumentFileOptions options) {
        auto text = core::readFileText(path, {.maxBytes = options.maxBytes});
        if (!text) {
            return std::unexpected{makeSchemaDocumentError(
                "Failed to read schema document '" + path.string() + "': " + text.error().message)};
        }

        auto document = readSchemaDocument(*text);
        if (!document) {
            return std::unexpected{makeSchemaDocumentError("Failed to parse schema document '" +
                                                           path.string() +
                                                           "': " + document.error().message)};
        }
        return document;
    }

} // namespace asharia::schema
