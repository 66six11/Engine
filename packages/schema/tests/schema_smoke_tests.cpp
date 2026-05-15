#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "asharia/schema/schema_registry.hpp"

namespace {

    constexpr std::string_view kVec3TypeName = "com.asharia.schemaSmoke.Vec3";
    constexpr std::string_view kTransformTypeName = "com.asharia.schemaSmoke.Transform";
    constexpr std::string_view kDeferredOwnerTypeName = "com.asharia.schemaSmoke.DeferredOwner";
    constexpr std::string_view kDeferredValueTypeName = "com.asharia.schemaSmoke.DeferredValue";
    constexpr std::string_view kAliasedStableTypeName = "com.asharia.schemaSmoke.AliasedStable";
    constexpr std::string_view kAliasedCanonicalTypeName =
        "com.asharia.schemaSmoke.AliasedCanonical";

    [[nodiscard]] bool contains(std::string_view text, std::string_view needle) {
        return text.find(needle) != std::string_view::npos;
    }

    [[nodiscard]] asharia::schema::FieldSchema persistentFloat(std::uint32_t id,
                                                               std::string_view key) {
        return asharia::schema::FieldSchema{
            .id = asharia::schema::makeFieldId(id),
            .key = std::string{key},
            .valueType = asharia::schema::builtin::floatTypeId(),
            .valueKind = asharia::schema::ValueKind::Float,
            .aliases = {},
            .metadata =
                {
                    .persistence = {.stored = true, .required = true, .hasDefault = false},
                    .editor = {.visible = true},
                    .script = {.visible = true, .read = true, .write = true},
                },
        };
    }

    [[nodiscard]] asharia::schema::TypeSchema makeVec3Schema() {
        return asharia::schema::TypeSchema{
            .id = asharia::schema::makeTypeId(kVec3TypeName),
            .canonicalName = std::string{kVec3TypeName},
            .version = 1,
            .kind = asharia::schema::ValueKind::InlineStruct,
            .fields =
                {
                    persistentFloat(1, "x"),
                    persistentFloat(2, "y"),
                    persistentFloat(3, "z"),
                },
            .reservedFieldIds = {},
            .metadata = {},
        };
    }

    [[nodiscard]] asharia::schema::TypeSchema makeTransformSchema() {
        asharia::schema::FieldSchema position{
            .id = asharia::schema::makeFieldId(1),
            .key = "position",
            .valueType = asharia::schema::makeTypeId(kVec3TypeName),
            .valueKind = asharia::schema::ValueKind::InlineStruct,
            .aliases = {"translation"},
            .metadata =
                {
                    .persistence = {.stored = true, .required = true, .hasDefault = false},
                    .editor = {.visible = true},
                    .script = {.visible = true, .read = true, .write = true},
                },
        };
        asharia::schema::FieldSchema runtimeOnly{
            .id = asharia::schema::makeFieldId(2),
            .key = "cachedMagnitude",
            .valueType = asharia::schema::builtin::floatTypeId(),
            .valueKind = asharia::schema::ValueKind::Float,
            .aliases = {},
            .metadata =
                {
                    .persistence = {.stored = false},
                    .editor = {.visible = true, .readOnly = true},
                    .script = {.visible = false},
                },
        };

        return asharia::schema::TypeSchema{
            .id = asharia::schema::makeTypeId(kTransformTypeName),
            .canonicalName = std::string{kTransformTypeName},
            .version = 1,
            .kind = asharia::schema::ValueKind::Object,
            .fields = {std::move(position), std::move(runtimeOnly)},
            .reservedFieldIds = {asharia::schema::makeFieldId(99)},
            .metadata = {},
        };
    }

} // namespace

int main() {
    asharia::schema::SchemaRegistry registry;
    if (auto registered = asharia::schema::registerBuiltinSchemas(registry); !registered) {
        std::cerr << registered.error().message << '\n';
        return EXIT_FAILURE;
    }

    if (auto registered = registry.registerType(makeVec3Schema()); !registered) {
        std::cerr << registered.error().message << '\n';
        return EXIT_FAILURE;
    }
    if (auto registered = registry.registerType(makeTransformSchema()); !registered) {
        std::cerr << registered.error().message << '\n';
        return EXIT_FAILURE;
    }

    if (auto registered = registry.registerType(asharia::schema::TypeSchema{
            .id = asharia::schema::makeTypeId(kAliasedStableTypeName),
            .canonicalName = std::string{kAliasedCanonicalTypeName},
            .version = 1,
            .kind = asharia::schema::ValueKind::Object,
        });
        !registered) {
        std::cerr << registered.error().message << '\n';
        return EXIT_FAILURE;
    }

    auto duplicate = registry.registerType(makeTransformSchema());
    if (duplicate || !contains(duplicate.error().message, "duplicate")) {
        std::cerr << "Schema registry accepted a duplicate type.\n";
        return EXIT_FAILURE;
    }

    auto stableCanonicalCollision = registry.registerType(asharia::schema::TypeSchema{
        .id = asharia::schema::makeTypeId(kAliasedCanonicalTypeName),
        .canonicalName = "com.asharia.schemaSmoke.OtherCanonical",
        .version = 1,
        .kind = asharia::schema::ValueKind::Object,
    });
    if (stableCanonicalCollision ||
        !contains(stableCanonicalCollision.error().message, "duplicate")) {
        std::cerr << "Schema registry accepted a stable id that collides with a canonical name.\n";
        return EXIT_FAILURE;
    }

    auto canonicalStableCollision = registry.registerType(asharia::schema::TypeSchema{
        .id = asharia::schema::makeTypeId("com.asharia.schemaSmoke.OtherStable"),
        .canonicalName = std::string{kAliasedStableTypeName},
        .version = 1,
        .kind = asharia::schema::ValueKind::Object,
    });
    if (canonicalStableCollision ||
        !contains(canonicalStableCollision.error().message, "duplicate")) {
        std::cerr << "Schema registry accepted a canonical name that collides with a stable id.\n";
        return EXIT_FAILURE;
    }

    asharia::schema::TypeSchema badReserved = makeTransformSchema();
    badReserved.id = asharia::schema::makeTypeId("com.asharia.schemaSmoke.BadReserved");
    badReserved.canonicalName = badReserved.id.stableName;
    badReserved.fields.front().id = asharia::schema::makeFieldId(99);
    auto reserved = registry.registerType(std::move(badReserved));
    if (reserved || !contains(reserved.error().message, "reserved field id")) {
        std::cerr << "Schema registry accepted reserved field id reuse.\n";
        return EXIT_FAILURE;
    }

    asharia::schema::TypeSchema deferredOwner{
        .id = asharia::schema::makeTypeId(kDeferredOwnerTypeName),
        .canonicalName = std::string{kDeferredOwnerTypeName},
        .version = 1,
        .kind = asharia::schema::ValueKind::Object,
        .fields =
            {
                asharia::schema::FieldSchema{
                    .id = asharia::schema::makeFieldId(1),
                    .key = "deferred",
                    .valueType = asharia::schema::makeTypeId(kDeferredValueTypeName),
                    .valueKind = asharia::schema::ValueKind::InlineStruct,
                    .aliases = {},
                    .metadata = {.persistence = {.stored = true}},
                },
            },
        .reservedFieldIds = {},
        .metadata = {},
    };
    if (auto registered = registry.registerType(std::move(deferredOwner)); !registered) {
        std::cerr << registered.error().message << '\n';
        return EXIT_FAILURE;
    }

    auto missingReference = registry.freeze();
    if (missingReference || !contains(missingReference.error().message, "registered field type")) {
        std::cerr << "Schema registry accepted a missing field type reference.\n";
        return EXIT_FAILURE;
    }

    asharia::schema::TypeSchema deferredValue{
        .id = asharia::schema::makeTypeId(kDeferredValueTypeName),
        .canonicalName = std::string{kDeferredValueTypeName},
        .version = 1,
        .kind = asharia::schema::ValueKind::InlineStruct,
        .fields = {persistentFloat(1, "value")},
        .reservedFieldIds = {},
        .metadata = {},
    };
    if (auto registered = registry.registerType(std::move(deferredValue)); !registered) {
        std::cerr << registered.error().message << '\n';
        return EXIT_FAILURE;
    }

    if (auto frozen = registry.freeze(); !frozen) {
        std::cerr << frozen.error().message << '\n';
        return EXIT_FAILURE;
    }

    auto late = registry.registerType(asharia::schema::TypeSchema{
        .id = asharia::schema::makeTypeId("com.asharia.schemaSmoke.Late"),
        .canonicalName = "com.asharia.schemaSmoke.Late",
        .version = 1,
        .kind = asharia::schema::ValueKind::Object,
    });
    if (late || !contains(late.error().message, "frozen")) {
        std::cerr << "Schema registry accepted registration after freeze.\n";
        return EXIT_FAILURE;
    }

    const asharia::schema::TypeSchema* transform =
        registry.findType(asharia::schema::makeTypeId(kTransformTypeName));
    if (transform == nullptr) {
        std::cerr << "Schema registry could not find transform schema.\n";
        return EXIT_FAILURE;
    }

    const asharia::schema::FieldProjection persistent =
        asharia::schema::makePersistenceProjection(*transform);
    const asharia::schema::FieldProjection editor =
        asharia::schema::makeEditorProjection(*transform);
    const asharia::schema::FieldProjection script =
        asharia::schema::makeScriptProjection(*transform);
    if (persistent.fields.size() != 1 || editor.fields.size() != 2 || script.fields.size() != 1) {
        std::cerr << "Schema projections returned unexpected field counts.\n";
        return EXIT_FAILURE;
    }

    std::cout << "Schema registry types: " << registry.types().size() << '\n';
    return EXIT_SUCCESS;
}
