#include "current_image_activation_test_provider.hpp"

#include <array>
#include <atomic>
#include <cstdlib>
#include <utility>

#include "asharia/host_runtime/current_image_activation_descriptor_provider_access.hpp"

namespace asharia::host_runtime::tests {
    namespace {

        constexpr std::string_view kPackageId{"com.asharia.test.eligibility"};
        constexpr std::string_view kPackageVersion{"1.0.0"};
        constexpr std::string_view kModuleId{"runtime"};
        constexpr std::string_view kEntryPoint{
            "asharia::host_runtime::tests::provideExpectedFactory"};
        constexpr std::string_view kExpectedFactoryId{"service-a"};
        constexpr std::string_view kUnexpectedFactoryId{"service-b"};
        constexpr std::string_view kExpectedContributionId{"service-a.default"};
        constexpr std::string_view kEngineGenerationId{
            "sha256-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
        constexpr std::string_view kCompositionGenerationId{
            "sha256-cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"};
        constexpr std::string_view kUnexpectedCompositionGenerationId{
            "sha256-dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"};
        constexpr std::string_view kEffectiveSessionIntegrity{
            "1111111111111111111111111111111111111111111111111111111111111111"};
        constexpr std::string_view kBlueprintSha256{
            "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"};
        constexpr std::string_view kLifecycleModel{"create-activate-quiesce-deactivate-destroy-v1"};
        constexpr std::string_view kProviderApi{"asharia-static-factory-provider-v4"};

        std::atomic_size_t
            recordingInvocations{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
        std::atomic_size_t
            providerInvocations{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
        std::atomic_size_t
            lifecycleInvocations{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
        std::atomic_size_t
            contributionAccessorInvocations{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

        struct EligibilityServiceContract final {
            static constexpr std::string_view kind{"com.asharia.test.eligibility-service"};
            static constexpr StaticContributionCardinalityV1 cardinality{
                StaticContributionCardinalityV1::Single};
        };

        [[noreturn]] EligibilityServiceContract*
        abortContributionAccessor(FactoryInstanceViewV1 unusedInstance) noexcept {
            (void)unusedInstance;
            contributionAccessorInvocations.fetch_add(1, std::memory_order_relaxed);
            std::abort();
        }

        constexpr auto kExpectedContributionBinding =
            bindStaticContributionV2<EligibilityServiceContract, &abortContributionAccessor>(
                kExpectedContributionId);
        constexpr std::array kExpectedContributionBindings{kExpectedContributionBinding};

        [[nodiscard]] StaticFactoryRegistrationCapacityV2 expectedCapacity() noexcept {
            return {
                .providerCount = 1,
                .factoryCount = 1,
                .contributionCount = 1,
                .textBytes = 512,
                .diagnosticFactoryIdBytes = 256,
                .diagnosticContributionIdBytes = 256,
            };
        }

        [[nodiscard]] StaticFactoryRegistrationCapacityV2 invalidCapacity() noexcept {
            return {
                .providerCount = 0,
                .factoryCount = 1,
                .contributionCount = 0,
                .textBytes = 512,
                .diagnosticFactoryIdBytes = 256,
                .diagnosticContributionIdBytes = 256,
            };
        }

        [[nodiscard]] StaticFactoryRegistrationCapacityV2 zeroFactoryCapacity() noexcept {
            return {
                .providerCount = 0,
                .factoryCount = 0,
                .contributionCount = 0,
                .textBytes = 256,
                .diagnosticFactoryIdBytes = 128,
                .diagnosticContributionIdBytes = 128,
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
            registrar.registerFactory(kExpectedFactoryId, expectedCallbacks(),
                                      kExpectedContributionBindings);
        }

        void provideAlternateFactoryCallbacks(StaticFactoryRegistrar& registrar) noexcept {
            providerInvocations.fetch_add(1, std::memory_order_relaxed);
            registrar.registerFactory(kExpectedFactoryId, alternateCallbacks(),
                                      kExpectedContributionBindings);
        }

        void provideUnexpectedFactory(StaticFactoryRegistrar& registrar) noexcept {
            providerInvocations.fetch_add(1, std::memory_order_relaxed);
            registrar.registerFactory(kUnexpectedFactoryId, expectedCallbacks(),
                                      kExpectedContributionBindings);
        }

        void provideNothing(StaticFactoryRegistrar& unusedRegistrar) noexcept {
            (void)unusedRegistrar;
            providerInvocations.fetch_add(1, std::memory_order_relaxed);
        }

        struct FactoryRecordingCaseV2 final {
            std::string_view generationId;
            std::string_view blueprintSha256;
            std::string_view factoryId;
            StaticFactoryProviderV4 provider;
        };

        void recordOneFactory(StaticFactoryRegistrationRecorder& recorder,
                              const FactoryRecordingCaseV2& recording) noexcept {
            recorder.beginComposition({
                .generationId = recording.generationId,
                .hostActivationBlueprintSha256 = recording.blueprintSha256,
                .capacity = expectedCapacity(),
            });
            constexpr std::array expectedContributions{StaticContributionExpectationV1{
                .contributionId = kExpectedContributionId,
                .contributionKind = EligibilityServiceContract::kind,
            }};
            const std::array expectedFactories{StaticFactoryExpectationV1{
                .factoryId = recording.factoryId,
                .contributions = expectedContributions,
            }};
            recorder.invokeProvider(
                {
                    .packageId = kPackageId,
                    .packageVersion = kPackageVersion,
                    .moduleId = kModuleId,
                    .entryPoint = kEntryPoint,
                    .expectedFactories = expectedFactories,
                },
                recording.provider);
            recorder.endComposition();
        }

        void recordExpectedFactories(StaticFactoryRegistrationRecorder& recorder) noexcept {
            recordingInvocations.fetch_add(1, std::memory_order_relaxed);
            recordOneFactory(recorder, {
                                           .generationId = kCompositionGenerationId,
                                           .blueprintSha256 = kBlueprintSha256,
                                           .factoryId = kExpectedFactoryId,
                                           .provider = &provideExpectedFactory,
                                       });
        }

        void recordAlternateCallbacks(StaticFactoryRegistrationRecorder& recorder) noexcept {
            recordingInvocations.fetch_add(1, std::memory_order_relaxed);
            recordOneFactory(recorder, {
                                           .generationId = kCompositionGenerationId,
                                           .blueprintSha256 = kBlueprintSha256,
                                           .factoryId = kExpectedFactoryId,
                                           .provider = &provideAlternateFactoryCallbacks,
                                       });
        }

        void recordUnexpectedSnapshot(StaticFactoryRegistrationRecorder& recorder) noexcept {
            recordingInvocations.fetch_add(1, std::memory_order_relaxed);
            recordOneFactory(recorder, {
                                           .generationId = kUnexpectedCompositionGenerationId,
                                           .blueprintSha256 = kBlueprintSha256,
                                           .factoryId = kUnexpectedFactoryId,
                                           .provider = &provideUnexpectedFactory,
                                       });
        }

        void recordRegistrationFailure(StaticFactoryRegistrationRecorder& recorder) noexcept {
            recordingInvocations.fetch_add(1, std::memory_order_relaxed);
            recordOneFactory(recorder, {
                                           .generationId = kCompositionGenerationId,
                                           .blueprintSha256 = kBlueprintSha256,
                                           .factoryId = kExpectedFactoryId,
                                           .provider = &provideNothing,
                                       });
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

        constexpr CurrentImageExactFactoryReferenceProviderV2 kExpectedReference{
            .packageId = kPackageId,
            .packageVersion = kPackageVersion,
            .moduleId = kModuleId,
            .factoryId = kExpectedFactoryId,
        };
        constexpr std::array<CurrentImageExactFactoryReferenceProviderV2, 0> kNoRequirements{};
        constexpr std::array kExpectedProcessFactories{CurrentImageProcessFactoryProviderV2{
            .reference = kExpectedReference,
            .requirements = kNoRequirements,
        }};
        constexpr std::array<CurrentImageProcessFactoryProviderV2, 0> kNoProcessFactories{};

        constexpr CurrentImageActivationDescriptorProviderV2 baseProvider() noexcept {
            return {
                .engineGenerationId = kEngineGenerationId,
                .hostKind = "runtime",
                .targetPlatform = "windows-x86_64",
                .effectiveSessionIntegrity = kEffectiveSessionIntegrity,
                .staticCompositionGenerationId = kCompositionGenerationId,
                .hostActivationBlueprintSha256 = kBlueprintSha256,
                .templateRendererRevision = 3,
                .compositionRendererRevision = 6,
                .providerApi = kProviderApi,
                .registrationSnapshotSchemaVersion = 2,
                .lifecycleModel = kLifecycleModel,
                .processFactories = kExpectedProcessFactories,
                .registrationCapacity = &expectedCapacity,
                .recordProviders = &recordExpectedFactories,
            };
        }

        constexpr auto kValidProvider = baseProvider();
        constexpr auto kInvalidHostProvider = [] {
            auto provider = baseProvider();
            provider.hostKind = {};
            return provider;
        }();
        constexpr auto kInvalidSessionProvider = [] {
            auto provider = baseProvider();
            provider.effectiveSessionIntegrity = "invalid";
            return provider;
        }();
        constexpr auto kInvalidCompositionProvider = [] {
            auto provider = baseProvider();
            provider.staticCompositionGenerationId = "invalid";
            return provider;
        }();
        constexpr auto kInvalidBlueprintProvider = [] {
            auto provider = baseProvider();
            provider.hostActivationBlueprintSha256 = "invalid";
            return provider;
        }();
        constexpr auto kOldTemplateProvider = [] {
            auto provider = baseProvider();
            provider.templateRendererRevision = 2;
            return provider;
        }();
        constexpr auto kOldCompositionProvider = [] {
            auto provider = baseProvider();
            provider.compositionRendererRevision = 5;
            return provider;
        }();
        constexpr auto kOldApiProvider = [] {
            auto provider = baseProvider();
            provider.providerApi = "asharia-static-factory-provider-v3";
            return provider;
        }();
        constexpr auto kOldSnapshotProvider = [] {
            auto provider = baseProvider();
            provider.registrationSnapshotSchemaVersion = 1;
            return provider;
        }();
        constexpr auto kInvalidLifecycleProvider = [] {
            auto provider = baseProvider();
            provider.lifecycleModel = "invalid";
            return provider;
        }();

        constexpr CurrentImageExactFactoryReferenceProviderV2 kInvalidReference{
            .packageId = {},
            .packageVersion = kPackageVersion,
            .moduleId = kModuleId,
            .factoryId = kExpectedFactoryId,
        };
        constexpr std::array kInvalidReferenceFactories{CurrentImageProcessFactoryProviderV2{
            .reference = kInvalidReference,
            .requirements = kNoRequirements,
        }};
        constexpr auto kInvalidReferenceProvider = [] {
            auto provider = baseProvider();
            provider.processFactories = kInvalidReferenceFactories;
            return provider;
        }();

        constexpr CurrentImageExactFactoryReferenceProviderV2 kMissingReference{
            .packageId = kPackageId,
            .packageVersion = kPackageVersion,
            .moduleId = kModuleId,
            .factoryId = "missing",
        };
        constexpr std::array kMissingRequirements{kMissingReference};
        constexpr std::array kMissingRequirementFactories{CurrentImageProcessFactoryProviderV2{
            .reference = kExpectedReference,
            .requirements = kMissingRequirements,
        }};
        constexpr auto kMissingRequirementProvider = [] {
            auto provider = baseProvider();
            provider.processFactories = kMissingRequirementFactories;
            return provider;
        }();

        constexpr auto kMissingCapacityProvider = [] {
            auto provider = baseProvider();
            provider.registrationCapacity = nullptr;
            return provider;
        }();
        constexpr auto kMissingRecordingProvider = [] {
            auto provider = baseProvider();
            provider.recordProviders = nullptr;
            return provider;
        }();
        constexpr auto kInvalidCapacityProvider = [] {
            auto provider = baseProvider();
            provider.registrationCapacity = &invalidCapacity;
            return provider;
        }();
        constexpr auto kRegistrationFailureProvider = [] {
            auto provider = baseProvider();
            provider.recordProviders = &recordRegistrationFailure;
            return provider;
        }();
        constexpr auto kUnexpectedSnapshotProvider = [] {
            auto provider = baseProvider();
            provider.recordProviders = &recordUnexpectedSnapshot;
            return provider;
        }();
        constexpr auto kAlternateCallbacksProvider = [] {
            auto provider = baseProvider();
            provider.recordProviders = &recordAlternateCallbacks;
            return provider;
        }();
        constexpr auto kZeroFactoryProvider = [] {
            auto provider = baseProvider();
            provider.processFactories = kNoProcessFactories;
            provider.registrationCapacity = &zeroFactoryCapacity;
            provider.recordProviders = &recordZeroFactories;
            return provider;
        }();

        template <const CurrentImageActivationDescriptorProviderV2& Provider>
        [[nodiscard]] ActivationEligibilityResultV2<CurrentImageActivationDescriptorV2>
        sealProvider() noexcept {
            return CurrentImageActivationDescriptorProviderAccessV2::seal<Provider>();
        }

    } // namespace

    ActivationEligibilityResultV2<CurrentImageActivationDescriptorV2>
    issueCurrentImageActivationDescriptor(CurrentImageDescriptorMutationV2 mutation) noexcept {
        switch (mutation) {
        case CurrentImageDescriptorMutationV2::None:
            return sealProvider<kValidProvider>();
        case CurrentImageDescriptorMutationV2::InvalidHostIdentity:
            return sealProvider<kInvalidHostProvider>();
        case CurrentImageDescriptorMutationV2::InvalidEffectiveSession:
            return sealProvider<kInvalidSessionProvider>();
        case CurrentImageDescriptorMutationV2::InvalidStaticComposition:
            return sealProvider<kInvalidCompositionProvider>();
        case CurrentImageDescriptorMutationV2::InvalidBlueprintIntegrity:
            return sealProvider<kInvalidBlueprintProvider>();
        case CurrentImageDescriptorMutationV2::UnsupportedTemplateRenderer:
            return sealProvider<kOldTemplateProvider>();
        case CurrentImageDescriptorMutationV2::UnsupportedCompositionRenderer:
            return sealProvider<kOldCompositionProvider>();
        case CurrentImageDescriptorMutationV2::UnsupportedProviderApi:
            return sealProvider<kOldApiProvider>();
        case CurrentImageDescriptorMutationV2::UnsupportedSnapshotSchemaVersion:
            return sealProvider<kOldSnapshotProvider>();
        case CurrentImageDescriptorMutationV2::InvalidLifecycleModel:
            return sealProvider<kInvalidLifecycleProvider>();
        case CurrentImageDescriptorMutationV2::InvalidFactoryReference:
            return sealProvider<kInvalidReferenceProvider>();
        case CurrentImageDescriptorMutationV2::MissingFactoryRequirement:
            return sealProvider<kMissingRequirementProvider>();
        case CurrentImageDescriptorMutationV2::MissingCapacityFunction:
            return sealProvider<kMissingCapacityProvider>();
        case CurrentImageDescriptorMutationV2::MissingRecordingFunction:
            return sealProvider<kMissingRecordingProvider>();
        case CurrentImageDescriptorMutationV2::InvalidCapacityFunction:
            return sealProvider<kInvalidCapacityProvider>();
        case CurrentImageDescriptorMutationV2::RegistrationFailureFunction:
            return sealProvider<kRegistrationFailureProvider>();
        case CurrentImageDescriptorMutationV2::UnexpectedSnapshotFunction:
            return sealProvider<kUnexpectedSnapshotProvider>();
        case CurrentImageDescriptorMutationV2::AlternateCallbacksFunction:
            return sealProvider<kAlternateCallbacksProvider>();
        case CurrentImageDescriptorMutationV2::ZeroFactoryFunction:
            return sealProvider<kZeroFactoryProvider>();
        }
        std::abort();
    }

    void resetEligibilityProbeCounts() noexcept {
        recordingInvocations.store(0, std::memory_order_relaxed);
        providerInvocations.store(0, std::memory_order_relaxed);
        lifecycleInvocations.store(0, std::memory_order_relaxed);
        contributionAccessorInvocations.store(0, std::memory_order_relaxed);
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

    std::size_t contributionAccessorInvocationCount() noexcept {
        return contributionAccessorInvocations.load(std::memory_order_relaxed);
    }

    StaticFactoryRegistrationResult<StaticFactoryCallbackTableV1>
    collectEligibilityEvidenceOnlyTable(bool useAlternateCallbacks) noexcept {
        auto recorder = createStaticFactoryRegistrationRecorder(expectedCapacity());
        if (!recorder) {
            return std::unexpected(recorder.error());
        }
        if (useAlternateCallbacks) {
            recordAlternateCallbacks(*recorder);
        } else {
            recordExpectedFactories(*recorder);
        }
        return std::move(*recorder).finish();
    }

    bool eligibilityCallbackTableUsesExpected(
        std::span<const StaticFactoryCallbacksV1> callbacks) noexcept {
        return callbacks.size() == 1 && callbacks.front().create == &abortCreate &&
               callbacks.front().activate == &abortActivate &&
               callbacks.front().quiesce == &abortQuiesce &&
               callbacks.front().deactivate == &abortDeactivate &&
               callbacks.front().destroy == &abortDestroy;
    }

} // namespace asharia::host_runtime::tests
