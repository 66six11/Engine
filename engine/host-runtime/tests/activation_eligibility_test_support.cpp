#include "activation_eligibility_test_support.hpp"

#include <array>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

#include "activation_eligibility_state.hpp"
#include "admitted_static_factory_callback_table_access.hpp"

namespace asharia::host_runtime::tests {
    namespace {

        constexpr std::string_view kProviderApi{"asharia-static-factory-provider-v2"};
        constexpr std::string_view kPackageId{"com.asharia.test.eligibility"};
        constexpr std::string_view kPackageVersion{"1.0.0"};
        constexpr std::string_view kModuleId{"runtime"};
        constexpr std::string_view kEntryPoint{
            "asharia::host_runtime::tests::provideExpectedFactory"};
        constexpr std::string_view kExpectedFactoryId{"service-a"};
        constexpr std::string_view kUnexpectedFactoryId{"service-b"};
        constexpr std::string_view kCompositionGenerationId{
            "sha256-cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"};
        constexpr std::string_view kBlueprintSha256{
            "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"};

        std::atomic_size_t recordingInvocations{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
        std::atomic_size_t providerInvocations{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
        std::atomic_size_t lifecycleInvocations{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

        [[nodiscard]] std::string digest(char character) {
            // Braced return selects the initializer-list constructor and changes
            // the digest to two characters, so the explicit count overload matters.
            // NOLINTNEXTLINE(modernize-return-braced-init-list)
            return std::string(64, character);
        }

        [[nodiscard]] std::string generationId(char character) {
            return "sha256-" + digest(character);
        }

        [[nodiscard]] ExactHostIdentityStateV1 hostIdentity() {
            return {
                .engineGenerationId = generationId('a'),
                .hostKind = "runtime",
                .targetPlatform = "windows-x86_64",
            };
        }

        [[nodiscard]] StaticFactoryRegistrationCapacityV1 expectedCapacity() noexcept {
            return {
                .providerCount = 1,
                .factoryCount = 1,
                .textBytes = 512,
                .diagnosticFactoryIdBytes = 256,
            };
        }

        [[nodiscard]] StaticFactoryRegistrationCapacityV1 invalidCapacity() noexcept {
            return {
                .providerCount = 0,
                .factoryCount = 1,
                .textBytes = 512,
                .diagnosticFactoryIdBytes = 256,
            };
        }

        [[nodiscard]] StaticFactoryRegistrationCapacityV1 zeroFactoryCapacity() noexcept {
            return {
                .providerCount = 0,
                .factoryCount = 0,
                .textBytes = 256,
                .diagnosticFactoryIdBytes = 128,
            };
        }

        [[noreturn]] FactoryCreateResultV1
        abortCreate(FactoryCreateContextV1& unusedContext) noexcept {
            (void)unusedContext;
            lifecycleInvocations.fetch_add(1, std::memory_order_relaxed);
            std::abort();
        }

        [[noreturn]] FactoryCreateResultV1
        alternateAbortCreate(FactoryCreateContextV1& unusedContext) noexcept {
            (void)unusedContext;
            lifecycleInvocations.fetch_add(1, std::memory_order_relaxed);
            std::abort();
        }

        [[noreturn]] FactoryCallbackResultV1
        abortActivate(FactoryActivateContextV1& unusedContext,
                      FactoryInstanceViewV1 unusedInstance) noexcept {
            (void)unusedContext;
            (void)unusedInstance;
            lifecycleInvocations.fetch_add(1, std::memory_order_relaxed);
            std::abort();
        }

        [[noreturn]] FactoryCallbackResultV1
        abortQuiesce(FactoryQuiesceContextV1& unusedContext,
                     FactoryInstanceViewV1 unusedInstance) noexcept {
            (void)unusedContext;
            (void)unusedInstance;
            lifecycleInvocations.fetch_add(1, std::memory_order_relaxed);
            std::abort();
        }

        [[noreturn]] FactoryCallbackResultV1
        abortDeactivate(FactoryDeactivateContextV1& unusedContext,
                        FactoryInstanceViewV1 unusedInstance) noexcept {
            (void)unusedContext;
            (void)unusedInstance;
            lifecycleInvocations.fetch_add(1, std::memory_order_relaxed);
            std::abort();
        }

        [[noreturn]] void abortDestroy(FactoryInstanceTokenV1 unusedInstance) noexcept {
            (void)unusedInstance;
            lifecycleInvocations.fetch_add(1, std::memory_order_relaxed);
            std::abort();
        }

        [[nodiscard]] StaticFactoryCallbacksV1 expectedCallbacks() noexcept {
            return {
                .create = &abortCreate,
                .activate = &abortActivate,
                .quiesce = &abortQuiesce,
                .deactivate = &abortDeactivate,
                .destroy = &abortDestroy,
            };
        }

        [[nodiscard]] StaticFactoryCallbacksV1 alternateCallbacks() noexcept {
            StaticFactoryCallbacksV1 callbacks = expectedCallbacks();
            callbacks.create = &alternateAbortCreate;
            return callbacks;
        }

        void provideExpectedFactory(StaticFactoryRegistrar& registrar) noexcept {
            providerInvocations.fetch_add(1, std::memory_order_relaxed);
            registrar.registerFactory(kExpectedFactoryId, expectedCallbacks());
        }

        void provideAlternateFactoryCallbacks(StaticFactoryRegistrar& registrar) noexcept {
            providerInvocations.fetch_add(1, std::memory_order_relaxed);
            registrar.registerFactory(kExpectedFactoryId, alternateCallbacks());
        }

        void provideUnexpectedFactory(StaticFactoryRegistrar& registrar) noexcept {
            providerInvocations.fetch_add(1, std::memory_order_relaxed);
            registrar.registerFactory(kUnexpectedFactoryId, expectedCallbacks());
        }

        void provideNothing(StaticFactoryRegistrar& unusedRegistrar) noexcept {
            (void)unusedRegistrar;
            providerInvocations.fetch_add(1, std::memory_order_relaxed);
        }

        void recordOneFactory(StaticFactoryRegistrationRecorder& recorder,
                              std::string_view factoryId,
                              StaticFactoryProviderV2 provider) noexcept {
            const StaticFactoryRegistrationCapacityV1 capacity = expectedCapacity();
            recorder.beginComposition({
                .generationId = kCompositionGenerationId,
                .hostActivationBlueprintSha256 = kBlueprintSha256,
                .capacity = capacity,
            });
            const std::array<std::string_view, 1> expectedFactories{factoryId};
            recorder.invokeProvider(
                {
                    .packageId = kPackageId,
                    .packageVersion = kPackageVersion,
                    .moduleId = kModuleId,
                    .entryPoint = kEntryPoint,
                    .expectedFactoryIds = expectedFactories,
                },
                provider);
            recorder.endComposition();
        }

        void recordExpectedFactories(StaticFactoryRegistrationRecorder& recorder) noexcept {
            recordingInvocations.fetch_add(1, std::memory_order_relaxed);
            recordOneFactory(recorder, kExpectedFactoryId, &provideExpectedFactory);
        }

        void recordAlternateCallbacks(StaticFactoryRegistrationRecorder& recorder) noexcept {
            recordingInvocations.fetch_add(1, std::memory_order_relaxed);
            recordOneFactory(recorder, kExpectedFactoryId, &provideAlternateFactoryCallbacks);
        }

        void recordUnexpectedSnapshot(StaticFactoryRegistrationRecorder& recorder) noexcept {
            recordingInvocations.fetch_add(1, std::memory_order_relaxed);
            recordOneFactory(recorder, kUnexpectedFactoryId, &provideUnexpectedFactory);
        }

        void recordRegistrationFailure(StaticFactoryRegistrationRecorder& recorder) noexcept {
            recordingInvocations.fetch_add(1, std::memory_order_relaxed);
            recordOneFactory(recorder, kExpectedFactoryId, &provideNothing);
        }

        void recordZeroFactories(StaticFactoryRegistrationRecorder& recorder) noexcept {
            recordingInvocations.fetch_add(1, std::memory_order_relaxed);
            recorder.beginComposition({
                .generationId = kCompositionGenerationId,
                .hostActivationBlueprintSha256 = kBlueprintSha256,
                .capacity = zeroFactoryCapacity(),
            });
            recorder.endComposition();
        }

        [[nodiscard]] StaticFactoryRegistrationSnapshotV1 expectedSnapshot() {
            return {
                .generationId = generationId('c'),
                .hostActivationBlueprintSha256 = digest('b'),
                .registrations =
                    {
                        {
                            .packageId = std::string(kPackageId),
                            .packageVersion = std::string(kPackageVersion),
                            .moduleId = std::string(kModuleId),
                            .factoryId = std::string(kExpectedFactoryId),
                            .providerEntryPoint = std::string(kEntryPoint),
                        },
                    },
            };
        }

    } // namespace

    // The analyzer cannot follow the sealed pimpl ownership through the aggregate
    // return, but every allocation is retained by one of the returned handoffs.
    // NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks)
    EligibilityHandoffsV1
    makeEligibilityHandoffs(EligibilityHandoffMutationV1 mutation) {
        ReadySessionHandoffStateV1 ready{
            .host = hostIdentity(),
            .sessionFingerprint = digest('1'),
        };
        VerifiedHostActivationBlueprintHandoffStateV1 blueprint{
            .host = hostIdentity(),
            .effectiveSessionIntegrity = digest('1'),
            .blueprintIntegrity = digest('b'),
            .processScope =
                {
                    .scope = HostScopeKindStateV1::Process,
                    .parentScope = std::nullopt,
                    .engineGenerationId = generationId('a'),
                    .blueprintIntegrity = digest('b'),
                    .lifecycleModel =
                        "create-activate-quiesce-deactivate-destroy-v1",
                    .factories =
                        {
                            {
                                .reference =
                                    {
                                        .packageId = std::string(kPackageId),
                                        .packageVersion = std::string(kPackageVersion),
                                        .moduleId = std::string(kModuleId),
                                        .factoryId = std::string(kExpectedFactoryId),
                                    },
                                .requirements = {},
                            },
                        },
                },
        };
        DeepVerifiedHostBindingHandoffStateV1 binding{
            .host = hostIdentity(),
            .bindingGenerationId = generationId('d'),
            .staticComposition =
                {
                    .generationId = generationId('c'),
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
                    .compositionRendererRevision = 3,
                    .providerApi = std::string(kProviderApi),
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
            .registrationCapacity = &expectedCapacity,
            .recordProviders = &recordExpectedFactories,
        };

        switch (mutation) {
        case EligibilityHandoffMutationV1::None:
            break;
        case EligibilityHandoffMutationV1::InvalidReadySession:
            ready.host.hostKind.clear();
            break;
        case EligibilityHandoffMutationV1::InvalidBinding:
            binding.bindingGenerationId.clear();
            break;
        case EligibilityHandoffMutationV1::SessionFingerprintMismatch:
            blueprint.effectiveSessionIntegrity = digest('6');
            break;
        case EligibilityHandoffMutationV1::HostIdentityMismatch:
            launch.host.targetPlatform = "linux-x86_64";
            break;
        case EligibilityHandoffMutationV1::BlueprintMismatch:
            launch.blueprintIntegrity = digest('6');
            break;
        case EligibilityHandoffMutationV1::StaticCompositionMismatch:
            launch.staticComposition.generationId = generationId('6');
            break;
        case EligibilityHandoffMutationV1::HostTemplateMismatch:
            launch.hostTemplate.manifestSha256 = digest('6');
            break;
        case EligibilityHandoffMutationV1::UnsupportedTemplateRenderer:
            binding.generationTuple.templateRendererRevision = 1;
            launch.generationTuple.templateRendererRevision = 1;
            break;
        case EligibilityHandoffMutationV1::UnsupportedCompositionRenderer:
            binding.generationTuple.compositionRendererRevision = 2;
            launch.generationTuple.compositionRendererRevision = 2;
            break;
        case EligibilityHandoffMutationV1::UnsupportedProviderApi:
            binding.generationTuple.providerApi = "asharia-static-factory-provider-v1";
            launch.generationTuple.providerApi = binding.generationTuple.providerApi;
            break;
        case EligibilityHandoffMutationV1::BindingGenerationMismatch:
            launch.bindingGenerationId = generationId('6');
            break;
        case EligibilityHandoffMutationV1::ArtifactMismatch:
            launch.artifact.sha256 = digest('6');
            break;
        case EligibilityHandoffMutationV1::ExpectedSnapshotInvalid:
            binding.expectedSnapshot.registrations.front().factoryId.clear();
            break;
        case EligibilityHandoffMutationV1::LaunchProcessEpochMissing:
            launch.processEpoch.reset();
            break;
        case EligibilityHandoffMutationV1::LaunchProcessEpochStale: {
            [[maybe_unused]] const auto currentEpoch = createAndBindCurrentProcessEpoch();
            break;
        }
        case EligibilityHandoffMutationV1::LaunchProcessEpochConsumed:
            (void)tryClaimCurrentProcessEpoch(launch.processEpoch);
            break;
        case EligibilityHandoffMutationV1::LaunchControlThreadEpochMissing:
            launch.controlThreadEpoch.reset();
            break;
        case EligibilityHandoffMutationV1::LaunchRecordingFunctionMissing:
            launch.recordProviders = nullptr;
            break;
        case EligibilityHandoffMutationV1::InvalidCapacityFunction:
            launch.registrationCapacity = &invalidCapacity;
            break;
        case EligibilityHandoffMutationV1::RegistrationFailureFunction:
            launch.recordProviders = &recordRegistrationFailure;
            break;
        case EligibilityHandoffMutationV1::UnexpectedSnapshotFunction:
            launch.recordProviders = &recordUnexpectedSnapshot;
            break;
        case EligibilityHandoffMutationV1::AlternateCallbacksFunction:
            launch.recordProviders = &recordAlternateCallbacks;
            break;
        case EligibilityHandoffMutationV1::ZeroFactoryFunction:
            binding.expectedSnapshot.registrations.clear();
            blueprint.processScope.factories.clear();
            launch.registrationCapacity = &zeroFactoryCapacity;
            launch.recordProviders = &recordZeroFactories;
            break;
        }

        return {
            .readySession = ActivationEligibilityStateAccessV1::makeReadySession(
                std::move(ready)),
            .blueprint =
                ActivationEligibilityStateAccessV1::makeBlueprint(std::move(blueprint)),
            .binding = ActivationEligibilityStateAccessV1::makeBinding(std::move(binding)),
            .launchHandoff =
                ActivationEligibilityStateAccessV1::makeLaunchHandoff(std::move(launch)),
        };
    }
    // NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)

    ActivationEligibilityResultV1<PreRegistrationAdmissionV1>
    makePreRegistrationAdmission(EligibilityHandoffMutationV1 mutation) {
        EligibilityHandoffsV1 handoffs = makeEligibilityHandoffs(mutation);
        return admitPreRegistration(std::move(handoffs.readySession),
                                    std::move(handoffs.blueprint),
                                    std::move(handoffs.binding),
                                    std::move(handoffs.launchHandoff));
    }

    void resetEligibilityProbeCounts() noexcept {
        recordingInvocations.store(0, std::memory_order_relaxed);
        providerInvocations.store(0, std::memory_order_relaxed);
        lifecycleInvocations.store(0, std::memory_order_relaxed);
    }

    std::size_t recordingFunctionInvocationCount() noexcept {
        return recordingInvocations.load(std::memory_order_relaxed);
    }

    std::size_t providerInvocationCount() noexcept {
        return providerInvocations.load(std::memory_order_relaxed);
    }

    std::size_t lifecycleInvocationCount() noexcept {
        return lifecycleInvocations.load(std::memory_order_relaxed);
    }

    StaticFactoryRegistrationResult<StaticFactoryCallbackTableV1>
    collectEvidenceOnlyTable(bool useAlternateCallbacks) noexcept {
        auto recorderResult = createStaticFactoryRegistrationRecorder(expectedCapacity());
        if (!recorderResult) {
            return std::unexpected(std::move(recorderResult.error()));
        }
        auto recorder = std::move(*recorderResult);
        if (useAlternateCallbacks) {
            recordAlternateCallbacks(recorder);
        } else {
            recordExpectedFactories(recorder);
        }
        return std::move(recorder).finish();
    }

    PendingActivationFactoryTableV1
    markPendingTableEvidenceOnly(PendingActivationFactoryTableV1 pendingTable) noexcept {
        auto state = ActivationEligibilityStateAccessV1::take(std::move(pendingTable));
        state->origin = PendingFactoryTableOriginV1::EvidenceOnly;
        return ActivationEligibilityStateAccessV1::makePendingTable(std::move(state));
    }

    PendingActivationFactoryTableV1
    corruptPendingTableAddress(PendingActivationFactoryTableV1 pendingTable) noexcept {
        auto state = ActivationEligibilityStateAccessV1::take(std::move(pendingTable));
        state->expectedTableAddress = nullptr;
        return ActivationEligibilityStateAccessV1::makePendingTable(std::move(state));
    }

    PendingActivationFactoryTableV1
    corruptPendingExpectedSnapshot(PendingActivationFactoryTableV1 pendingTable) noexcept {
        auto state = ActivationEligibilityStateAccessV1::take(std::move(pendingTable));
        state->lineage->expectedSnapshot.registrations.front().factoryId.clear();
        return ActivationEligibilityStateAccessV1::makePendingTable(std::move(state));
    }

    std::optional<PendingActivationFactoryTableV1> replacePendingWithEquivalentTable(
        PendingActivationFactoryTableV1 pendingTable) noexcept {
        auto replacement = collectEvidenceOnlyTable(true);
        if (!replacement) {
            return std::nullopt;
        }
        auto state = ActivationEligibilityStateAccessV1::take(std::move(pendingTable));
        if (!state) {
            return std::nullopt;
        }
        std::destroy_at(std::addressof(state->table));
        std::construct_at(std::addressof(state->table), std::move(*replacement));
        return ActivationEligibilityStateAccessV1::makePendingTable(std::move(state));
    }

    void rebindCurrentProcessEpochForTest() {
        [[maybe_unused]] const auto currentEpoch = createAndBindCurrentProcessEpoch();
    }

    std::optional<std::size_t> admittedDescriptorCount(
        const AdmittedStaticFactoryCallbackTableV1& admittedTable) noexcept {
        const auto callbacks =
            AdmittedStaticFactoryCallbackTableAccessV1::callbacks(admittedTable);
        if (!callbacks) {
            return std::nullopt;
        }
        return callbacks->size();
    }

    bool admittedTableUsesExpectedCallbacks(
        const AdmittedStaticFactoryCallbackTableV1& admittedTable) noexcept {
        const auto callbacks =
            AdmittedStaticFactoryCallbackTableAccessV1::callbacks(admittedTable);
        return callbacks && callbacks->size() == 1 &&
               callbacks->front().create == &abortCreate &&
               callbacks->front().activate == &abortActivate &&
               callbacks->front().quiesce == &abortQuiesce &&
               callbacks->front().deactivate == &abortDeactivate &&
               callbacks->front().destroy == &abortDestroy;
    }

} // namespace asharia::host_runtime::tests
