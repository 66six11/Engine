#include <algorithm>
#include <cstdlib>
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
    constexpr std::string_view kNestedObjectTypeName =
        "com.asharia.persistenceSmoke.NestedObject";
    constexpr std::string_view kObjectOwnerTypeName =
        "com.asharia.persistenceSmoke.ObjectOwner";
    constexpr std::string_view kUnsupportedArrayOwnerTypeName =
        "com.asharia.persistenceSmoke.UnsupportedArrayOwner";
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

    [[nodiscard]] bool contains(std::string_view text, std::string_view needle) {
        return text.find(needle) != std::string_view::npos;
    }

    [[nodiscard]] asharia::schema::FieldSchema storedFloat(std::uint32_t id, std::string_view key) {
        return asharia::schema::FieldSchema{
            .id = asharia::schema::makeFieldId(id),
            .key = std::string{key},
            .valueType = asharia::schema::builtin::floatTypeId(),
            .valueKind = asharia::schema::ValueKind::Float,
            .aliases = {},
            .metadata = {.persistence = {.stored = true, .required = true}},
        };
    }

    [[nodiscard]] asharia::schema::FieldSchema storedField(std::uint32_t id, std::string_view key,
                                                           std::string_view typeName,
                                                           asharia::schema::ValueKind kind,
                                                           bool hasDefault = false) {
        return asharia::schema::FieldSchema{
            .id = asharia::schema::makeFieldId(id),
            .key = std::string{key},
            .valueType = asharia::schema::makeTypeId(typeName),
            .valueKind = kind,
            .aliases = {},
            .metadata =
                {
                    .persistence = {.stored = true, .required = true, .hasDefault = hasDefault},
                    .editor = {.visible = true},
                    .script = {.visible = true, .read = true, .write = true},
                },
        };
    }

    [[nodiscard]] asharia::schema::FieldSchema storedString(std::uint32_t id,
                                                            std::string_view key) {
        return asharia::schema::FieldSchema{
            .id = asharia::schema::makeFieldId(id),
            .key = std::string{key},
            .valueType = asharia::schema::builtin::stringTypeId(),
            .valueKind = asharia::schema::ValueKind::String,
            .aliases = {},
            .metadata = {.persistence = {.stored = true, .required = false}},
        };
    }

    [[nodiscard]] asharia::schema::TypeSchema makeVec3Schema() {
        return asharia::schema::TypeSchema{
            .id = asharia::schema::makeTypeId(kVec3TypeName),
            .canonicalName = std::string{kVec3TypeName},
            .version = 1,
            .kind = asharia::schema::ValueKind::InlineStruct,
            .fields = {storedFloat(1, "x"), storedFloat(2, "y"), storedFloat(3, "z")},
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
                        .metadata = {.persistence = {.stored = false},
                                     .editor = {.visible = true, .readOnly = true}},
                    },
                    asharia::schema::FieldSchema{
                        .id = asharia::schema::makeFieldId(6),
                        .key = "scriptCounter",
                        .valueType = asharia::schema::builtin::int32TypeId(),
                        .valueKind = asharia::schema::ValueKind::Integer,
                        .metadata = {.persistence = {.stored = false},
                                     .script = {.visible = true, .read = true}},
                    },
                },
        };
    }

    [[nodiscard]] asharia::schema::TypeSchema makePropertySchema() {
        return asharia::schema::TypeSchema{
            .id = asharia::schema::makeTypeId(kPropertyTypeName),
            .canonicalName = std::string{kPropertyTypeName},
            .version = 1,
            .kind = asharia::schema::ValueKind::Object,
            .fields = {storedFloat(1, "exposure")},
        };
    }

    [[nodiscard]] asharia::schema::TypeSchema makeNestedObjectSchema() {
        return asharia::schema::TypeSchema{
            .id = asharia::schema::makeTypeId(kNestedObjectTypeName),
            .canonicalName = std::string{kNestedObjectTypeName},
            .version = 1,
            .kind = asharia::schema::ValueKind::Object,
            .fields = {storedFloat(1, "value")},
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
        };
    }

    [[nodiscard]] asharia::schema::TypeSchema makeUnsupportedArrayOwnerSchema() {
        return asharia::schema::TypeSchema{
            .id = asharia::schema::makeTypeId(kUnsupportedArrayOwnerTypeName),
            .canonicalName = std::string{kUnsupportedArrayOwnerTypeName},
            .version = 1,
            .kind = asharia::schema::ValueKind::Object,
            .fields =
                {
                    asharia::schema::FieldSchema{
                        .id = asharia::schema::makeFieldId(1),
                        .key = "values",
                        .valueType = asharia::schema::builtin::floatTypeId(),
                        .valueKind = asharia::schema::ValueKind::Array,
                        .metadata = {.persistence = {.stored = true}},
                    },
                },
        };
    }

    [[nodiscard]] asharia::schema::TypeSchema makeMigratedTransformSchema() {
        auto schema = makeTransformSchema();
        schema.id = asharia::schema::makeTypeId(kMigratedTransformTypeName);
        schema.canonicalName = std::string{kMigratedTransformTypeName};
        schema.version = 2;
        schema.fields.resize(4);
        schema.fields[1].aliases.push_back("eulerRotation");
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
              makeUnsupportedArrayOwnerSchema(), makeMigratedTransformSchema()}) {
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
        if (auto committed =
                CppBindingBuilder<NestedObject>(bindings, makeTypeId(kNestedObjectTypeName),
                                                "NestedObject")
                    .field(makeFieldId(1), "value", &NestedObject::value)
                    .commit();
            !committed) {
            std::cerr << committed.error().message << '\n';
            return std::nullopt;
        }
        if (auto committed =
                CppBindingBuilder<ObjectOwner>(bindings, makeTypeId(kObjectOwnerTypeName),
                                               "ObjectOwner")
                    .field(makeFieldId(1), "child", &ObjectOwner::child)
                    .commit();
            !committed) {
            std::cerr << committed.error().message << '\n';
            return std::nullopt;
        }
        if (auto committed = CppBindingBuilder<UnsupportedArrayOwner>(
                                 bindings, makeTypeId(kUnsupportedArrayOwnerTypeName),
                                 "UnsupportedArrayOwner")
                                 .field(makeFieldId(1), "values", &UnsupportedArrayOwner::values)
                                 .commit();
            !committed) {
            std::cerr << committed.error().message << '\n';
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

    [[nodiscard]] asharia::archive::ArchiveValue makeVec3Archive(float x, float y, float z) {
        return asharia::archive::ArchiveValue::object({
            asharia::archive::ArchiveMember{.key = "x",
                                            .value = asharia::archive::ArchiveValue::floating(x)},
            asharia::archive::ArchiveMember{.key = "y",
                                            .value = asharia::archive::ArchiveValue::floating(y)},
            asharia::archive::ArchiveMember{.key = "z",
                                            .value = asharia::archive::ArchiveValue::floating(z)},
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

} // namespace

int main() {
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

    const UnsupportedArrayOwner unsupportedArrayOwner{.values = 1.0F};
    auto unsupportedArrayArchive = asharia::persistence::saveObject(
        *schemas, *bindings, asharia::schema::makeTypeId(kUnsupportedArrayOwnerTypeName),
        &unsupportedArrayOwner);
    if (unsupportedArrayArchive ||
        !contains(unsupportedArrayArchive.error().message, "not supported") ||
        !contains(unsupportedArrayArchive.error().message, "array")) {
        std::cerr << "Persistence accepted an unsupported array field kind.\n";
        return EXIT_FAILURE;
    }

    PropertyComponent propertySource;
    if (auto set = propertySource.setExposure(2.75F); !set) {
        std::cerr << set.error().message << '\n';
        return EXIT_FAILURE;
    }
    auto propertyArchive = asharia::persistence::saveObject(
        *schemas, *bindings, asharia::schema::makeTypeId(kPropertyTypeName), &propertySource);
    if (!propertyArchive) {
        std::cerr << propertyArchive.error().message << '\n';
        return EXIT_FAILURE;
    }
    PropertyComponent propertyLoaded;
    if (auto result = asharia::persistence::loadObject(
            *schemas, *bindings, asharia::schema::makeTypeId(kPropertyTypeName), *propertyArchive,
            &propertyLoaded);
        !result || propertyLoaded.exposure() != 2.75F || !propertyLoaded.dirty()) {
        std::cerr << "Persistence property roundtrip failed.\n";
        return EXIT_FAILURE;
    }

    asharia::archive::ArchiveValue unknownArchive = *archive;
    asharia::archive::ArchiveValue* unknownFields = unknownArchive.findMemberValue("fields");
    unknownFields->objectValue.push_back(asharia::archive::ArchiveMember{
        .key = "unknownFutureField",
        .value = asharia::archive::ArchiveValue::integer(7),
    });
    Transform rejectedUnknownObject{};
    auto rejectedUnknown = asharia::persistence::loadObject(
        *schemas, *bindings, asharia::schema::makeTypeId(kTransformTypeName), unknownArchive,
        &rejectedUnknownObject);
    if (rejectedUnknown || !contains(rejectedUnknown.error().message, "unknown field")) {
        std::cerr << "Persistence accepted an unknown field.\n";
        return EXIT_FAILURE;
    }

    Transform droppedUnknown{};
    const asharia::persistence::PersistencePolicy dropUnknownPolicy{
        .unknownFields = asharia::persistence::UnknownFieldPolicy::Drop,
    };
    if (auto result = asharia::persistence::loadObject(
            *schemas, *bindings, asharia::schema::makeTypeId(kTransformTypeName), unknownArchive,
            &droppedUnknown, dropUnknownPolicy);
        !result || droppedUnknown.position.x != source.position.x) {
        std::cerr << "Persistence did not drop an unknown field.\n";
        return EXIT_FAILURE;
    }

    Transform preserveUnknown{};
    const asharia::persistence::PersistencePolicy preservePolicy{
        .unknownFields = asharia::persistence::UnknownFieldPolicy::Preserve,
    };
    auto preserveResult = asharia::persistence::loadObject(
        *schemas, *bindings, asharia::schema::makeTypeId(kTransformTypeName), unknownArchive,
        &preserveUnknown, preservePolicy);
    if (preserveResult || !contains(preserveResult.error().message, "unsupported")) {
        std::cerr << "Persistence did not reject unsupported unknown-field preserve policy.\n";
        return EXIT_FAILURE;
    }

    asharia::archive::ArchiveValue missingScaleArchive = *archive;
    asharia::archive::ArchiveValue* missingScaleFields =
        missingScaleArchive.findMemberValue("fields");
    std::erase_if(
        missingScaleFields->objectValue,
        [](const asharia::archive::ArchiveMember& member) { return member.key == "scale"; });
    Transform defaultedScale{.scale = {.x = 8.0F, .y = 8.0F, .z = 8.0F}};
    if (auto result = asharia::persistence::loadObject(
            *schemas, *bindings, asharia::schema::makeTypeId(kTransformTypeName),
            missingScaleArchive, &defaultedScale);
        !result || defaultedScale.scale.x != 1.0F || defaultedScale.scale.y != 1.0F ||
        defaultedScale.scale.z != 1.0F) {
        std::cerr << "Persistence did not apply a missing field default.\n";
        return EXIT_FAILURE;
    }

    asharia::archive::ArchiveValue missingPositionArchive = *archive;
    asharia::archive::ArchiveValue* missingPositionFields =
        missingPositionArchive.findMemberValue("fields");
    std::erase_if(
        missingPositionFields->objectValue,
        [](const asharia::archive::ArchiveMember& member) { return member.key == "position"; });
    Transform missingPosition{};
    auto missingPositionResult = asharia::persistence::loadObject(
        *schemas, *bindings, asharia::schema::makeTypeId(kTransformTypeName),
        missingPositionArchive, &missingPosition);
    if (missingPositionResult || !contains(missingPositionResult.error().message, "default")) {
        std::cerr << "Persistence accepted a missing field without a default.\n";
        return EXIT_FAILURE;
    }

    asharia::persistence::MigrationRegistry migrations;
    if (auto registered = migrations.registerMigration(
            asharia::schema::makeTypeId(kMigratedTransformTypeName), 1, 2, migrateTransformV1ToV2);
        !registered) {
        std::cerr << registered.error().message << '\n';
        return EXIT_FAILURE;
    }
    auto duplicateMigration = migrations.registerMigration(
        asharia::schema::makeTypeId(kMigratedTransformTypeName), 1, 2, migrateTransformV1ToV2);
    if (duplicateMigration || !contains(duplicateMigration.error().message, "duplicate")) {
        std::cerr << "Persistence accepted a duplicate migration.\n";
        return EXIT_FAILURE;
    }

    const asharia::archive::ArchiveValue oldArchive = makeMigratedTransformV1Archive();
    MigratedTransform rejectedMigration{};
    auto missingMigrationResult = asharia::persistence::loadObject(
        *schemas, *bindings, asharia::schema::makeTypeId(kMigratedTransformTypeName), oldArchive,
        &rejectedMigration);
    if (missingMigrationResult ||
        !contains(missingMigrationResult.error().message, "requires migration")) {
        std::cerr << "Persistence did not require a migration policy.\n";
        return EXIT_FAILURE;
    }

    const asharia::persistence::PersistencePolicy migrationPolicy{
        .migrations = &migrations,
    };
    MigratedTransform migrated{};
    if (auto result = asharia::persistence::loadObject(
            *schemas, *bindings, asharia::schema::makeTypeId(kMigratedTransformTypeName),
            oldArchive, &migrated, migrationPolicy);
        !result || migrated.position.x != 1.0F || migrated.position.y != 2.0F ||
        migrated.position.z != 3.0F || migrated.rotation.z != 0.25F ||
        migrated.rotation.w != 1.0F || migrated.scale.x != 1.0F ||
        migrated.debugName != "migrated") {
        std::cerr << "Persistence migration did not load expected values.\n";
        return EXIT_FAILURE;
    }

    std::cout << "Persistence roundtrip fields: " << fields->objectValue.size() << '\n';
    return EXIT_SUCCESS;
}
