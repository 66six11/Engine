#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "process_scope_synthetic_provider.hpp"
#include "process_scope_test_support.hpp"

namespace asharia::host_runtime::tests {
    namespace {

        [[nodiscard]] ExactFactoryReferenceV2 syntheticReference(SyntheticFactoryV1 factory) {
            return {
                .packageId = std::string(kSyntheticPackageId),
                .packageVersion = std::string(kSyntheticPackageVersion),
                .moduleId = std::string(kSyntheticModuleId),
                .factoryId = std::string(syntheticFactoryId(factory)),
            };
        }

        [[nodiscard]] std::optional<ExactFactoryReferenceV2>
        expectedFactoryAttribution(ProcessPlanMutationV1 mutation) {
            ExactFactoryReferenceV2 middle = syntheticReference(SyntheticFactoryV1::Middle);
            switch (mutation) {
            case ProcessPlanMutationV1::PackageIdentityMismatch:
                middle.packageId = "com.asharia.test.other";
                return middle;
            case ProcessPlanMutationV1::VersionIdentityMismatch:
                middle.packageVersion = "2.0.0";
                return middle;
            case ProcessPlanMutationV1::ModuleIdentityMismatch:
                middle.moduleId = "other";
                return middle;
            case ProcessPlanMutationV1::FactoryIdentityMismatch:
                middle.factoryId = "missing";
                return middle;
            case ProcessPlanMutationV1::DuplicateFactory:
                return syntheticReference(SyntheticFactoryV1::Root);
            case ProcessPlanMutationV1::DuplicateRequirement:
            case ProcessPlanMutationV1::MissingRequirement:
            case ProcessPlanMutationV1::ForwardRequirement:
                return middle;
            case ProcessPlanMutationV1::None:
            case ProcessPlanMutationV1::Empty:
            case ProcessPlanMutationV1::NonProcessScope:
            case ProcessPlanMutationV1::ParentedProcessScope:
            case ProcessPlanMutationV1::EngineGenerationMismatch:
            case ProcessPlanMutationV1::BlueprintMismatch:
            case ProcessPlanMutationV1::LifecycleModelMismatch:
            case ProcessPlanMutationV1::IncludeProjectOnlySingleConflict:
            case ProcessPlanMutationV1::IncludeZeroContributionFactory:
                return std::nullopt;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ExactFactoryReferenceV2>
        expectedRequirementAttribution(ProcessPlanMutationV1 mutation) {
            ExactFactoryReferenceV2 root = syntheticReference(SyntheticFactoryV1::Root);
            switch (mutation) {
            case ProcessPlanMutationV1::DuplicateRequirement:
            case ProcessPlanMutationV1::ForwardRequirement:
                return root;
            case ProcessPlanMutationV1::MissingRequirement:
                root.factoryId = "missing";
                return root;
            case ProcessPlanMutationV1::None:
            case ProcessPlanMutationV1::Empty:
            case ProcessPlanMutationV1::NonProcessScope:
            case ProcessPlanMutationV1::ParentedProcessScope:
            case ProcessPlanMutationV1::EngineGenerationMismatch:
            case ProcessPlanMutationV1::BlueprintMismatch:
            case ProcessPlanMutationV1::LifecycleModelMismatch:
            case ProcessPlanMutationV1::PackageIdentityMismatch:
            case ProcessPlanMutationV1::VersionIdentityMismatch:
            case ProcessPlanMutationV1::ModuleIdentityMismatch:
            case ProcessPlanMutationV1::FactoryIdentityMismatch:
            case ProcessPlanMutationV1::DuplicateFactory:
            case ProcessPlanMutationV1::IncludeProjectOnlySingleConflict:
            case ProcessPlanMutationV1::IncludeZeroContributionFactory:
                return std::nullopt;
            }
            return std::nullopt;
        }

        [[nodiscard]] bool invalidProjectionMatrixFailsBeforeCallbacks() {
            struct Case final {
                ProcessPlanMutationV1 mutation;
                ProcessScopeErrorCodeV2 expectedCode;
            };
            constexpr std::array cases{
                Case{
                    .mutation = ProcessPlanMutationV1::NonProcessScope,
                    .expectedCode = ProcessScopeErrorCodeV2::ProcessScopeExpected,
                },
                Case{
                    .mutation = ProcessPlanMutationV1::ParentedProcessScope,
                    .expectedCode = ProcessScopeErrorCodeV2::ParentScopeInvalid,
                },
                Case{
                    .mutation = ProcessPlanMutationV1::EngineGenerationMismatch,
                    .expectedCode = ProcessScopeErrorCodeV2::EngineGenerationMismatch,
                },
                Case{
                    .mutation = ProcessPlanMutationV1::BlueprintMismatch,
                    .expectedCode = ProcessScopeErrorCodeV2::BlueprintMismatch,
                },
                Case{
                    .mutation = ProcessPlanMutationV1::LifecycleModelMismatch,
                    .expectedCode = ProcessScopeErrorCodeV2::LifecycleModelMismatch,
                },
                Case{
                    .mutation = ProcessPlanMutationV1::PackageIdentityMismatch,
                    .expectedCode = ProcessScopeErrorCodeV2::DescriptorMissing,
                },
                Case{
                    .mutation = ProcessPlanMutationV1::VersionIdentityMismatch,
                    .expectedCode = ProcessScopeErrorCodeV2::DescriptorMissing,
                },
                Case{
                    .mutation = ProcessPlanMutationV1::ModuleIdentityMismatch,
                    .expectedCode = ProcessScopeErrorCodeV2::DescriptorMissing,
                },
                Case{
                    .mutation = ProcessPlanMutationV1::FactoryIdentityMismatch,
                    .expectedCode = ProcessScopeErrorCodeV2::DescriptorMissing,
                },
                Case{
                    .mutation = ProcessPlanMutationV1::DuplicateFactory,
                    .expectedCode = ProcessScopeErrorCodeV2::FactoryDuplicate,
                },
                Case{
                    .mutation = ProcessPlanMutationV1::DuplicateRequirement,
                    .expectedCode = ProcessScopeErrorCodeV2::RequirementDuplicate,
                },
                Case{
                    .mutation = ProcessPlanMutationV1::MissingRequirement,
                    .expectedCode = ProcessScopeErrorCodeV2::RequirementMissing,
                },
                Case{
                    .mutation = ProcessPlanMutationV1::ForwardRequirement,
                    .expectedCode = ProcessScopeErrorCodeV2::RequirementOrderInvalid,
                },
            };

            return std::ranges::all_of(cases, [](const Case& testCase) {
                resetSyntheticProviderFixture();
                auto admitted = makeAdmittedSyntheticProcessScope(testCase.mutation);
                if (!admitted) {
                    return false;
                }
                const auto prepared = prepareProcessScopeExecutorV2(std::move(*admitted));
                const auto expectedFactory = expectedFactoryAttribution(testCase.mutation);
                const auto expectedRequirement = expectedRequirementAttribution(testCase.mutation);
                return !prepared && prepared.error().code == testCase.expectedCode &&
                       prepared.error().factory == expectedFactory &&
                       prepared.error().requirement == expectedRequirement &&
                       syntheticProviderTrace().empty() &&
                       syntheticProjectOnlyInvocationCount() == 0;
            });
        }

        // NOLINTBEGIN(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        [[nodiscard]] bool movedAndStaleAdmissionFailBeforeCallbacks() {
            resetSyntheticProviderFixture();
            auto admitted = makeAdmittedSyntheticProcessScope();
            if (!admitted) {
                return false;
            }
            [[maybe_unused]] AdmittedStaticFactoryCallbackTableV1 retained = std::move(*admitted);
            const auto moved = prepareProcessScopeExecutorV2(std::move(*admitted));
            if (moved || moved.error().code != ProcessScopeErrorCodeV2::AdmissionMovedFrom ||
                !syntheticProviderTrace().empty()) {
                return false;
            }

            auto stale = makeAdmittedSyntheticProcessScope();
            if (!stale) {
                return false;
            }
            rebindSyntheticCurrentProcessEpoch();
            const auto stalePrepared = prepareProcessScopeExecutorV2(std::move(*stale));
            return !stalePrepared &&
                   stalePrepared.error().code == ProcessScopeErrorCodeV2::ProcessEpochStale &&
                   syntheticProviderTrace().empty();
        }
        // NOLINTEND(bugprone-use-after-move,clang-analyzer-cplusplus.Move)

        [[nodiscard]] bool wrongThreadPreparationFailsBeforeCallbacks() {
            resetSyntheticProviderFixture();
            auto admitted = makeAdmittedSyntheticProcessScope();
            if (!admitted) {
                return false;
            }
            bool rejected = false;
            ProcessScopeErrorCodeV2 code{};
            std::jthread worker([admitted = std::move(*admitted), &rejected, &code]() mutable {
                const auto prepared = prepareProcessScopeExecutorV2(std::move(admitted));
                rejected = !prepared;
                if (!prepared) {
                    code = prepared.error().code;
                }
            });
            worker.join();
            return rejected && code == ProcessScopeErrorCodeV2::WrongControlThread &&
                   syntheticProviderTrace().empty();
        }

    } // namespace

    std::span<const NamedProcessScopeTestV1> processScopePreflightTests() noexcept {
        static constexpr std::array tests{
            NamedProcessScopeTestV1{
                .name = "invalid projection matrix fails before callbacks",
                .function = &invalidProjectionMatrixFailsBeforeCallbacks,
            },
            NamedProcessScopeTestV1{
                .name = "moved and stale admission fail before callbacks",
                .function = &movedAndStaleAdmissionFailBeforeCallbacks,
            },
            NamedProcessScopeTestV1{
                .name = "wrong-thread preparation fails before callbacks",
                .function = &wrongThreadPreparationFailsBeforeCallbacks,
            },
        };
        return tests;
    }

} // namespace asharia::host_runtime::tests
