#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asharia/schema/schema_document.hpp"
#include "asharia/schema/schema_registry.hpp"

namespace {

    constexpr std::string_view kVec3TypeName = "com.asharia.schemaSmoke.Vec3";
    constexpr std::string_view kTransformTypeName = "com.asharia.schemaSmoke.Transform";
    constexpr std::string_view kDeferredOwnerTypeName = "com.asharia.schemaSmoke.DeferredOwner";
    constexpr std::string_view kDeferredValueTypeName = "com.asharia.schemaSmoke.DeferredValue";
    constexpr std::string_view kAliasedStableTypeName = "com.asharia.schemaSmoke.AliasedStable";
    constexpr std::string_view kAliasedCanonicalTypeName =
        "com.asharia.schemaSmoke.AliasedCanonical";
    constexpr std::string_view kGoldenVec3TypeName = "com.asharia.schemaGolden.Vec3";
    constexpr std::string_view kGoldenTransformTypeName = "com.asharia.schemaGolden.Transform";
    constexpr std::string_view kGoldenTransformCanonicalName =
        "com.asharia.schemaGolden.TransformComponent";

    [[nodiscard]] bool contains(std::string_view text, std::string_view needle) {
        return text.find(needle) != std::string_view::npos;
    }

    [[nodiscard]] asharia::schema::FieldSchema persistentFloat(std::uint32_t fieldId,
                                                               std::string_view key) {
        return asharia::schema::FieldSchema{
            .id = asharia::schema::makeFieldId(fieldId),
            .key = std::string{key},
            .valueType = asharia::schema::builtin::floatTypeId(),
            .valueKind = asharia::schema::ValueKind::Float,
            .aliases = {},
            .metadata =
                {
                    .persistence = {.stored = true, .required = true, .hasDefault = false},
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
        asharia::schema::FieldSchema runtimeOnly{
            .id = asharia::schema::makeFieldId(2),
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
                    .script = {.visible = false,
                               .read = false,
                               .write = false,
                               .context = {},
                               .threadAffinity = {},
                               .lifetime = {}},
                    .numeric = {},
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

    [[nodiscard]] const asharia::schema::TypeSchema*
    findLoadedType(const std::vector<asharia::schema::TypeSchema>& types,
                   std::string_view stableName) {
        for (const asharia::schema::TypeSchema& type : types) {
            if (type.id.stableName == stableName) {
                return &type;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::uint64_t fixtureByteCount(const std::filesystem::path& fixturePath) {
        std::ifstream fixture{fixturePath, std::ios::binary | std::ios::ate};
        if (!fixture) {
            throw std::runtime_error{"Could not open the schema document fixture."};
        }
        const std::streampos byteCount = fixture.tellg();
        if (byteCount < 0) {
            throw std::runtime_error{"Could not measure the schema document fixture."};
        }
        return static_cast<std::uint64_t>(byteCount);
    }

    [[nodiscard]] bool expectGoldenSchemaDocument() {
        const std::filesystem::path fixturePath =
            std::filesystem::path{ASHARIA_SCHEMA_TEST_FIXTURE_DIR} / "schema_golden.json";
        auto loaded = asharia::schema::readSchemaDocumentFile(fixturePath);
        if (!loaded) {
            std::cerr << loaded.error().message << '\n';
            return false;
        }
        if (loaded->size() != 2U) {
            std::cerr << "Schema document loaded an unexpected type count.\n";
            return false;
        }

        const std::uint64_t fixtureBytes = fixtureByteCount(fixturePath);
        auto exactLimit =
            asharia::schema::readSchemaDocumentFile(fixturePath, {.maxBytes = fixtureBytes});
        if (!exactLimit) {
            std::cerr << "Schema document rejected a file at the exact byte limit.\n";
            return false;
        }
        auto oversized =
            asharia::schema::readSchemaDocumentFile(fixturePath, {.maxBytes = fixtureBytes - 1U});
        if (oversized ||
            !contains(oversized.error().message, "observedBytes=" + std::to_string(fixtureBytes)) ||
            !contains(oversized.error().message, "maxBytes=" + std::to_string(fixtureBytes - 1U))) {
            std::cerr << "Schema document accepted a file above the byte limit.\n";
            return false;
        }

        const asharia::schema::TypeSchema* vec3 = findLoadedType(*loaded, kGoldenVec3TypeName);
        if (vec3 == nullptr || vec3->version != 2U ||
            vec3->kind != asharia::schema::ValueKind::InlineStruct ||
            vec3->metadata.editor.displayName != "Vector 3" || !vec3->metadata.script.visible) {
            std::cerr << "Schema document did not preserve Vec3 type metadata.\n";
            return false;
        }

        const asharia::schema::FieldSchema* xField = asharia::schema::findFieldByKey(*vec3, "x");
        if (xField == nullptr || xField->id.value != 1U || xField->aliases.size() != 1U ||
            xField->aliases.front() != "oldX" || !xField->metadata.persistence.stored ||
            !xField->metadata.numeric.hasMin || !xField->metadata.numeric.hasMax ||
            xField->metadata.numeric.min != -1000.0 || xField->metadata.numeric.max != 1000.0 ||
            xField->metadata.numeric.step != 0.01 || xField->metadata.numeric.unit != "meter") {
            std::cerr << "Schema document did not preserve Vec3 field metadata.\n";
            return false;
        }

        const asharia::schema::TypeSchema* transform =
            findLoadedType(*loaded, kGoldenTransformTypeName);
        if (transform == nullptr || transform->canonicalName != kGoldenTransformCanonicalName ||
            transform->version != 3U || transform->reservedFieldIds.size() != 2U ||
            transform->reservedFieldIds[0].value != 77U ||
            transform->reservedFieldIds[1].value != 99U ||
            transform->metadata.editor.category != "Scene") {
            std::cerr << "Schema document did not preserve Transform type metadata.\n";
            return false;
        }

        const asharia::schema::FieldSchema* position =
            asharia::schema::findFieldByKeyOrAlias(*transform, "translation");
        if (position == nullptr || position->key != "position" ||
            position->valueType.stableName != kGoldenVec3TypeName ||
            position->valueKind != asharia::schema::ValueKind::InlineStruct ||
            !position->metadata.persistence.stored ||
            position->metadata.editor.displayName != "Position" ||
            !position->metadata.script.write) {
            std::cerr << "Schema document did not preserve Transform field aliases.\n";
            return false;
        }

        asharia::schema::SchemaRegistry loadedRegistry;
        if (auto registered = asharia::schema::registerBuiltinSchemas(loadedRegistry);
            !registered) {
            std::cerr << registered.error().message << '\n';
            return false;
        }
        for (asharia::schema::TypeSchema type : *loaded) {
            if (auto registered = loadedRegistry.registerType(std::move(type)); !registered) {
                std::cerr << registered.error().message << '\n';
                return false;
            }
        }
        if (auto frozen = loadedRegistry.freeze(); !frozen) {
            std::cerr << frozen.error().message << '\n';
            return false;
        }
        if (loadedRegistry.findType(kGoldenTransformCanonicalName) == nullptr) {
            std::cerr << "Schema registry could not find loaded canonical type name.\n";
            return false;
        }

        auto duplicateKey = asharia::schema::readSchemaDocument(
            R"json({"schemaVersion":1,"types":[],"types":[]})json");
        if (duplicateKey || !contains(duplicateKey.error().message, "duplicate key")) {
            std::cerr << "Schema document accepted duplicate JSON object keys.\n";
            return false;
        }

        auto badKind = asharia::schema::readSchemaDocument(
            R"json({"schemaVersion":1,"types":[{"stableName":"com.asharia.Bad","canonicalName":"com.asharia.Bad","version":1,"kind":"Matrix"}]})json");
        if (badKind || !contains(badKind.error().message, "value kind")) {
            std::cerr << "Schema document accepted an unknown value kind.\n";
            return false;
        }

        auto unknownMember = asharia::schema::readSchemaDocument(
            R"json({"schemaVersion":1,"types":[{"stableName":"com.asharia.Bad","canonicalName":"com.asharia.Bad","version":1,"kind":"Object","metadata":{"editor":{"unknown":true}}}]})json");
        if (unknownMember || !contains(unknownMember.error().message, "unknown member")) {
            std::cerr << "Schema document accepted an unknown metadata member.\n";
            return false;
        }

        return true;
    }

    [[nodiscard]] int runSchemaSmokeTests() {
        if (!expectGoldenSchemaDocument()) {
            return EXIT_FAILURE;
        }

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
                .fields = {},
                .reservedFieldIds = {},
                .metadata = {},
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
            .fields = {},
            .reservedFieldIds = {},
            .metadata = {},
        });
        if (stableCanonicalCollision ||
            !contains(stableCanonicalCollision.error().message, "duplicate")) {
            std::cerr
                << "Schema registry accepted a stable id that collides with a canonical name.\n";
            return EXIT_FAILURE;
        }

        auto canonicalStableCollision = registry.registerType(asharia::schema::TypeSchema{
            .id = asharia::schema::makeTypeId("com.asharia.schemaSmoke.OtherStable"),
            .canonicalName = std::string{kAliasedStableTypeName},
            .version = 1,
            .kind = asharia::schema::ValueKind::Object,
            .fields = {},
            .reservedFieldIds = {},
            .metadata = {},
        });
        if (canonicalStableCollision ||
            !contains(canonicalStableCollision.error().message, "duplicate")) {
            std::cerr
                << "Schema registry accepted a canonical name that collides with a stable id.\n";
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
        if (auto registered = registry.registerType(std::move(deferredOwner)); !registered) {
            std::cerr << registered.error().message << '\n';
            return EXIT_FAILURE;
        }

        auto missingReference = registry.freeze();
        if (missingReference ||
            !contains(missingReference.error().message, "registered field type")) {
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
            .fields = {},
            .reservedFieldIds = {},
            .metadata = {},
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
        if (persistent.fields.size() != 1 || editor.fields.size() != 2 ||
            script.fields.size() != 1) {
            std::cerr << "Schema projections returned unexpected field counts.\n";
            return EXIT_FAILURE;
        }

        std::cout << "Schema registry types: " << registry.types().size() << '\n';
        return EXIT_SUCCESS;
    }

} // namespace

// The exhaustive catch boundary converts all failures to the smoke-test exit protocol.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() noexcept {
    try {
        return runSchemaSmokeTests();
    } catch (const std::exception& error) {
        std::cerr << "Schema smoke test threw: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "Schema smoke test threw an unknown exception.\n";
    }
    return EXIT_FAILURE;
}
