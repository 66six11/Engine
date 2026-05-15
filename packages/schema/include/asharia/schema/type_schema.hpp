#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/schema/ids.hpp"

namespace asharia::schema {

    enum class ValueKind : std::uint8_t {
        Null,
        Bool,
        Integer,
        Float,
        String,
        Enum,
        Array,
        Object,
        InlineStruct,
        AssetReference,
        EntityReference,
    };

    struct PersistenceSpec {
        bool stored{};
        bool required{true};
        bool hasDefault{};
        std::uint32_t sinceVersion{1};
        std::uint32_t deprecatedSince{};
    };

    struct EditorSpec {
        bool visible{};
        bool readOnly{};
        std::string displayName;
        std::string category;
        std::string tooltip;
        std::string readOnlyReason;
    };

    struct ScriptSpec {
        bool visible{};
        bool read{};
        bool write{};
        std::string context;
        std::string threadAffinity;
        std::string lifetime;
    };

    struct NumericSpec {
        bool hasMin{};
        bool hasMax{};
        double min{};
        double max{};
        double step{};
        std::string unit;
    };

    struct TypedMetadata {
        PersistenceSpec persistence;
        EditorSpec editor;
        ScriptSpec script;
        NumericSpec numeric;
    };

    struct FieldSchema {
        FieldId id{};
        std::string key;
        TypeId valueType{};
        ValueKind valueKind{ValueKind::Object};
        std::vector<std::string> aliases;
        TypedMetadata metadata;
    };

    struct TypeSchema {
        TypeId id{};
        std::string canonicalName;
        std::uint32_t version{1};
        ValueKind kind{ValueKind::Object};
        std::vector<FieldSchema> fields;
        std::vector<FieldId> reservedFieldIds;
        TypedMetadata metadata;
    };

    struct FieldProjection {
        std::vector<const FieldSchema*> fields;
    };

    [[nodiscard]] inline const FieldSchema* findFieldById(const TypeSchema& type, FieldId fieldId) {
        for (const FieldSchema& field : type.fields) {
            if (field.id == fieldId) {
                return &field;
            }
        }
        return nullptr;
    }

    [[nodiscard]] inline const FieldSchema* findFieldByKey(const TypeSchema& type,
                                                           std::string_view key) {
        for (const FieldSchema& field : type.fields) {
            if (field.key == key) {
                return &field;
            }
        }
        return nullptr;
    }

    [[nodiscard]] inline const FieldSchema* findFieldByKeyOrAlias(const TypeSchema& type,
                                                                  std::string_view key) {
        for (const FieldSchema& field : type.fields) {
            if (field.key == key) {
                return &field;
            }
            for (const std::string& alias : field.aliases) {
                if (alias == key) {
                    return &field;
                }
            }
        }
        return nullptr;
    }

    [[nodiscard]] inline FieldProjection makePersistenceProjection(const TypeSchema& type) {
        FieldProjection projection;
        for (const FieldSchema& field : type.fields) {
            if (field.metadata.persistence.stored) {
                projection.fields.push_back(&field);
            }
        }
        return projection;
    }

    [[nodiscard]] inline FieldProjection makeEditorProjection(const TypeSchema& type) {
        FieldProjection projection;
        for (const FieldSchema& field : type.fields) {
            if (field.metadata.editor.visible) {
                projection.fields.push_back(&field);
            }
        }
        return projection;
    }

    [[nodiscard]] inline FieldProjection makeScriptProjection(const TypeSchema& type) {
        FieldProjection projection;
        for (const FieldSchema& field : type.fields) {
            if (field.metadata.script.visible) {
                projection.fields.push_back(&field);
            }
        }
        return projection;
    }

} // namespace asharia::schema
