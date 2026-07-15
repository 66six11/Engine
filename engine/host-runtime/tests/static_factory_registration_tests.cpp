#include <array>
#include <string_view>
#include <utility>

#include "asharia/host_runtime/static_factory_registration.hpp"

namespace {

    using asharia::host_runtime::StaticCompositionRegistrationContextV1;
    using asharia::host_runtime::StaticFactoryProviderContextV1;
    using asharia::host_runtime::StaticFactoryRegistrar;
    using asharia::host_runtime::StaticFactoryRegistrationCapacityV1;
    using asharia::host_runtime::StaticFactoryRegistrationErrorCode;

    constexpr std::string_view kGenerationId =
        "sha256-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    constexpr std::string_view kBlueprintSha256 =
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    constexpr std::size_t kTextCapacity = 1024;

    StaticFactoryRegistrar*
        capturedRegistrar{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

    [[nodiscard]] constexpr StaticFactoryRegistrationCapacityV1
    registrationCapacity(std::size_t providerCount, std::size_t factoryCount, std::size_t textBytes,
                         std::size_t diagnosticFactoryIdBytes = 256) noexcept {
        return {
            .providerCount = providerCount,
            .factoryCount = factoryCount,
            .textBytes = textBytes,
            .diagnosticFactoryIdBytes = diagnosticFactoryIdBytes,
        };
    }

    void provideOutOfOrderFactories(StaticFactoryRegistrar& registrar) noexcept {
        registrar.registerFactory("service-b");
        registrar.registerFactory("service-a");
    }

    void provideInOrderFactories(StaticFactoryRegistrar& registrar) noexcept {
        registrar.registerFactory("service-a");
        registrar.registerFactory("service-b");
    }

    void provideToolFactory(StaticFactoryRegistrar& registrar) noexcept {
        registrar.registerFactory("service-c");
    }

    void provideUnknownFactory(StaticFactoryRegistrar& registrar) noexcept {
        registrar.registerFactory("unknown-service");
    }

    void provideSingleFactory(StaticFactoryRegistrar& registrar) noexcept {
        registrar.registerFactory("service-a");
    }

    void provideNoFactories(StaticFactoryRegistrar& unusedRegistrar) noexcept {
        (void)unusedRegistrar;
    }

    void provideDuplicateFactory(StaticFactoryRegistrar& registrar) noexcept {
        registrar.registerFactory("service-a");
        registrar.registerFactory("service-a");
    }

    void provideAndCaptureRegistrar(StaticFactoryRegistrar& registrar) noexcept {
        registrar.registerFactory("service-a");
        capturedRegistrar = &registrar;
    }

    [[nodiscard]] StaticCompositionRegistrationContextV1
    compositionContext(StaticFactoryRegistrationCapacityV1 capacity) noexcept {
        return {
            .generationId = kGenerationId,
            .hostActivationBlueprintSha256 = kBlueprintSha256,
            .capacity = capacity,
        };
    }

    [[nodiscard]] StaticFactoryProviderContextV1
    providerContext(std::span<const std::string_view> factories) noexcept {
        return {
            .packageId = "com.asharia.test.runtime",
            .packageVersion = "1.0.0",
            .moduleId = "runtime",
            .entryPoint = "asharia::test::provideRuntimeFactories",
            .expectedFactoryIds = factories,
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

        constexpr std::array<std::string_view, 2> firstFactories{"service-a", "service-b"};
        recorder.invokeProvider(providerContext(firstFactories), &provideOutOfOrderFactories);

        constexpr std::array<std::string_view, 1> secondFactories{"service-c"};
        const StaticFactoryProviderContextV1 secondProvider{
            .packageId = "com.asharia.test.tools",
            .packageVersion = "2.0.0",
            .moduleId = "tools",
            .entryPoint = "asharia::test::provideToolFactories",
            .expectedFactoryIds = secondFactories,
        };
        recorder.invokeProvider(secondProvider, &provideToolFactory);
        recorder.endComposition();

        auto snapshotResult = std::move(recorder).finish();
        if (!snapshotResult) {
            return false;
        }
        const auto& snapshot = *snapshotResult;
        return snapshot.generationId == kGenerationId &&
               snapshot.hostActivationBlueprintSha256 == kBlueprintSha256 &&
               snapshot.registrations.size() == 3 &&
               snapshot.registrations[0].factoryId == "service-a" &&
               snapshot.registrations[1].factoryId == "service-b" &&
               snapshot.registrations[2].packageId == "com.asharia.test.tools" &&
               snapshot.registrations[2].factoryId == "service-c";
    }

    [[nodiscard]] asharia::host_runtime::StaticFactoryRegistrationResult<
        asharia::host_runtime::StaticFactoryRegistrationSnapshotV1>
    collectEquivalentSnapshot(bool reverseProviderOrder) noexcept {
        constexpr auto capacity = registrationCapacity(2, 3, kTextCapacity);
        auto recorderResult =
            asharia::host_runtime::createStaticFactoryRegistrationRecorder(capacity);
        if (!recorderResult) {
            return std::unexpected(std::move(recorderResult.error()));
        }
        auto recorder = std::move(*recorderResult);
        recorder.beginComposition(compositionContext(capacity));

        constexpr std::array<std::string_view, 2> runtimeFactories{"service-a", "service-b"};
        constexpr std::array<std::string_view, 1> toolFactories{"service-c"};
        const StaticFactoryProviderContextV1 runtimeProvider = providerContext(runtimeFactories);
        const StaticFactoryProviderContextV1 toolProvider{
            .packageId = "com.asharia.test.tools",
            .packageVersion = "2.0.0",
            .moduleId = "tools",
            .entryPoint = "asharia::test::provideToolFactories",
            .expectedFactoryIds = toolFactories,
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
        return forward && reversed && *forward == *reversed;
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
        constexpr std::array<std::string_view, 1> factories{"service-a"};
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
        constexpr std::array<std::string_view, 1> factories{"service-a"};
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
        constexpr std::array<std::string_view, 1> factories{"service-a"};
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
        constexpr std::array<std::string_view, 1> factories{"service-a"};
        recorder.invokeProvider(providerContext(factories), &provideAndCaptureRegistrar);
        if (capturedRegistrar == nullptr) {
            return false;
        }
        auto movedRecorder = std::move(recorder);
        capturedRegistrar->registerFactory("service-a");
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
        return result && result->registrations.empty();
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
        constexpr std::array<std::string_view, 1> factories{"service-a"};
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
        constexpr std::array<std::string_view, 1> factories{"service-a"};
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
        constexpr std::array<std::string_view, 1> factories{"service-a"};
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
        constexpr std::array<std::string_view, 1> factories{"service-a"};
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

} // namespace

int main() noexcept {
    using Test = bool (*)() noexcept;
    constexpr std::array<Test, 13> tests{
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
        &textCapacityExhaustionFails,
        &duplicateProviderTakesPriorityOverCapacity,
        &oversizedDiagnosticFactoryIdFailsExplicitly,
    };

    bool succeeded = true;
    for (const Test test : tests) {
        if (!test()) {
            succeeded = false;
        }
    }
    return succeeded ? 0 : 1;
}
