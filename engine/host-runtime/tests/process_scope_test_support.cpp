#include "process_scope_test_support.hpp"

#include <memory>
#include <string>
#include <utility>

#include "activation_eligibility_state.hpp"
#include "process_scope_synthetic_provider.hpp"

namespace asharia::host_runtime::tests {
    namespace {

        constexpr std::string_view kProviderApi{"asharia-static-factory-provider-v4"};
        constexpr std::string_view kLifecycleModel{"create-activate-quiesce-deactivate-destroy-v1"};

        [[nodiscard]] std::string digest(char character) {
            // NOLINTNEXTLINE(modernize-return-braced-init-list)
            return std::string(64, character);
        }

        [[nodiscard]] std::string generationId(char character) {
            return "sha256-" + digest(character);
        }

        [[nodiscard]] ExactHostIdentityStateV2 hostIdentity() {
            return {
                .engineGenerationId = std::string(kSyntheticEngineGenerationId),
                .hostKind = "runtime",
                .targetPlatform = "windows-x86_64",
            };
        }

        [[nodiscard]] ExactFactoryReferenceStateV1 factoryReference(std::string_view factoryId) {
            return {
                .packageId = std::string(kSyntheticPackageId),
                .packageVersion = std::string(kSyntheticPackageVersion),
                .moduleId = std::string(kSyntheticModuleId),
                .factoryId = std::string(factoryId),
            };
        }

        [[nodiscard]] ProcessScopeBlueprintProjectionStateV1 processProjection() {
            const ExactFactoryReferenceStateV1 root = factoryReference(kSyntheticRootFactoryId);
            const ExactFactoryReferenceStateV1 middle = factoryReference(kSyntheticMiddleFactoryId);
            const ExactFactoryReferenceStateV1 leaf = factoryReference(kSyntheticLeafFactoryId);
            return {
                .scope = HostScopeKindStateV1::Process,
                .parentScope = std::nullopt,
                .engineGenerationId = std::string(kSyntheticEngineGenerationId),
                .blueprintIntegrity = std::string(kSyntheticBlueprintSha256),
                .lifecycleModel = std::string(kLifecycleModel),
                .factories =
                    {
                        {
                            .reference = root,
                            .requirements = {},
                        },
                        {
                            .reference = middle,
                            .requirements = {root},
                        },
                        {
                            .reference = leaf,
                            .requirements = {root, middle},
                        },
                    },
            };
        }

        void mutateProjection(ProcessScopeBlueprintProjectionStateV1& projection,
                              ProcessPlanMutationV1 mutation) {
            switch (mutation) {
            case ProcessPlanMutationV1::None:
                break;
            case ProcessPlanMutationV1::Empty:
                projection.factories.clear();
                break;
            case ProcessPlanMutationV1::NonProcessScope:
                projection.scope = HostScopeKindStateV1::Project;
                break;
            case ProcessPlanMutationV1::ParentedProcessScope:
                projection.parentScope = HostScopeKindStateV1::Project;
                break;
            case ProcessPlanMutationV1::EngineGenerationMismatch:
                projection.engineGenerationId = generationId('6');
                break;
            case ProcessPlanMutationV1::BlueprintMismatch:
                projection.blueprintIntegrity = digest('6');
                break;
            case ProcessPlanMutationV1::LifecycleModelMismatch:
                projection.lifecycleModel = "unsupported-lifecycle";
                break;
            case ProcessPlanMutationV1::PackageIdentityMismatch:
                projection.factories[1].reference.packageId = "com.asharia.test.other";
                break;
            case ProcessPlanMutationV1::VersionIdentityMismatch:
                projection.factories[1].reference.packageVersion = "2.0.0";
                break;
            case ProcessPlanMutationV1::ModuleIdentityMismatch:
                projection.factories[1].reference.moduleId = "other";
                break;
            case ProcessPlanMutationV1::FactoryIdentityMismatch:
                projection.factories[1].reference.factoryId = "missing";
                break;
            case ProcessPlanMutationV1::DuplicateFactory:
                projection.factories[1].reference = projection.factories[0].reference;
                break;
            case ProcessPlanMutationV1::DuplicateRequirement:
                projection.factories[1].requirements.push_back(projection.factories[0].reference);
                break;
            case ProcessPlanMutationV1::MissingRequirement:
                projection.factories[1].requirements[0].factoryId = "missing";
                break;
            case ProcessPlanMutationV1::ForwardRequirement:
                std::swap(projection.factories[0], projection.factories[1]);
                break;
            case ProcessPlanMutationV1::IncludeProjectOnlySingleConflict:
                projection.factories.push_back({
                    .reference = factoryReference(kSyntheticProjectOnlyFactoryId),
                    .requirements = {},
                });
                break;
            case ProcessPlanMutationV1::IncludeZeroContributionFactory:
                projection.factories.push_back({
                    .reference = factoryReference(kSyntheticEmptyFactoryId),
                    .requirements = {},
                });
                break;
            }
        }

        [[nodiscard]] ActivationEligibilityErrorV2 processEpochClaimError() noexcept {
            return {
                .stage = ActivationEligibilityStageV2::PreRegistration,
                .code = ActivationEligibilityErrorCodeV2::ProcessEpochConsumed,
                .field = ActivationEligibilityFieldV2::CurrentProcess,
                .registrationCode = std::nullopt,
            };
        }

    } // namespace

    ActivationEligibilityResultV2<AdmittedStaticFactoryCallbackTableV2>
    makeAdmittedSyntheticProcessScope(ProcessPlanMutationV1 mutation) {
        ProcessScopeBlueprintProjectionStateV1 projection = processProjection();
        mutateProjection(projection, mutation);

        const auto processEpoch = createAndBindCurrentProcessEpoch();
        const auto controlThreadEpoch = createAndBindCurrentControlThreadEpoch();
        if (!tryClaimCurrentProcessEpoch(processEpoch)) {
            return std::unexpected(processEpochClaimError());
        }

        // Tests-only defense-in-depth fixture: intentionally bypass Stage1 so
        // ProcessScope can receive invalid projections and exercise its own
        // preflight. Ordinary tests and generated providers must never use it.
        auto lineage = std::make_unique<ActivationEligibilityLineageStateV2>(
            ActivationEligibilityLineageStateV2{
                .host = hostIdentity(),
                .effectiveSessionIntegrity = digest('1'),
                .staticCompositionGenerationId = std::string(kSyntheticCompositionGenerationId),
                .blueprintIntegrity = std::string(kSyntheticBlueprintSha256),
                .generationTuple =
                    {
                        .templateRendererRevision = 3,
                        .compositionRendererRevision = 6,
                        .providerApi = std::string(kProviderApi),
                        .registrationSnapshotSchemaVersion = 2,
                    },
                .lifecycleModel = std::string(kLifecycleModel),
                .processScope = std::move(projection),
                .processEpoch = processEpoch,
                .controlThreadEpoch = controlThreadEpoch,
                .registrationCapacity = &syntheticRegistrationCapacity,
                .recordProviders = &recordSyntheticFactoryProviders,
            });

        auto admission =
            ActivationEligibilityStateAccessV2::makePreRegistrationAdmission(std::move(lineage));
        auto pending = recordAdmittedStaticFactoryProviders(std::move(admission));
        if (!pending) {
            return std::unexpected(pending.error());
        }
        return admitStaticFactoryActivation(std::move(*pending));
    }

    void rebindSyntheticCurrentProcessEpoch() {
        [[maybe_unused]] const auto currentEpoch = createAndBindCurrentProcessEpoch();
    }

} // namespace asharia::host_runtime::tests
