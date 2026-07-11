#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "asharia/cpp_binding/cpp_binding_builder.hpp"
#include "asharia/persistence/persistence.hpp"

namespace {

    constexpr std::string_view kVec3TypeName = "com.asharia.persistenceSmoke.Vec3";
    constexpr std::string_view kQuatTypeName = "com.asharia.persistenceSmoke.Quat";
    constexpr std::string_view kTransformTypeName = "com.asharia.persistenceSmoke.Transform";
    constexpr std::string_view kPropertyTypeName = "com.asharia.persistenceSmoke.Property";
    constexpr std::string_view kNestedObjectTypeName = "com.asharia.persistenceSmoke.NestedObject";
    constexpr std::string_view kObjectOwnerTypeName = "com.asharia.persistenceSmoke.ObjectOwner";
    constexpr std::string_view kUnsupportedEnumOwnerTypeName =
        "com.asharia.persistenceSmoke.UnsupportedEnumOwner";
    constexpr std::string_view kUnsupportedArrayOwnerTypeName =
        "com.asharia.persistenceSmoke.UnsupportedArrayOwner";
    constexpr std::string_view kUnsupportedAssetReferenceOwnerTypeName =
        "com.asharia.persistenceSmoke.UnsupportedAssetReferenceOwner";
    constexpr std::string_view kUnsupportedEntityReferenceOwnerTypeName =
        "com.asharia.persistenceSmoke.UnsupportedEntityReferenceOwner";
    constexpr std::string_view kMigratedTransformTypeName =
        "com.asharia.persistenceSmoke.MigratedTransform";

    struct Vec3 {
        float x{};
        float y{};
        float z{};
    };

    struct Quat {
        float x{};
        float y{};
        float z{};
        float w{1.0F};
    };

    struct Transform {
        Vec3 position;
        Quat rotation;
        Vec3 scale{.x = 1.0F, .y = 1.0F, .z = 1.0F};
        std::string debugName;
        float cachedMagnitude{};
        std::int32_t scriptCounter{};
    };

    struct MigratedTransform {
        Vec3 position;
        Quat rotation;
        Vec3 scale{.x = 1.0F, .y = 1.0F, .z = 1.0F};
        std::string debugName;
    };

    struct PropertyComponent {
        [[nodiscard]] float exposure() const noexcept {
            return exposure_;
        }

        asharia::VoidResult setExposure(const float& exposure) noexcept {
            exposure_ = exposure;
            dirty_ = true;
            return {};
        }

        [[nodiscard]] bool dirty() const noexcept {
            return dirty_;
        }

    private:
        float exposure_{1.5F};
        bool dirty_{};
    };

    struct NestedObject {
        float value{};
    };

    struct ObjectOwner {
        NestedObject child;
    };

    struct UnsupportedArrayOwner {
        float values{};
    };

    struct UnsupportedStringOwner {
        std::string value;
    };

} // namespace

namespace asharia::cpp_binding {

    template <> struct ReflectedType<Vec3> {
        [[nodiscard]] static schema::TypeId typeId() {
            return schema::makeTypeId(kVec3TypeName);
        }
    };

    template <> struct ReflectedType<Quat> {
        [[nodiscard]] static schema::TypeId typeId() {
            return schema::makeTypeId(kQuatTypeName);
        }
    };

    template <> struct ReflectedType<NestedObject> {
        [[nodiscard]] static schema::TypeId typeId() {
            return schema::makeTypeId(kNestedObjectTypeName);
        }
    };

} // namespace asharia::cpp_binding

namespace {

    [[nodiscard]] bool contains(std::string_view text, std::string_view needle) {
        return text.find(needle) != std::string_view::npos;
    }

    [[nodiscard]] asharia::schema::FieldSchema storedFloat(std::uint32_t fieldId,
                                                           std::string_view key) {
        return asharia::schema::FieldSchema{
            .id = asharia::schema::makeFieldId(fieldId),
            .key = std::string{key},
            .valueType = asharia::schema::builtin::floatTypeId(),
            .valueKind = asharia::schema::ValueKind::Float,
            .aliases = {},
            .metadata =
                {
                    .persistence = {.stored = true, .required = true},
                    .editor = {},
                    .script = {},
                    .numeric = {},
                },
        };
    }

    [[nodiscard]] asharia::schema::FieldSchema
    storedField(std::uint32_t fieldId, std::string_view key, std::string_view typeName,
                asharia::schema::ValueKind kind, bool hasDefault = false) {
        return asharia::schema::FieldSchema{
            .id = asharia::schema::makeFieldId(fieldId),
            .key = std::string{key},
            .valueType = asharia::schema::makeTypeId(typeName),
            .valueKind = kind,
            .aliases = {},
            .metadata =
                {
                    .persistence = {.stored = true, .required = true, .hasDefault = hasDefault},
                    .editor = {.visible = true,
                               .readOnly = false,
                               .displayName = {},
                               .category = {},
                               .tooltip = {},
                               .readOnlyReason = {}},
                    .script = {.visible = true,
                               .read = true,
                               .write = true,
                               .context = {},
                               .threadAffinity = {},
                               .lifetime = {}},
                    .numeric = {},
                },
        };
    }

    [[nodiscard]] asharia::schema::FieldSchema storedString(std::uint32_t fieldId,
                                                            std::string_view key) {
        return asharia::schema::FieldSchema{
            .id = asharia::schema::makeFieldId(fieldId),
            .key = std::string{key},
            .valueType = asharia::schema::builtin::stringTypeId(),
            .valueKind = asharia::schema::ValueKind::String,
            .aliases = {},
            .metadata =
                {
                    .persistence = {.stored = true, .required = false},
                    .editor = {},
                    .script = {},
                    .numeric = {},
                },
        };
    }

    [[nodiscard]] asharia::schema::TypeSchema makeVec3Schema() {
        return asharia::schema::TypeSchema{
            .id = asharia::schema::makeTypeId(kVec3TypeName),
            .canonicalName = std::string{kVec3TypeName},
            .version = 1,
            .kind = asharia::schema::ValueKind::InlineStruct,
            .fields = {storedFloat(1, "x"), storedFloat(2, "y"), storedFloat(3, "z")},
            .reservedFieldIds = {},
            .metadata = {},
        };
    }

    [[nodiscard]] asharia::schema::TypeSchema makeQuatSchema() {
        return asharia::schema::TypeSchema{
            .id = asharia::schema::makeTypeId(kQuatTypeName),
            .canonicalName = std::string{kQuatTypeName},
            .version = 1,
            .kind = asharia::schema::ValueKind::InlineStruct,
            .fields = {storedFloat(1, "x"), storedFloat(2, "y"), storedFloat(3, "z"),
                       storedFloat(4, "w")},
            .reservedFieldIds = {},
            .metadata = {},
        };
    }

    [[nodiscard]] asharia::schema::TypeSchema makeTransformSchema() {
        return asharia::schema::TypeSchema{
            .id = asharia::schema::makeTypeId(kTransformTypeName),
            .canonicalName = std::string{kTransformTypeName},
            .version = 1,
            .kind = asharia::schema::ValueKind::Object,
            .fields =
                {
                    storedField(1, "position", kVec3TypeName,
                                asharia::schema::ValueKind::InlineStruct),
                    storedField(2, "rotation", kQuatTypeName,
                                asharia::schema::ValueKind::InlineStruct),
                    storedField(3, "scale", kVec3TypeName, asharia::schema::ValueKind::InlineStruct,
                                true),
                    storedString(4, "debugName"),
                    asharia::schema::FieldSchema{
                        .id = asharia::schema::makeFieldId(5),
                        .key = "cachedMagnitude",
                        .valueType = asharia::schema::builtin::floatTypeId(),
                        .valueKind = asharia::schema::ValueKind::Float,
                        .aliases = {},
                        .metadata =
                            {
                                .persistence = {.stored = false},
                                .editor = {.visible = true,
                                           .readOnly = true,
                                           .displayName = {},
                                           .category = {},
                                           .tooltip = {},
                                           .readOnlyReason = {}},
                                .script = {},
                                .numeric = {},
                            },
                    },
                    asharia::schema::FieldSchema{
                        .id = asharia::schema::makeFieldId(6),
                        .key = "scriptCounter",
                        .valueType = asharia::schema::builtin::int32TypeId(),
                        .valueKind = asharia::schema::ValueKind::Integer,
                        .aliases = {},
                        .metadata =
                            {
                                .persistence = {.stored = false},
                                .editor = {},
                                .script = {.visible = true,
                                           .read = true,
                                           .write = false,
                                           .context = {},
                                           .threadAffinity = {},
                                           .lifetime = {}},
                                .numeric = {},
                            },
                    },
                },
            .reservedFieldIds = {},
            .metadata = {},
        };
    }

    [[nodiscard]] asharia::schema::TypeSchema makePropertySchema() {
        return asharia::schema::TypeSchema{
            .id = asharia::schema::makeTypeId(kPropertyTypeName),
            .canonicalName = std::string{kPropertyTypeName},
            .version = 1,
            .kind = asharia::schema::ValueKind::Object,
            .fields = {storedFloat(1, "exposure")},
            .reservedFieldIds = {},
            .metadata = {},
        };
    }

    [[nodiscard]] asharia::schema::TypeSchema makeNestedObjectSchema() {
        return asharia::schema::TypeSchema{
            .id = asharia::schema::makeTypeId(kNestedObjectTypeName),
            .canonicalName = std::string{kNestedObjectTypeName},
            .version = 1,
            .kind = asharia::schema::ValueKind::Object,
            .fields = {storedFloat(1, "value")},
            .reservedFieldIds = {},
            .metadata = {},
        };
    }

    [[nodiscard]] asharia::schema::TypeSchema makeObjectOwnerSchema() {
        return asharia::schema::TypeSchema{
            .id = asharia::schema::makeTypeId(kObjectOwnerTypeName),
            .canonicalName = std::string{kObjectOwnerTypeName},
            .version = 1,
            .kind = asharia::schema::ValueKind::Object,
            .fields =
                {
                    storedField(1, "child", kNestedObjectTypeName,
                                asharia::schema::ValueKind::Object),
                },
            .reservedFieldIds = {},
            .metadata = {},
        };
    }

    [[nodiscard]] asharia::schema::TypeSchema
    makeUnsupportedOwnerSchema(std::string_view typeName, std::string_view key,
                               asharia::schema::TypeId valueType, asharia::schema::ValueKind kind) {
        return asharia::schema::TypeSchema{
            .id = asharia::schema::makeTypeId(typeName),
            .canonicalName = std::string{typeName},
            .version = 1,
            .kind = asharia::schema::ValueKind::Object,
            .fields =
                {
                    asharia::schema::FieldSchema{
                        .id = asharia::schema::makeFieldId(1),
                        .key = std::string{key},
                        .valueType = std::move(valueType),
                        .valueKind = kind,
                        .aliases = {},
                        .metadata =
                            {
                                .persistence = {.stored = true},
                                .editor = {},
                                .script = {},
                                .numeric = {},
                            },
                    },
                },
            .reservedFieldIds = {},
            .metadata = {},
        };
    }

    [[nodiscard]] asharia::schema::TypeSchema makeMigratedTransformSchema() {
        auto schema = makeTransformSchema();
        schema.id = asharia::schema::makeTypeId(kMigratedTransformTypeName);
        schema.canonicalName = std::string{kMigratedTransformTypeName};
        schema.version = 2;
        schema.fields.resize(4);
        schema.fields[1].aliases.emplace_back("eulerRotation");
        return schema;
    }

    [[nodiscard]] std::optional<asharia::schema::SchemaRegistry> makeSchemas() {
        asharia::schema::SchemaRegistry schemas;
        if (auto registered = asharia::schema::registerBuiltinSchemas(schemas); !registered) {
            std::cerr << registered.error().message << '\n';
            return std::nullopt;
        }
        for (asharia::schema::TypeSchema schema :
             {makeVec3Schema(), makeQuatSchema(), makeTransformSchema(), makePropertySchema(),
              makeNestedObjectSchema(), makeObjectOwnerSchema(),
              makeUnsupportedOwnerSchema(kUnsupportedEnumOwnerTypeName, "value",
                                         asharia::schema::builtin::stringTypeId(),
                                         asharia::schema::ValueKind::Enum),
              makeUnsupportedOwnerSchema(kUnsupportedArrayOwnerTypeName, "values",
                                         asharia::schema::builtin::floatTypeId(),
                                         asharia::schema::ValueKind::Array),
              makeUnsupportedOwnerSchema(kUnsupportedAssetReferenceOwnerTypeName, "value",
                                         asharia::schema::builtin::stringTypeId(),
                                         asharia::schema::ValueKind::AssetReference),
              makeUnsupportedOwnerSchema(kUnsupportedEntityReferenceOwnerTypeName, "value",
                                         asharia::schema::builtin::stringTypeId(),
                                         asharia::schema::ValueKind::EntityReference),
              makeMigratedTransformSchema()}) {
            if (auto registered = schemas.registerType(std::move(schema)); !registered) {
                std::cerr << registered.error().message << '\n';
                return std::nullopt;
            }
        }
        if (auto frozen = schemas.freeze(); !frozen) {
            std::cerr << frozen.error().message << '\n';
            return std::nullopt;
        }
        return schemas;
    }

    [[nodiscard]] std::optional<asharia::cpp_binding::BindingRegistry>
    makeBindings(const asharia::schema::SchemaRegistry& schemas) {
        asharia::cpp_binding::BindingRegistry bindings{schemas};
        using asharia::cpp_binding::CppBindingBuilder;
        using asharia::schema::makeFieldId;
        using asharia::schema::makeTypeId;

        if (auto committed = CppBindingBuilder<Vec3>(bindings, makeTypeId(kVec3TypeName), "Vec3")
                                 .field(makeFieldId(1), "x", &Vec3::x)
                                 .field(makeFieldId(2), "y", &Vec3::y)
                                 .field(makeFieldId(3), "z", &Vec3::z)
                                 .commit();
            !committed) {
            std::cerr << committed.error().message << '\n';
            return std::nullopt;
        }
        if (auto committed = CppBindingBuilder<Quat>(bindings, makeTypeId(kQuatTypeName), "Quat")
                                 .field(makeFieldId(1), "x", &Quat::x)
                                 .field(makeFieldId(2), "y", &Quat::y)
                                 .field(makeFieldId(3), "z", &Quat::z)
                                 .field(makeFieldId(4), "w", &Quat::w)
                                 .commit();
            !committed) {
            std::cerr << committed.error().message << '\n';
            return std::nullopt;
        }
        if (auto committed =
                CppBindingBuilder<Transform>(bindings, makeTypeId(kTransformTypeName), "Transform")
                    .field(makeFieldId(1), "position", &Transform::position)
                    .field(makeFieldId(2), "rotation", &Transform::rotation)
                    .field(makeFieldId(3), "scale", &Transform::scale)
                    .defaultValue(Vec3{.x = 1.0F, .y = 1.0F, .z = 1.0F})
                    .field(makeFieldId(4), "debugName", &Transform::debugName)
                    .readonlyField(makeFieldId(5), "cachedMagnitude", &Transform::cachedMagnitude)
                    .readonlyField(makeFieldId(6), "scriptCounter", &Transform::scriptCounter)
                    .commit();
            !committed) {
            std::cerr << committed.error().message << '\n';
            return std::nullopt;
        }
        if (auto committed =
                CppBindingBuilder<PropertyComponent>(bindings, makeTypeId(kPropertyTypeName),
                                                     "PropertyComponent")
                    .property<float>(
                        makeFieldId(1), "exposure",
                        [](const PropertyComponent& component) { return component.exposure(); },
                        [](PropertyComponent& component, const float& exposure) {
                            return component.setExposure(exposure);
                        })
                    .commit();
            !committed) {
            std::cerr << committed.error().message << '\n';
            return std::nullopt;
        }
        if (auto committed = CppBindingBuilder<NestedObject>(
                                 bindings, makeTypeId(kNestedObjectTypeName), "NestedObject")
                                 .field(makeFieldId(1), "value", &NestedObject::value)
                                 .commit();
            !committed) {
            std::cerr << committed.error().message << '\n';
            return std::nullopt;
        }
        if (auto committed = CppBindingBuilder<ObjectOwner>(
                                 bindings, makeTypeId(kObjectOwnerTypeName), "ObjectOwner")
                                 .field(makeFieldId(1), "child", &ObjectOwner::child)
                                 .commit();
            !committed) {
            std::cerr << committed.error().message << '\n';
            return std::nullopt;
        }
        if (auto committed =
                CppBindingBuilder<UnsupportedArrayOwner>(
                    bindings, makeTypeId(kUnsupportedArrayOwnerTypeName), "UnsupportedArrayOwner")
                    .field(makeFieldId(1), "values", &UnsupportedArrayOwner::values)
                    .commit();
            !committed) {
            std::cerr << committed.error().message << '\n';
            return std::nullopt;
        }

        const auto commitUnsupportedStringBinding = [&](std::string_view typeName,
                                                        std::string_view cppTypeName) -> bool {
            if (auto committed = CppBindingBuilder<UnsupportedStringOwner>(
                                     bindings, makeTypeId(typeName), cppTypeName)
                                     .field(makeFieldId(1), "value", &UnsupportedStringOwner::value)
                                     .commit();
                !committed) {
                std::cerr << committed.error().message << '\n';
                return false;
            }
            return true;
        };
        if (!commitUnsupportedStringBinding(kUnsupportedEnumOwnerTypeName,
                                            "UnsupportedEnumOwner") ||
            !commitUnsupportedStringBinding(kUnsupportedAssetReferenceOwnerTypeName,
                                            "UnsupportedAssetReferenceOwner") ||
            !commitUnsupportedStringBinding(kUnsupportedEntityReferenceOwnerTypeName,
                                            "UnsupportedEntityReferenceOwner")) {
            return std::nullopt;
        }
        if (auto committed =
                CppBindingBuilder<MigratedTransform>(
                    bindings, makeTypeId(kMigratedTransformTypeName), "MigratedTransform")
                    .field(makeFieldId(1), "position", &MigratedTransform::position)
                    .field(makeFieldId(2), "rotation", &MigratedTransform::rotation)
                    .field(makeFieldId(3), "scale", &MigratedTransform::scale)
                    .defaultValue(Vec3{.x = 1.0F, .y = 1.0F, .z = 1.0F})
                    .field(makeFieldId(4), "debugName", &MigratedTransform::debugName)
                    .commit();
            !committed) {
            std::cerr << committed.error().message << '\n';
            return std::nullopt;
        }

        if (auto frozen = bindings.freeze(); !frozen) {
            std::cerr << frozen.error().message << '\n';
            return std::nullopt;
        }
        return bindings;
    }

    [[nodiscard]] asharia::archive::ArchiveValue makeVec3Archive(float xValue, float yValue,
                                                                 float zValue) {
        return asharia::archive::ArchiveValue::object({
            asharia::archive::ArchiveMember{
                .key = "x", .value = asharia::archive::ArchiveValue::floating(xValue)},
            asharia::archive::ArchiveMember{
                .key = "y", .value = asharia::archive::ArchiveValue::floating(yValue)},
            asharia::archive::ArchiveMember{
                .key = "z", .value = asharia::archive::ArchiveValue::floating(zValue)},
        });
    }

    [[nodiscard]] asharia::archive::ArchiveValue
    makeQuatArchive(const asharia::archive::ArchiveValue& zValue) {
        return asharia::archive::ArchiveValue::object({
            asharia::archive::ArchiveMember{.key = "x",
                                            .value = asharia::archive::ArchiveValue::floating(0.0)},
            asharia::archive::ArchiveMember{.key = "y",
                                            .value = asharia::archive::ArchiveValue::floating(0.0)},
            asharia::archive::ArchiveMember{.key = "z", .value = zValue},
            asharia::archive::ArchiveMember{.key = "w",
                                            .value = asharia::archive::ArchiveValue::floating(1.0)},
        });
    }

    [[nodiscard]] asharia::archive::ArchiveValue makeMigratedTransformV1Archive() {
        return asharia::archive::ArchiveValue::object({
            asharia::archive::ArchiveMember{
                .key = "type",
                .value =
                    asharia::archive::ArchiveValue::string(std::string{kMigratedTransformTypeName}),
            },
            asharia::archive::ArchiveMember{
                .key = "version",
                .value = asharia::archive::ArchiveValue::integer(1),
            },
            asharia::archive::ArchiveMember{
                .key = "fields",
                .value = asharia::archive::ArchiveValue::object({
                    asharia::archive::ArchiveMember{
                        .key = "position",
                        .value = makeVec3Archive(1.0F, 2.0F, 3.0F),
                    },
                    asharia::archive::ArchiveMember{
                        .key = "eulerRotation",
                        .value = makeVec3Archive(0.0F, 0.0F, 0.25F),
                    },
                    asharia::archive::ArchiveMember{
                        .key = "debugName",
                        .value = asharia::archive::ArchiveValue::string("migrated"),
                    },
                }),
            },
        });
    }

    asharia::VoidResult migrateTransformV1ToV2(asharia::persistence::MigrationContext& context) {
        const asharia::archive::ArchiveValue* inputFields =
            context.input == nullptr ? nullptr : context.input->findMemberValue("fields");
        const asharia::archive::ArchiveValue* position =
            inputFields == nullptr ? nullptr : inputFields->findMemberValue("position");
        const asharia::archive::ArchiveValue* eulerRotation =
            inputFields == nullptr ? nullptr : inputFields->findMemberValue("eulerRotation");
        const asharia::archive::ArchiveValue* debugName =
            inputFields == nullptr ? nullptr : inputFields->findMemberValue("debugName");
        const asharia::archive::ArchiveValue* eulerZ =
            eulerRotation == nullptr ? nullptr : eulerRotation->findMemberValue("z");
        if (context.output == nullptr || position == nullptr || eulerZ == nullptr) {
            return std::unexpected{
                asharia::persistence::persistenceError("Missing v1 transform migration input.")};
        }

        std::vector<asharia::archive::ArchiveMember> fields{
            asharia::archive::ArchiveMember{.key = "position", .value = *position},
            asharia::archive::ArchiveMember{.key = "rotation", .value = makeQuatArchive(*eulerZ)},
            asharia::archive::ArchiveMember{.key = "scale",
                                            .value = makeVec3Archive(1.0F, 1.0F, 1.0F)},
        };
        if (debugName != nullptr) {
            fields.push_back(
                asharia::archive::ArchiveMember{.key = "debugName", .value = *debugName});
        }

        *context.output = asharia::archive::ArchiveValue::object({
            asharia::archive::ArchiveMember{
                .key = "type",
                .value =
                    asharia::archive::ArchiveValue::string(std::string{kMigratedTransformTypeName}),
            },
            asharia::archive::ArchiveMember{
                .key = "version",
                .value = asharia::archive::ArchiveValue::integer(context.toVersion),
            },
            asharia::archive::ArchiveMember{
                .key = "fields",
                .value = asharia::archive::ArchiveValue::object(std::move(fields)),
            },
        });
        return {};
    }

    [[nodiscard]] asharia::archive::ArchiveValue
    makeUnsupportedOwnerArchive(std::string_view typeName, std::string_view fieldKey,
                                const asharia::archive::ArchiveValue& fieldValue) {
        return asharia::archive::ArchiveValue::object({
            asharia::archive::ArchiveMember{
                .key = "type",
                .value = asharia::archive::ArchiveValue::string(std::string{typeName}),
            },
            asharia::archive::ArchiveMember{
                .key = "version",
                .value = asharia::archive::ArchiveValue::integer(1),
            },
            asharia::archive::ArchiveMember{
                .key = "fields",
                .value = asharia::archive::ArchiveValue::object({
                    asharia::archive::ArchiveMember{.key = std::string{fieldKey},
                                                    .value = fieldValue},
                }),
            },
        });
    }

    struct UnsupportedTypeName {
        UnsupportedTypeName(const char* value) : text{value} {}
        UnsupportedTypeName(std::string_view value) : text{value} {}
        std::string_view text;
    };

    struct UnsupportedFieldKey {
        UnsupportedFieldKey(const char* value) : text{value} {}
        UnsupportedFieldKey(std::string_view value) : text{value} {}
        std::string_view text;
    };

    struct UnsupportedKindName {
        UnsupportedKindName(const char* value) : text{value} {}
        UnsupportedKindName(std::string_view value) : text{value} {}
        std::string_view text;
    };

    template <typename ObjectT>
    [[nodiscard]] bool
    expectUnsupportedKindRejected(const asharia::schema::SchemaRegistry& schemas,
                                  const asharia::cpp_binding::BindingRegistry& bindings,
                                  UnsupportedTypeName typeName, UnsupportedFieldKey fieldKey,
                                  UnsupportedKindName kindName, const ObjectT& source,
                                  const asharia::archive::ArchiveValue& fieldValue) {
        auto saved = asharia::persistence::saveObject(
            schemas, bindings, asharia::schema::makeTypeId(typeName.text), &source);
        if (saved || !contains(saved.error().message, "not supported") ||
            !contains(saved.error().message, kindName.text)) {
            std::cerr << "Persistence accepted unsupported " << kindName.text << " during save.\n";
            return false;
        }

        ObjectT loaded{};
        const asharia::archive::ArchiveValue archive =
            makeUnsupportedOwnerArchive(typeName.text, fieldKey.text, fieldValue);
        auto loadedResult = asharia::persistence::loadObject(
            schemas, bindings, asharia::schema::makeTypeId(typeName.text), archive, &loaded);
        if (loadedResult || !contains(loadedResult.error().message, "not supported") ||
            !contains(loadedResult.error().message, kindName.text)) {
            std::cerr << "Persistence accepted unsupported " << kindName.text << " during load.\n";
            return false;
        }
        return true;
    }

    [[nodiscard]] bool
    verifyUnsupportedKindsAndProperty(const asharia::schema::SchemaRegistry& schemas,
                                      const asharia::cpp_binding::BindingRegistry& bindings) {
        const UnsupportedStringOwner unsupportedStringOwner{.value = "token"};
        if (!expectUnsupportedKindRejected(schemas, bindings, kUnsupportedEnumOwnerTypeName,
                                           "value", "enum", unsupportedStringOwner,
                                           asharia::archive::ArchiveValue::string("Token"))) {
            return false;
        }
        const UnsupportedArrayOwner unsupportedArrayOwner{.values = 1.0F};
        if (!expectUnsupportedKindRejected(schemas, bindings, kUnsupportedArrayOwnerTypeName,
                                           "values", "array", unsupportedArrayOwner,
                                           asharia::archive::ArchiveValue::array(
                                               {asharia::archive::ArchiveValue::floating(1.0)}))) {
            return false;
        }
        if (!expectUnsupportedKindRejected(schemas, bindings,
                                           kUnsupportedAssetReferenceOwnerTypeName, "value",
                                           "asset reference", unsupportedStringOwner,
                                           asharia::archive::ArchiveValue::string("asset-guid")) ||
            !expectUnsupportedKindRejected(schemas, bindings,
                                           kUnsupportedEntityReferenceOwnerTypeName, "value",
                                           "entity reference", unsupportedStringOwner,
                                           asharia::archive::ArchiveValue::string("entity:1"))) {
            return false;
        }

        PropertyComponent propertySource;
        if (auto set = propertySource.setExposure(2.75F); !set) {
            std::cerr << set.error().message << '\n';
            return false;
        }
        auto propertyArchive = asharia::persistence::saveObject(
            schemas, bindings, asharia::schema::makeTypeId(kPropertyTypeName), &propertySource);
        if (!propertyArchive) {
            std::cerr << propertyArchive.error().message << '\n';
            return false;
        }
        PropertyComponent propertyLoaded;
        if (auto result = asharia::persistence::loadObject(
                schemas, bindings, asharia::schema::makeTypeId(kPropertyTypeName), *propertyArchive,
                &propertyLoaded);
            !result || propertyLoaded.exposure() != 2.75F || !propertyLoaded.dirty()) {
            std::cerr << "Persistence property roundtrip failed.\n";
            return false;
        }
        return true;
    }

    [[nodiscard]] bool
    verifyUnknownAndMissingFields(const asharia::schema::SchemaRegistry& schemas,
                                  const asharia::cpp_binding::BindingRegistry& bindings,
                                  const asharia::archive::ArchiveValue& archive,
                                  const Transform& source) {
        asharia::archive::ArchiveValue unknownArchive = archive;
        asharia::archive::ArchiveValue* unknownFields = unknownArchive.findMemberValue("fields");
        unknownFields->objectValue.push_back(asharia::archive::ArchiveMember{
            .key = "unknownFutureField",
            .value = asharia::archive::ArchiveValue::integer(7),
        });
        Transform rejectedUnknownObject{};
        auto rejectedUnknown = asharia::persistence::loadObject(
            schemas, bindings, asharia::schema::makeTypeId(kTransformTypeName), unknownArchive,
            &rejectedUnknownObject);
        if (rejectedUnknown || !contains(rejectedUnknown.error().message, "unknown field")) {
            std::cerr << "Persistence accepted an unknown field.\n";
            return false;
        }

        Transform droppedUnknown{};
        const asharia::persistence::PersistencePolicy dropUnknownPolicy{
            .includeTypeHeader = true,
            .unknownFields = asharia::persistence::UnknownFieldPolicy::Drop,
            .missingFields = asharia::persistence::MissingFieldPolicy::UseDefault,
            .migrations = nullptr,
            .archivePath = {},
            .migrationScenario = asharia::persistence::MigrationScenario::Unspecified,
        };
        if (auto result = asharia::persistence::loadObject(
                schemas, bindings, asharia::schema::makeTypeId(kTransformTypeName), unknownArchive,
                &droppedUnknown, dropUnknownPolicy);
            !result || droppedUnknown.position.x != source.position.x) {
            std::cerr << "Persistence did not drop an unknown field.\n";
            return false;
        }

        asharia::archive::ArchiveValue missingScaleArchive = archive;
        asharia::archive::ArchiveValue* missingScaleFields =
            missingScaleArchive.findMemberValue("fields");
        std::erase_if(
            missingScaleFields->objectValue,
            [](const asharia::archive::ArchiveMember& member) { return member.key == "scale"; });
        Transform defaultedScale{
            .position = {},
            .rotation = {},
            .scale = {.x = 8.0F, .y = 8.0F, .z = 8.0F},
            .debugName = {},
            .cachedMagnitude = 0.0F,
            .scriptCounter = 0,
        };
        if (auto result = asharia::persistence::loadObject(
                schemas, bindings, asharia::schema::makeTypeId(kTransformTypeName),
                missingScaleArchive, &defaultedScale);
            !result || defaultedScale.scale.x != 1.0F || defaultedScale.scale.y != 1.0F ||
            defaultedScale.scale.z != 1.0F) {
            std::cerr << "Persistence did not apply a missing field default.\n";
            return false;
        }

        asharia::archive::ArchiveValue missingPositionArchive = archive;
        asharia::archive::ArchiveValue* missingPositionFields =
            missingPositionArchive.findMemberValue("fields");
        std::erase_if(
            missingPositionFields->objectValue,
            [](const asharia::archive::ArchiveMember& member) { return member.key == "position"; });
        Transform missingPosition{};
        auto missingPositionResult = asharia::persistence::loadObject(
            schemas, bindings, asharia::schema::makeTypeId(kTransformTypeName),
            missingPositionArchive, &missingPosition);
        if (missingPositionResult || !contains(missingPositionResult.error().message, "default")) {
            std::cerr << "Persistence accepted a missing field without a default.\n";
            return false;
        }
        return true;
    }

    [[nodiscard]] bool verifyMigration(const asharia::schema::SchemaRegistry& schemas,
                                       const asharia::cpp_binding::BindingRegistry& bindings) {
        asharia::persistence::MigrationRegistry migrations;
        if (auto registered = migrations.registerMigration(
                asharia::schema::makeTypeId(kMigratedTransformTypeName), 1, 2,
                migrateTransformV1ToV2);
            !registered) {
            std::cerr << registered.error().message << '\n';
            return false;
        }
        auto duplicateMigration = migrations.registerMigration(
            asharia::schema::makeTypeId(kMigratedTransformTypeName), 1, 2, migrateTransformV1ToV2);
        if (duplicateMigration || !contains(duplicateMigration.error().message, "duplicate")) {
            std::cerr << "Persistence accepted a duplicate migration.\n";
            return false;
        }

        const asharia::archive::ArchiveValue oldArchive = makeMigratedTransformV1Archive();
        MigratedTransform rejectedMigration{};
        auto missingMigrationResult = asharia::persistence::loadObject(
            schemas, bindings, asharia::schema::makeTypeId(kMigratedTransformTypeName), oldArchive,
            &rejectedMigration);
        if (missingMigrationResult ||
            !contains(missingMigrationResult.error().message, "requires migration")) {
            std::cerr << "Persistence did not require a migration policy.\n";
            return false;
        }

        const asharia::persistence::PersistencePolicy migrationPolicy{
            .includeTypeHeader = true,
            .unknownFields = asharia::persistence::UnknownFieldPolicy::Error,
            .missingFields = asharia::persistence::MissingFieldPolicy::UseDefault,
            .migrations = &migrations,
            .archivePath = {},
            .migrationScenario = asharia::persistence::MigrationScenario::Unspecified,
        };
        MigratedTransform migrated{};
        if (auto result = asharia::persistence::loadObject(
                schemas, bindings, asharia::schema::makeTypeId(kMigratedTransformTypeName),
                oldArchive, &migrated, migrationPolicy);
            !result || migrated.position.x != 1.0F || migrated.position.y != 2.0F ||
            migrated.position.z != 3.0F || migrated.rotation.z != 0.25F ||
            migrated.rotation.w != 1.0F || migrated.scale.x != 1.0F ||
            migrated.debugName != "migrated") {
            std::cerr << "Persistence migration did not load expected values.\n";
            return false;
        }
        return true;
    }

    [[nodiscard]] int runPersistenceSmokeTests() {
        auto schemas = makeSchemas();
        if (!schemas) {
            return EXIT_FAILURE;
        }
        auto bindings = makeBindings(*schemas);
        if (!bindings) {
            return EXIT_FAILURE;
        }

        const Transform source{
            .position = {.x = 1.0F, .y = 2.0F, .z = 3.0F},
            .rotation = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F},
            .scale = {.x = 2.0F, .y = 2.0F, .z = 2.0F},
            .debugName = "roundtrip",
            .cachedMagnitude = 99.0F,
            .scriptCounter = 12,
        };

        auto archive = asharia::persistence::saveObject(
            *schemas, *bindings, asharia::schema::makeTypeId(kTransformTypeName), &source);
        if (!archive) {
            std::cerr << archive.error().message << '\n';
            return EXIT_FAILURE;
        }

        const asharia::archive::ArchiveValue* fields = archive->findMemberValue("fields");
        const asharia::archive::ArchiveValue* position =
            fields == nullptr ? nullptr : fields->findMemberValue("position");
        if (position == nullptr || position->findMemberValue("type") != nullptr) {
            std::cerr << "Persistence added an envelope to an inline Vec3 field.\n";
            return EXIT_FAILURE;
        }

        Transform loaded{};
        if (auto result = asharia::persistence::loadObject(
                *schemas, *bindings, asharia::schema::makeTypeId(kTransformTypeName), *archive,
                &loaded);
            !result) {
            std::cerr << result.error().message << '\n';
            return EXIT_FAILURE;
        }
        if (loaded.position.x != source.position.x || loaded.position.y != source.position.y ||
            loaded.position.z != source.position.z || loaded.scale.x != source.scale.x ||
            loaded.debugName != source.debugName || loaded.cachedMagnitude != 0.0F ||
            loaded.scriptCounter != 0) {
            std::cerr << "Persistence roundtrip loaded unexpected transform values.\n";
            return EXIT_FAILURE;
        }

        const ObjectOwner objectOwner{
            .child = {.value = 42.0F},
        };
        auto objectOwnerArchive = asharia::persistence::saveObject(
            *schemas, *bindings, asharia::schema::makeTypeId(kObjectOwnerTypeName), &objectOwner);
        if (!objectOwnerArchive) {
            std::cerr << objectOwnerArchive.error().message << '\n';
            return EXIT_FAILURE;
        }
        const asharia::archive::ArchiveValue* objectOwnerFields =
            objectOwnerArchive->findMemberValue("fields");
        const asharia::archive::ArchiveValue* childArchive =
            objectOwnerFields == nullptr ? nullptr : objectOwnerFields->findMemberValue("child");
        if (childArchive == nullptr || childArchive->findMemberValue("type") == nullptr ||
            childArchive->findMemberValue("version") == nullptr ||
            childArchive->findMemberValue("fields") == nullptr) {
            std::cerr << "Persistence did not add an envelope to an object-valued field.\n";
            return EXIT_FAILURE;
        }

        ObjectOwner loadedObjectOwner{};
        if (auto result = asharia::persistence::loadObject(
                *schemas, *bindings, asharia::schema::makeTypeId(kObjectOwnerTypeName),
                *objectOwnerArchive, &loadedObjectOwner);
            !result || loadedObjectOwner.child.value != objectOwner.child.value) {
            std::cerr << "Persistence object-valued field roundtrip failed.\n";
            return EXIT_FAILURE;
        }

        if (!verifyUnsupportedKindsAndProperty(*schemas, *bindings)) {
            return EXIT_FAILURE;
        }

        if (!verifyUnknownAndMissingFields(*schemas, *bindings, *archive, source)) {
            return EXIT_FAILURE;
        }

        if (!verifyMigration(*schemas, *bindings)) {
            return EXIT_FAILURE;
        }

        std::cout << "Persistence roundtrip fields: " << fields->objectValue.size() << '\n';
        return EXIT_SUCCESS;
    }

} // namespace

// The exhaustive catch boundary converts all failures to the smoke-test exit protocol.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() noexcept {
    try {
        return runPersistenceSmokeTests();
    } catch (const std::exception& error) {
        std::cerr << "Persistence smoke test threw: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "Persistence smoke test threw an unknown exception.\n";
    }
    return EXIT_FAILURE;
}
