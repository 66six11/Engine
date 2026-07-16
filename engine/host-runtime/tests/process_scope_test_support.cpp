#include "process_scope_test_support.hpp"

#include <string>
#include <utility>

#include "activation_eligibility_state.hpp"
#include "process_scope_synthetic_provider.hpp"

namespace asharia::host_runtime::tests {
    namespace {

        constexpr std::string_view kProviderApi{"asharia-static-factory-provider-v3"};

        [[nodiscard]] std::string digest(char character) {
            // NOLINTNEXTLINE(modernize-return-braced-init-list)
            return std::string(64, character);
        }

        [[nodiscard]] std::string generationId(char character) {
            return "sha256-" + digest(character);
        }

        [[nodiscard]] ExactHostIdentityStateV1 hostIdentity() {
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
                .lifecycleModel = "create-activate-quiesce-deactivate-destroy-v1",
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

        [[nodiscard]] StaticFactoryRegistrationSnapshotV2 expectedSnapshot() {
            return {
                .generationId = std::string(kSyntheticCompositionGenerationId),
                .hostActivationBlueprintSha256 = std::string(kSyntheticBlueprintSha256),
                .registrations =
                    {
                        {
                            .packageId = std::string(kSyntheticPackageId),
                            .packageVersion = std::string(kSyntheticPackageVersion),
                            .moduleId = std::string(kSyntheticModuleId),
                            .factoryId = std::string(kSyntheticMiddleFactoryId),
                            .providerEntryPoint = std::string(kSyntheticProviderEntryPoint),
                            .contributions = {},
                        },
                        {
                            .packageId = std::string(kSyntheticPackageId),
                            .packageVersion = std::string(kSyntheticPackageVersion),
                            .moduleId = std::string(kSyntheticModuleId),
                            .factoryId = std::string(kSyntheticProjectOnlyFactoryId),
                            .providerEntryPoint = std::string(kSyntheticProviderEntryPoint),
                            .contributions = {},
                        },
                        {
                            .packageId = std::string(kSyntheticPackageId),
                            .packageVersion = std::string(kSyntheticPackageVersion),
                            .moduleId = std::string(kSyntheticModuleId),
                            .factoryId = std::string(kSyntheticLeafFactoryId),
                            .providerEntryPoint = std::string(kSyntheticProviderEntryPoint),
                            .contributions = {},
                        },
                        {
                            .packageId = std::string(kSyntheticPackageId),
                            .packageVersion = std::string(kSyntheticPackageVersion),
                            .moduleId = std::string(kSyntheticModuleId),
                            .factoryId = std::string(kSyntheticRootFactoryId),
                            .providerEntryPoint = std::string(kSyntheticProviderEntryPoint),
                            .contributions = {},
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
            }
        }

    } // namespace

    // All handoffs are made through the existing PRIVATE test issuer. Production
    // code still has no public constructor for any admission input.
    // NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks)
    ActivationEligibilityResultV1<AdmittedStaticFactoryCallbackTableV1>
    makeAdmittedSyntheticProcessScope(ProcessPlanMutationV1 mutation) {
        ReadySessionHandoffStateV1 ready{
            .host = hostIdentity(),
            .sessionFingerprint = digest('1'),
        };
        ProcessScopeBlueprintProjectionStateV1 projection = processProjection();
        mutateProjection(projection, mutation);
        VerifiedHostActivationBlueprintHandoffStateV1 blueprint{
            .host = hostIdentity(),
            .effectiveSessionIntegrity = digest('1'),
            .blueprintIntegrity = digest('b'),
            .processScope = std::move(projection),
        };
        DeepVerifiedHostBindingHandoffStateV1 binding{
            .host = hostIdentity(),
            .bindingGenerationId = generationId('d'),
            .staticComposition =
                {
                    .generationId = std::string(kSyntheticCompositionGenerationId),
                    .manifestSha256 = digest('3'),
                },
            .hostTemplate =
                {
                    .generationId = generationId('e'),
                    .manifestSha256 = digest('4'),
                },
            .generationTuple =
                {
                    .templateRendererRevision = 2,
                    .compositionRendererRevision = 4,
                    .providerApi = std::string(kProviderApi),
                    .registrationSnapshotSchemaVersion = 2,
                },
            .blueprintIntegrity = digest('b'),
            .artifact =
                {
                    .size = 4096,
                    .sha256 = digest('5'),
                },
            .expectedSnapshot = expectedSnapshot(),
        };
        VerifiedCurrentProcessLaunchHandoffStateV1 launch{
            .host = hostIdentity(),
            .sessionFingerprint = digest('1'),
            .bindingGenerationId = generationId('d'),
            .staticComposition = binding.staticComposition,
            .hostTemplate = binding.hostTemplate,
            .generationTuple = binding.generationTuple,
            .blueprintIntegrity = digest('b'),
            .artifact = binding.artifact,
            .processEpoch = createAndBindCurrentProcessEpoch(),
            .controlThreadEpoch = createAndBindCurrentControlThreadEpoch(),
            .registrationCapacity = &syntheticRegistrationCapacity,
            .recordProviders = &recordSyntheticFactoryProviders,
        };

        auto admission = admitPreRegistration(
            ActivationEligibilityStateAccessV1::makeReadySession(std::move(ready)),
            ActivationEligibilityStateAccessV1::makeBlueprint(std::move(blueprint)),
            ActivationEligibilityStateAccessV1::makeBinding(std::move(binding)),
            ActivationEligibilityStateAccessV1::makeLaunchHandoff(std::move(launch)));
        if (!admission) {
            return std::unexpected(admission.error());
        }
        auto pending = recordAdmittedStaticFactoryProviders(std::move(*admission));
        if (!pending) {
            return std::unexpected(pending.error());
        }
        return admitStaticFactoryActivation(std::move(*pending));
    }
    // NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)

    void rebindSyntheticCurrentProcessEpoch() {
        [[maybe_unused]] const auto currentEpoch = createAndBindCurrentProcessEpoch();
    }

} // namespace asharia::host_runtime::tests
