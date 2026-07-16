#include <array>
#include <string_view>
#include <type_traits>
#include <utility>

#include "asharia/host_runtime/static_factory_registration.hpp"

#include "static_factory_callback_table_tests.hpp"
#include "static_factory_callback_table_private_access.hpp"

namespace {

    using asharia::host_runtime::StaticCompositionRegistrationContextV2;
    using asharia::host_runtime::StaticContributionCardinalityV1;
    using asharia::host_runtime::StaticContributionExpectationV1;
    using asharia::host_runtime::StaticFactoryExpectationV1;
    using asharia::host_runtime::StaticFactoryProviderContextV2;
    using asharia::host_runtime::StaticFactoryRegistrar;
    using asharia::host_runtime::StaticFactoryRegistrationCapacityV2;
    using asharia::host_runtime::StaticFactoryRegistrationErrorCode;

    constexpr std::string_view kGenerationId =
        "sha256-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    constexpr std::string_view kBlueprintSha256 =
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    constexpr std::size_t kTextCapacity = 1024;

    struct ServiceContractA final {
        static constexpr std::string_view kind{"com.asharia.test.service"};
        static constexpr StaticContributionCardinalityV1 cardinality{
            StaticContributionCardinalityV1::Single};
    };

    struct ServiceContractB final {
        static constexpr std::string_view kind{"com.asharia.test.service"};
        static constexpr StaticContributionCardinalityV1 cardinality{
            StaticContributionCardinalityV1::Single};
    };

    struct MultipleServiceContract final {
        static constexpr std::string_view kind{"com.asharia.test.service"};
        static constexpr StaticContributionCardinalityV1 cardinality{
            StaticContributionCardinalityV1::Multiple};
    };

    struct OtherServiceContract final {
        static constexpr std::string_view kind{"com.asharia.test.other-service"};
        static constexpr StaticContributionCardinalityV1 cardinality{
            StaticContributionCardinalityV1::Multiple};
    };

    struct MissingServiceContract final {};

    static_assert(asharia::host_runtime::StaticContributionContractV1<ServiceContractA>);
    static_assert(!asharia::host_runtime::StaticContributionContractV1<MissingServiceContract>);
    static_assert(
        !std::is_default_constructible_v<asharia::host_runtime::StaticContributionBindingV1>);
    static_assert(
        std::is_trivially_copyable_v<asharia::host_runtime::StaticContributionBindingV1>);

    constexpr auto kServiceABinding =
        asharia::host_runtime::bindStaticContributionV1<ServiceContractA>("service-a.default");
    constexpr auto kServiceBBinding =
        asharia::host_runtime::bindStaticContributionV1<ServiceContractA>("service-b.default");
    constexpr auto kDifferentTypeBinding =
        asharia::host_runtime::bindStaticContributionV1<ServiceContractB>("service-b.default");
    constexpr auto kMultipleBinding =
        asharia::host_runtime::bindStaticContributionV1<MultipleServiceContract>(
            "service-b.default");
    constexpr auto kOtherBinding =
        asharia::host_runtime::bindStaticContributionV1<OtherServiceContract>("other.default");
    constexpr auto kWrongKindBinding =
        asharia::host_runtime::bindStaticContributionV1<OtherServiceContract>("service-a.default");

    StaticFactoryRegistrar*
        capturedRegistrar{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

    [[nodiscard]] constexpr StaticFactoryRegistrationCapacityV2
    registrationCapacity(std::size_t providerCount, std::size_t factoryCount, std::size_t textBytes,
                         std::size_t diagnosticFactoryIdBytes = 256,
                         std::size_t contributionCount = 0,
                         std::size_t diagnosticContributionIdBytes = 256) noexcept {
        return {
            .providerCount = providerCount,
            .factoryCount = factoryCount,
            .contributionCount = contributionCount,
            .textBytes = textBytes,
            .diagnosticFactoryIdBytes = diagnosticFactoryIdBytes,
            .diagnosticContributionIdBytes = diagnosticContributionIdBytes,
        };
    }

    [[nodiscard]] constexpr StaticFactoryExpectationV1
    factoryExpectation(std::string_view factoryId) noexcept {
        return {.factoryId = factoryId, .contributions = {}};
    }

    [[nodiscard]] constexpr StaticFactoryExpectationV1
    factoryExpectation(std::string_view factoryId,
                       std::span<const StaticContributionExpectationV1> contributions) noexcept {
        return {.factoryId = factoryId, .contributions = contributions};
    }

    void provideOutOfOrderFactories(StaticFactoryRegistrar& registrar) noexcept {
        registrar.registerFactory("service-b", asharia::host_runtime::tests::abortingCallbacks(),
                                  {});
        registrar.registerFactory("service-a", asharia::host_runtime::tests::abortingCallbacks(),
                                  {});
    }

    void provideInOrderFactories(StaticFactoryRegistrar& registrar) noexcept {
        registrar.registerFactory("service-a", asharia::host_runtime::tests::abortingCallbacks(),
                                  {});
        registrar.registerFactory("service-b", asharia::host_runtime::tests::abortingCallbacks(),
                                  {});
    }

    void provideToolFactory(StaticFactoryRegistrar& registrar) noexcept {
        registrar.registerFactory("service-c", asharia::host_runtime::tests::abortingCallbacks(),
                                  {});
    }

    void provideUnknownFactory(StaticFactoryRegistrar& registrar) noexcept {
        registrar.registerFactory("unknown-service",
                                  asharia::host_runtime::tests::abortingCallbacks(), {});
    }

    void provideSingleFactory(StaticFactoryRegistrar& registrar) noexcept {
        registrar.registerFactory("service-a", asharia::host_runtime::tests::abortingCallbacks(),
                                  {});
    }

    void provideNoFactories(StaticFactoryRegistrar& unusedRegistrar) noexcept {
        (void)unusedRegistrar;
    }

    void provideDuplicateFactory(StaticFactoryRegistrar& registrar) noexcept {
        registrar.registerFactory("service-a", asharia::host_runtime::tests::abortingCallbacks(),
                                  {});
        registrar.registerFactory("service-a", asharia::host_runtime::tests::abortingCallbacks(),
                                  {});
    }

    void provideAndCaptureRegistrar(StaticFactoryRegistrar& registrar) noexcept {
        registrar.registerFactory("service-a", asharia::host_runtime::tests::abortingCallbacks(),
                                  {});
        capturedRegistrar = &registrar;
    }

    void provideSelectedAndUnselectedContributions(StaticFactoryRegistrar& registrar) noexcept {
        constexpr std::array bindings{kServiceABinding, kOtherBinding};
        registrar.registerFactory("service-a", asharia::host_runtime::tests::abortingCallbacks(),
                                  bindings);
    }

    void provideMissingContribution(StaticFactoryRegistrar& registrar) noexcept {
        registrar.registerFactory("service-a", asharia::host_runtime::tests::abortingCallbacks(),
                                  {});
    }

    void provideDuplicateContribution(StaticFactoryRegistrar& registrar) noexcept {
        constexpr std::array bindings{kServiceABinding, kServiceABinding};
        registrar.registerFactory("service-a", asharia::host_runtime::tests::abortingCallbacks(),
                                  bindings);
    }

    void provideWrongKindContribution(StaticFactoryRegistrar& registrar) noexcept {
        constexpr std::array bindings{kWrongKindBinding};
        registrar.registerFactory("service-a", asharia::host_runtime::tests::abortingCallbacks(),
                                  bindings);
    }

    void provideTypeConflict(StaticFactoryRegistrar& registrar) noexcept {
        constexpr std::array first{kServiceABinding};
        constexpr std::array second{kDifferentTypeBinding};
        registrar.registerFactory("service-a", asharia::host_runtime::tests::abortingCallbacks(),
                                  first);
        registrar.registerFactory("service-b", asharia::host_runtime::tests::abortingCallbacks(),
                                  second);
    }

    void provideTypeConflictReversed(StaticFactoryRegistrar& registrar) noexcept {
        constexpr std::array first{kServiceABinding};
        constexpr std::array second{kDifferentTypeBinding};
        registrar.registerFactory("service-b", asharia::host_runtime::tests::abortingCallbacks(),
                                  second);
        registrar.registerFactory("service-a", asharia::host_runtime::tests::abortingCallbacks(),
                                  first);
    }

    void provideCardinalityConflict(StaticFactoryRegistrar& registrar) noexcept {
        constexpr std::array first{kServiceABinding};
        constexpr std::array second{kMultipleBinding};
        registrar.registerFactory("service-a", asharia::host_runtime::tests::abortingCallbacks(),
                                  first);
        registrar.registerFactory("service-b", asharia::host_runtime::tests::abortingCallbacks(),
                                  second);
    }

    void provideSameSingleContractTwice(StaticFactoryRegistrar& registrar) noexcept {
        constexpr std::array first{kServiceABinding};
        constexpr std::array second{kServiceBBinding};
        registrar.registerFactory("service-a", asharia::host_runtime::tests::abortingCallbacks(),
                                  first);
        registrar.registerFactory("service-b", asharia::host_runtime::tests::abortingCallbacks(),
                                  second);
    }

    void provideUnorderedContributionEvidence(StaticFactoryRegistrar& registrar) noexcept {
        constexpr std::array second{kServiceBBinding, kOtherBinding};
        constexpr std::array first{kServiceABinding};
        registrar.registerFactory("service-b", asharia::host_runtime::tests::abortingCallbacks(),
                                  second);
        registrar.registerFactory("service-a", asharia::host_runtime::tests::abortingCallbacks(),
                                  first);
    }

    [[nodiscard]] StaticCompositionRegistrationContextV2
    compositionContext(StaticFactoryRegistrationCapacityV2 capacity) noexcept {
        return {
            .generationId = kGenerationId,
            .hostActivationBlueprintSha256 = kBlueprintSha256,
            .capacity = capacity,
        };
    }

    [[nodiscard]] StaticFactoryProviderContextV2
    providerContext(std::span<const StaticFactoryExpectationV1> factories) noexcept {
        return {
            .packageId = "com.asharia.test.runtime",
            .packageVersion = "1.0.0",
            .moduleId = "runtime",
            .entryPoint = "asharia::test::provideRuntimeFactories",
            .expectedFactories = factories,
        };
    }

    [[nodiscard]] bool validRegistrationOwnsCanonicalSnapshot() noexcept {
        constexpr auto capacity = registrationCapacity(2, 3, kTextCapacity);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return false;
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));

        constexpr std::array firstFactories{factoryExpectation("service-a"),
                                            factoryExpectation("service-b")};
        recorder.invokeProvider(providerContext(firstFactories), &provideOutOfOrderFactories);

        constexpr std::array secondFactories{factoryExpectation("service-c")};
        const StaticFactoryProviderContextV2 secondProvider{
            .packageId = "com.asharia.test.tools",
            .packageVersion = "2.0.0",
            .moduleId = "tools",
            .entryPoint = "asharia::test::provideToolFactories",
            .expectedFactories = secondFactories,
        };
        recorder.invokeProvider(secondProvider, &provideToolFactory);
        recorder.endComposition();

        auto tableResult = std::move(recorder).finish();
        if (!tableResult) {
            return false;
        }
        const auto& snapshot = tableResult->registrationSnapshot();
        return snapshot.generationId == kGenerationId &&
               snapshot.hostActivationBlueprintSha256 == kBlueprintSha256 &&
               snapshot.registrations.size() == 3 &&
               snapshot.registrations[0].factoryId == "service-a" &&
               snapshot.registrations[1].factoryId == "service-b" &&
               snapshot.registrations[2].packageId == "com.asharia.test.tools" &&
               snapshot.registrations[2].factoryId == "service-c";
    }

    [[nodiscard]] asharia::host_runtime::StaticFactoryRegistrationResult<
        asharia::host_runtime::StaticFactoryCallbackTableV1>
    collectEquivalentSnapshot(bool reverseProviderOrder) noexcept {
        constexpr auto capacity = registrationCapacity(2, 3, kTextCapacity);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return std::unexpected(std::move(recorderResult.error()));
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));

        constexpr std::array runtimeFactories{factoryExpectation("service-a"),
                                              factoryExpectation("service-b")};
        constexpr std::array toolFactories{factoryExpectation("service-c")};
        const StaticFactoryProviderContextV2 runtimeProvider = providerContext(runtimeFactories);
        const StaticFactoryProviderContextV2 toolProvider{
            .packageId = "com.asharia.test.tools",
            .packageVersion = "2.0.0",
            .moduleId = "tools",
            .entryPoint = "asharia::test::provideToolFactories",
            .expectedFactories = toolFactories,
        };

        if (reverseProviderOrder) {
            recorder.invokeProvider(toolProvider, &provideToolFactory);
            recorder.invokeProvider(runtimeProvider, &provideInOrderFactories);
        } else {
            recorder.invokeProvider(runtimeProvider, &provideOutOfOrderFactories);
            recorder.invokeProvider(toolProvider, &provideToolFactory);
        }
        recorder.endComposition();
        return std::move(recorder).finish();
    }

    [[nodiscard]] bool equivalentOrdersProduceCanonicalSnapshot() noexcept {
        const auto forward = collectEquivalentSnapshot(false);
        const auto reversed = collectEquivalentSnapshot(true);
        return forward && reversed &&
               forward->registrationSnapshot() == reversed->registrationSnapshot();
    }

    [[nodiscard]] bool unknownFactoryFailsAtomically() noexcept {
        constexpr auto capacity = registrationCapacity(1, 1, kTextCapacity);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return false;
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));
        constexpr std::array factories{factoryExpectation("service-a")};
        recorder.invokeProvider(providerContext(factories), &provideUnknownFactory);
        recorder.endComposition();
        const auto result = std::move(recorder).finish();
        return !result &&
               result.error().code == StaticFactoryRegistrationErrorCode::FactoryUnknown &&
               result.error().factoryId == "unknown-service";
    }

    [[nodiscard]] bool missingFactoryFailsAtomically() noexcept {
        constexpr auto capacity = registrationCapacity(1, 1, kTextCapacity);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return false;
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));
        constexpr std::array factories{factoryExpectation("service-a")};
        recorder.invokeProvider(providerContext(factories), &provideNoFactories);
        recorder.endComposition();
        const auto result = std::move(recorder).finish();
        return !result &&
               result.error().code == StaticFactoryRegistrationErrorCode::FactoryMissing &&
               result.error().factoryId == "service-a";
    }

    [[nodiscard]] bool duplicateFactoryFailsAtomically() noexcept {
        constexpr auto capacity = registrationCapacity(1, 1, kTextCapacity);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return false;
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));
        constexpr std::array factories{factoryExpectation("service-a")};
        recorder.invokeProvider(providerContext(factories), &provideDuplicateFactory);
        recorder.endComposition();
        const auto result = std::move(recorder).finish();
        return !result &&
               result.error().code == StaticFactoryRegistrationErrorCode::FactoryDuplicate;
    }

    [[nodiscard]] bool registrationOutsideProviderFails() noexcept {
        capturedRegistrar = nullptr;
        constexpr auto capacity = registrationCapacity(1, 1, kTextCapacity);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return false;
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));
        constexpr std::array factories{factoryExpectation("service-a")};
        recorder.invokeProvider(providerContext(factories), &provideAndCaptureRegistrar);
        if (capturedRegistrar == nullptr) {
            return false;
        }
        auto movedRecorder = std::move(recorder);
        capturedRegistrar->registerFactory("service-a",
                                           asharia::host_runtime::tests::abortingCallbacks(), {});
        movedRecorder.endComposition();
        const auto result = std::move(movedRecorder).finish();
        return !result &&
               result.error().code == StaticFactoryRegistrationErrorCode::FactoryOutsideProvider;
    }

    [[nodiscard]] bool emptyCompositionIsValid() noexcept {
        constexpr auto capacity = registrationCapacity(0, 0, 256);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return false;
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));
        recorder.endComposition();
        const auto result = std::move(recorder).finish();
        return result && result->registrationSnapshot().registrations.empty();
    }

    [[nodiscard]] bool providerCountMismatchFails() noexcept {
        constexpr auto capacity = registrationCapacity(2, 2, kTextCapacity);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return false;
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));
        constexpr std::array factories{factoryExpectation("service-a")};
        recorder.invokeProvider(providerContext(factories), &provideAndCaptureRegistrar);
        recorder.endComposition();
        const auto result = std::move(recorder).finish();
        return !result &&
               result.error().code == StaticFactoryRegistrationErrorCode::ProviderCountMismatch;
    }

    [[nodiscard]] bool stickyFirstFailureWins() noexcept {
        constexpr auto capacity = registrationCapacity(1, 1, kTextCapacity);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return false;
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));
        constexpr std::array factories{factoryExpectation("service-a")};
        recorder.invokeProvider(providerContext(factories), &provideUnknownFactory);

        recorder.endComposition();
        recorder.endComposition();

        const auto result = std::move(recorder).finish();
        return !result &&
               result.error().code == StaticFactoryRegistrationErrorCode::FactoryUnknown &&
               result.error().factoryId == "unknown-service";
    }

    [[nodiscard]] bool endBeforeBeginFails() noexcept {
        constexpr auto capacity = registrationCapacity(0, 0, 256);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return false;
        }
        auto recorder = std::move(*recorderResult);
        recorder.endComposition();
        const auto result = std::move(recorder).finish();
        return !result &&
               result.error().code == StaticFactoryRegistrationErrorCode::CompositionNotStarted;
    }

    [[nodiscard]] bool mixedCompositionEvidenceFailsAtomically() noexcept {
        constexpr auto capacity = registrationCapacity(0, 0, 256);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return false;
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));
        recorder.beginComposition({
            .generationId =
                "sha256-cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
            .hostActivationBlueprintSha256 =
                "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
            .capacity = capacity,
        });
        recorder.endComposition();
        const auto result = std::move(recorder).finish();
        return !result &&
               result.error().code == StaticFactoryRegistrationErrorCode::CompositionAlreadyStarted;
    }

    [[nodiscard]] bool textCapacityExhaustionFails() noexcept {
        constexpr auto capacity = registrationCapacity(0, 0, 1);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return false;
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));
        recorder.endComposition();
        const auto result = std::move(recorder).finish();
        return !result &&
               result.error().code == StaticFactoryRegistrationErrorCode::TextCapacityExceeded;
    }

    [[nodiscard]] bool duplicateProviderTakesPriorityOverCapacity() noexcept {
        constexpr auto capacity = registrationCapacity(1, 1, kTextCapacity);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return false;
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));
        constexpr std::array factories{factoryExpectation("service-a")};
        const auto context = providerContext(factories);
        recorder.invokeProvider(context, &provideSingleFactory);
        recorder.invokeProvider(context, &provideSingleFactory);
        recorder.endComposition();
        const auto result = std::move(recorder).finish();
        return !result &&
               result.error().code == StaticFactoryRegistrationErrorCode::ProviderDuplicate;
    }

    [[nodiscard]] bool oversizedDiagnosticFactoryIdFailsExplicitly() noexcept {
        constexpr auto capacity = registrationCapacity(1, 1, kTextCapacity, 4);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return false;
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));
        constexpr std::array factories{factoryExpectation("service-a")};
        recorder.invokeProvider(providerContext(factories), &provideUnknownFactory);
        recorder.endComposition();
        const auto result = std::move(recorder).finish();
        constexpr std::string_view kUnknownFactoryId{"unknown-service"};
        return !result &&
               result.error().code ==
                   StaticFactoryRegistrationErrorCode::DiagnosticFactoryIdCapacityExceeded &&
               result.error().factoryId.empty() &&
               result.error().observedFactoryIdBytes == kUnknownFactoryId.size();
    }

    [[nodiscard]] bool selectedSubsetMaterializesTypedMetadata() noexcept {
        constexpr auto capacity = registrationCapacity(1, 1, kTextCapacity, 256, 1);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return false;
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));
        constexpr std::array contributions{StaticContributionExpectationV1{
            .contributionId = "service-a.default",
            .contributionKind = ServiceContractA::kind,
        }};
        const std::array factories{factoryExpectation("service-a", contributions)};
        recorder.invokeProvider(providerContext(factories),
                                &provideSelectedAndUnselectedContributions);
        recorder.endComposition();
        auto result = std::move(recorder).finish();
        if (!result) {
            return false;
        }
        const auto anchor = asharia::host_runtime::StaticFactoryCallbackTablePrivateAccessV1::
            instanceAnchor(*result);
        const auto evidence = asharia::host_runtime::StaticFactoryCallbackTablePrivateAccessV1::
            contributionTypeEvidence(*result);
        if (evidence.size() != 1 || evidence[0].registrationIndex != 0 ||
            evidence[0].contributionIndex != 0 || evidence[0].typeKey == nullptr) {
            return false;
        }
        const auto* evidenceAddress = evidence.data();
        const void* typeKey = evidence[0].typeKey;
        asharia::host_runtime::StaticFactoryCallbackTableV1 movedTable{std::move(*result)};
        const auto movedEvidence =
            asharia::host_runtime::StaticFactoryCallbackTablePrivateAccessV1::
                contributionTypeEvidence(movedTable);
        const auto& registrations = movedTable.registrationSnapshot().registrations;
        return anchor ==
                   asharia::host_runtime::StaticFactoryCallbackTablePrivateAccessV1::
                       instanceAnchor(movedTable) &&
               movedEvidence.data() == evidenceAddress && movedEvidence[0].typeKey == typeKey &&
               registrations.size() == 1 && registrations[0].contributions.size() == 1 &&
               registrations[0].contributions[0].contributionId == "service-a.default" &&
               registrations[0].contributions[0].contributionKind == ServiceContractA::kind &&
               registrations[0].contributions[0].cardinality ==
                   StaticContributionCardinalityV1::Single;
    }

    [[nodiscard]] StaticFactoryRegistrationErrorCode
    contributionFailure(asharia::host_runtime::StaticFactoryProviderV3 provider) noexcept {
        constexpr auto capacity = registrationCapacity(1, 1, kTextCapacity, 256, 1);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return recorderResult.error().code;
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));
        constexpr std::array contributions{StaticContributionExpectationV1{
            .contributionId = "service-a.default",
            .contributionKind = ServiceContractA::kind,
        }};
        const std::array factories{factoryExpectation("service-a", contributions)};
        recorder.invokeProvider(providerContext(factories), provider);
        recorder.endComposition();
        auto result = std::move(recorder).finish();
        return result ? StaticFactoryRegistrationErrorCode::InvalidCapacity : result.error().code;
    }

    [[nodiscard]] bool contributionBindingFailuresAreExplicit() noexcept {
        return contributionFailure(&provideMissingContribution) ==
                   StaticFactoryRegistrationErrorCode::ContributionMissing &&
               contributionFailure(&provideDuplicateContribution) ==
                   StaticFactoryRegistrationErrorCode::ContributionBindingDuplicate &&
               contributionFailure(&provideWrongKindContribution) ==
                   StaticFactoryRegistrationErrorCode::ContributionKindMismatch;
    }

    [[nodiscard]] asharia::host_runtime::StaticFactoryRegistrationError
    collectTypeConflict(asharia::host_runtime::StaticFactoryProviderV3 provider) noexcept {
        constexpr auto capacity = registrationCapacity(1, 2, kTextCapacity, 256, 2);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return std::move(recorderResult.error());
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));
        constexpr std::array firstContributions{StaticContributionExpectationV1{
            .contributionId = "service-a.default",
            .contributionKind = ServiceContractA::kind,
        }};
        constexpr std::array secondContributions{StaticContributionExpectationV1{
            .contributionId = "service-b.default",
            .contributionKind = ServiceContractA::kind,
        }};
        const std::array factories{
            factoryExpectation("service-a", firstContributions),
            factoryExpectation("service-b", secondContributions),
        };
        recorder.invokeProvider(providerContext(factories), provider);
        recorder.endComposition();
        auto result = std::move(recorder).finish();
        return result ? asharia::host_runtime::StaticFactoryRegistrationError{}
                      : std::move(result.error());
    }

    [[nodiscard]] bool typeConflictAttributionIsOrderIndependent() noexcept {
        const auto forward = collectTypeConflict(&provideTypeConflict);
        const auto reversed = collectTypeConflict(&provideTypeConflictReversed);
        return forward.code ==
                   StaticFactoryRegistrationErrorCode::ContributionContractTypeConflict &&
               forward.code == reversed.code && forward.packageId == reversed.packageId &&
               forward.factoryId == reversed.factoryId &&
               forward.contributionId == reversed.contributionId &&
               forward.factoryId == "service-b" && forward.contributionId == "service-b.default";
    }

    [[nodiscard]] bool cardinalityConflictTakesPriority() noexcept {
        const auto error = collectTypeConflict(&provideCardinalityConflict);
        return error.code ==
                   StaticFactoryRegistrationErrorCode::ContributionContractCardinalityConflict &&
               error.factoryId == "service-b";
    }

    [[nodiscard]] bool repeatedSingleContractIsTableValid() noexcept {
        constexpr auto capacity = registrationCapacity(1, 2, kTextCapacity, 256, 2);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return false;
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));
        constexpr std::array firstContributions{StaticContributionExpectationV1{
            .contributionId = "service-a.default",
            .contributionKind = ServiceContractA::kind,
        }};
        constexpr std::array secondContributions{StaticContributionExpectationV1{
            .contributionId = "service-b.default",
            .contributionKind = ServiceContractA::kind,
        }};
        const std::array factories{
            factoryExpectation("service-a", firstContributions),
            factoryExpectation("service-b", secondContributions),
        };
        recorder.invokeProvider(providerContext(factories), &provideSameSingleContractTwice);
        recorder.endComposition();
        const auto result = std::move(recorder).finish();
        return result && result->registrationSnapshot().registrations.size() == 2;
    }

    [[nodiscard]] bool nonCanonicalContributionExpectationsFailBeforeProvider() noexcept {
        constexpr auto capacity = registrationCapacity(1, 1, kTextCapacity, 256, 2);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return false;
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));
        constexpr std::array contributions{
            StaticContributionExpectationV1{
                .contributionId = "service-z.default",
                .contributionKind = ServiceContractA::kind,
            },
            StaticContributionExpectationV1{
                .contributionId = "service-a.default",
                .contributionKind = ServiceContractA::kind,
            },
        };
        const std::array factories{factoryExpectation("service-a", contributions)};
        recorder.invokeProvider(providerContext(factories), &provideNoFactories);
        recorder.endComposition();
        const auto result = std::move(recorder).finish();
        return !result && result.error().code ==
                              StaticFactoryRegistrationErrorCode::ExpectedContributionsNotCanonical;
    }

    [[nodiscard]] bool oversizedDiagnosticContributionIdFailsExplicitly() noexcept {
        constexpr auto capacity = registrationCapacity(1, 1, kTextCapacity, 256, 1, 4);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return false;
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));
        constexpr std::string_view kContributionId{"service-a.default"};
        constexpr std::array contributions{StaticContributionExpectationV1{
            .contributionId = kContributionId,
            .contributionKind = ServiceContractA::kind,
        }};
        const std::array factories{factoryExpectation("service-a", contributions)};
        recorder.invokeProvider(providerContext(factories), &provideMissingContribution);
        recorder.endComposition();
        const auto result = std::move(recorder).finish();
        return !result &&
               result.error().code ==
                   StaticFactoryRegistrationErrorCode::DiagnosticContributionIdCapacityExceeded &&
               result.error().contributionId.empty() &&
               result.error().observedContributionIdBytes == kContributionId.size();
    }

    [[nodiscard]] bool privateTypeEvidenceTracksCanonicalSnapshotIndices() noexcept {
        constexpr auto capacity = registrationCapacity(1, 2, kTextCapacity, 256, 3);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return false;
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));
        constexpr std::array firstContributions{StaticContributionExpectationV1{
            .contributionId = "service-a.default",
            .contributionKind = ServiceContractA::kind,
        }};
        constexpr std::array secondContributions{
            StaticContributionExpectationV1{
                .contributionId = "other.default",
                .contributionKind = OtherServiceContract::kind,
            },
            StaticContributionExpectationV1{
                .contributionId = "service-b.default",
                .contributionKind = ServiceContractA::kind,
            },
        };
        const std::array factories{
            factoryExpectation("service-a", firstContributions),
            factoryExpectation("service-b", secondContributions),
        };
        recorder.invokeProvider(providerContext(factories),
                                &provideUnorderedContributionEvidence);
        recorder.endComposition();
        auto result = std::move(recorder).finish();
        if (!result) {
            return false;
        }
        const auto& snapshot = result->registrationSnapshot();
        const auto evidence = asharia::host_runtime::StaticFactoryCallbackTablePrivateAccessV1::
            contributionTypeEvidence(*result);
        return snapshot.registrations.size() == 2 &&
               snapshot.registrations[0].factoryId == "service-a" &&
               snapshot.registrations[1].factoryId == "service-b" &&
               snapshot.registrations[1].contributions.size() == 2 &&
               snapshot.registrations[1].contributions[0].contributionId == "other.default" &&
               evidence.size() == 3 && evidence[0].registrationIndex == 0 &&
               evidence[0].contributionIndex == 0 && evidence[1].registrationIndex == 1 &&
               evidence[1].contributionIndex == 0 && evidence[2].registrationIndex == 1 &&
               evidence[2].contributionIndex == 1 && evidence[0].typeKey != nullptr &&
               evidence[1].typeKey != nullptr && evidence[0].typeKey == evidence[2].typeKey &&
               evidence[0].typeKey != evidence[1].typeKey;
    }

} // namespace

int main(int argumentCount, char** arguments) noexcept {
    if (argumentCount == 2 &&
        // argc validation makes this standard argv indexing safe.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        std::string_view{arguments[1]} == "--probe-valid-token-drop") {
        asharia::host_runtime::tests::runValidTokenDropProbe();
    }

    using Test = bool (*)() noexcept;
    constexpr std::array<Test, 23> tests{
        &asharia::host_runtime::tests::runStaticFactoryCallbackTableTests,
        &validRegistrationOwnsCanonicalSnapshot,
        &equivalentOrdersProduceCanonicalSnapshot,
        &unknownFactoryFailsAtomically,
        &missingFactoryFailsAtomically,
        &duplicateFactoryFailsAtomically,
        &registrationOutsideProviderFails,
        &emptyCompositionIsValid,
        &providerCountMismatchFails,
        &stickyFirstFailureWins,
        &endBeforeBeginFails,
        &mixedCompositionEvidenceFailsAtomically,
        &textCapacityExhaustionFails,
        &duplicateProviderTakesPriorityOverCapacity,
        &oversizedDiagnosticFactoryIdFailsExplicitly,
        &selectedSubsetMaterializesTypedMetadata,
        &contributionBindingFailuresAreExplicit,
        &typeConflictAttributionIsOrderIndependent,
        &cardinalityConflictTakesPriority,
        &repeatedSingleContractIsTableValid,
        &nonCanonicalContributionExpectationsFailBeforeProvider,
        &oversizedDiagnosticContributionIdFailsExplicitly,
        &privateTypeEvidenceTracksCanonicalSnapshotIndices,
    };

    bool succeeded = true;
    for (const Test test : tests) {
        if (!test()) {
            succeeded = false;
        }
    }
    return succeeded ? 0 : 1;
}
