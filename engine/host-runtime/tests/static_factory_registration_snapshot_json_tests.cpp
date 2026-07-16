#include <array>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/host_runtime/static_factory_registration_snapshot_json.hpp"

namespace {

    using asharia::host_runtime::StaticContributionCardinalityV1;
    using asharia::host_runtime::StaticContributionRegistrationV2;
    using asharia::host_runtime::StaticFactoryRegistrationSnapshotV2;
    using asharia::host_runtime::StaticFactoryRegistrationV2;

    [[nodiscard]] bool emptySnapshotUsesCanonicalShape() {
        const StaticFactoryRegistrationSnapshotV2 snapshot{
            .generationId = "sha256-empty",
            .hostActivationBlueprintSha256 = "blueprint-empty",
            .registrations = {},
        };
        const auto rendered =
            asharia::host_runtime::renderStaticFactoryRegistrationSnapshotJson(snapshot);
        constexpr std::string_view expected =
            "{\n"
            "  \"schema\": \"com.asharia.static-factory-registration-snapshot\",\n"
            "  \"schemaVersion\": 2,\n"
            "  \"generationId\": \"sha256-empty\",\n"
            "  \"hostActivationBlueprintSha256\": \"blueprint-empty\",\n"
            "  \"registrations\": []\n"
            "}\n";
        return rendered && *rendered == expected;
    }

    [[nodiscard]] bool stringsAreEscapedAndUtf8IsPreserved() {
        std::string factoryId{"factory\"\\\b\f\n\r\t"};
        factoryId.push_back('\x01');
        factoryId += "/结束";
        const StaticFactoryRegistrationSnapshotV2 snapshot{
            .generationId = "sha256-代际",
            .hostActivationBlueprintSha256 = "blueprint",
            .registrations =
                {
                    StaticFactoryRegistrationV2{
                        .packageId = "com.asharia.包",
                        .packageVersion = "1.0.0",
                        .moduleId = "runtime",
                        .factoryId = std::move(factoryId),
                        .providerEntryPoint = "asharia::test::provide",
                        .contributions = {},
                    },
                },
        };
        const auto rendered =
            asharia::host_runtime::renderStaticFactoryRegistrationSnapshotJson(snapshot);
        constexpr std::string_view expected =
            "{\n"
            "  \"schema\": \"com.asharia.static-factory-registration-snapshot\",\n"
            "  \"schemaVersion\": 2,\n"
            "  \"generationId\": \"sha256-代际\",\n"
            "  \"hostActivationBlueprintSha256\": \"blueprint\",\n"
            "  \"registrations\": [\n"
            "    {\n"
            "      \"packageId\": \"com.asharia.包\",\n"
            "      \"packageVersion\": \"1.0.0\",\n"
            "      \"moduleId\": \"runtime\",\n"
            "      \"factoryId\": \"factory\\\"\\\\\\b\\f\\n\\r\\t\\u0001/结束\",\n"
            "      \"providerEntryPoint\": \"asharia::test::provide\",\n"
            "      \"contributions\": []\n"
            "    }\n"
            "  ]\n"
            "}\n";
        return rendered && *rendered == expected;
    }

    [[nodiscard]] bool registrationsKeepSnapshotOrder() {
        const StaticFactoryRegistrationSnapshotV2 snapshot{
            .generationId = "sha256-order",
            .hostActivationBlueprintSha256 = "blueprint",
            .registrations =
                {
                    StaticFactoryRegistrationV2{
                        .packageId = "a",
                        .packageVersion = "1",
                        .moduleId = "runtime",
                        .factoryId = "first",
                        .providerEntryPoint = "provideA",
                        .contributions =
                            {
                                StaticContributionRegistrationV2{
                                    .contributionId = "a.default",
                                    .contributionKind = "a.service",
                                    .cardinality = StaticContributionCardinalityV1::Single,
                                },
                            },
                    },
                    StaticFactoryRegistrationV2{
                        .packageId = "b",
                        .packageVersion = "1",
                        .moduleId = "tools",
                        .factoryId = "second",
                        .providerEntryPoint = "provideB",
                        .contributions =
                            {
                                StaticContributionRegistrationV2{
                                    .contributionId = "b.default",
                                    .contributionKind = "b.service",
                                    .cardinality = StaticContributionCardinalityV1::Multiple,
                                },
                            },
                    },
                },
        };
        const auto rendered =
            asharia::host_runtime::renderStaticFactoryRegistrationSnapshotJson(snapshot);
        if (!rendered || rendered->empty() || rendered->back() != '\n') {
            return false;
        }
        const std::size_t first = rendered->find(R"("factoryId": "first")");
        const std::size_t second = rendered->find(R"("factoryId": "second")");
        return first != std::string::npos && second != std::string::npos && first < second &&
               rendered->find(R"("cardinality": "single")") != std::string::npos &&
               rendered->find(R"("cardinality": "multiple")") != std::string::npos &&
               rendered->find("typeKey") == std::string::npos;
    }

} // namespace

int main() {
    using Test = bool (*)();
    constexpr std::array<Test, 3> tests{
        &emptySnapshotUsesCanonicalShape,
        &stringsAreEscapedAndUtf8IsPreserved,
        &registrationsKeepSnapshotOrder,
    };

    for (const Test test : tests) {
        if (!test()) {
            return 1;
        }
    }
    return 0;
}
