#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "asharia/host_runtime/static_factory_registration.hpp"

namespace asharia::host_runtime::tests {

    inline constexpr std::string_view kSyntheticPackageId{"com.asharia.test.process-scope"};
    inline constexpr std::string_view kSyntheticPackageVersion{"1.0.0"};
    inline constexpr std::string_view kSyntheticModuleId{"runtime"};
    inline constexpr std::string_view kSyntheticProviderEntryPoint{
        "asharia::host_runtime::tests::provideSyntheticProcessScopeFactories"};
    inline constexpr std::string_view kSyntheticEngineGenerationId{
        "sha256-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
    inline constexpr std::string_view kSyntheticCompositionGenerationId{
        "sha256-cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"};
    inline constexpr std::string_view kSyntheticBlueprintSha256{
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"};

    inline constexpr std::string_view kSyntheticMiddleFactoryId{"a-middle"};
    inline constexpr std::string_view kSyntheticProjectOnlyFactoryId{"b-project-only"};
    inline constexpr std::string_view kSyntheticEmptyFactoryId{"c-empty"};
    inline constexpr std::string_view kSyntheticLeafFactoryId{"m-leaf"};
    inline constexpr std::string_view kSyntheticRootFactoryId{"z-root"};

    inline constexpr std::string_view kSyntheticRootPrimaryContributionId{"root.primary"};
    inline constexpr std::string_view kSyntheticProjectOnlyPrimaryContributionId{
        "project-only.primary"};
    inline constexpr std::string_view kSyntheticMiddleExtensionAContributionId{
        "middle.extension-a"};
    inline constexpr std::string_view kSyntheticMiddleExtensionBContributionId{
        "middle.extension-b"};
    inline constexpr std::string_view kSyntheticLeafExtensionContributionId{"leaf.extension"};

    enum class SyntheticFactoryV1 : std::uint8_t {
        Middle,
        ProjectOnly,
        Empty,
        Leaf,
        Root,
        Count,
    };

    enum class SyntheticLifecyclePhaseV1 : std::uint8_t {
        Create,
        Activate,
        Quiesce,
        Deactivate,
        Destroy,
        Count,
    };

    enum class SyntheticContributionSlotV1 : std::uint8_t {
        Primary,
        ExtensionA,
        ExtensionB,
        Count,
    };

    struct SyntheticPrimaryServiceContractV1 final {
        static constexpr std::string_view kind{"com.asharia.test.process-scope.primary-service"};
        static constexpr StaticContributionCardinalityV1 cardinality{
            StaticContributionCardinalityV1::Single};

        SyntheticFactoryV1 owner{SyntheticFactoryV1::Count};
        std::string_view contributionId{};
    };

    struct SyntheticExtensionContractV1 final {
        static constexpr std::string_view kind{"com.asharia.test.process-scope.extension"};
        static constexpr StaticContributionCardinalityV1 cardinality{
            StaticContributionCardinalityV1::Multiple};

        SyntheticFactoryV1 owner{SyntheticFactoryV1::Count};
        std::string_view contributionId{};
    };

    using SyntheticLifecycleHookV1 = void (*)(SyntheticFactoryV1 factory,
                                              SyntheticLifecyclePhaseV1 phase) noexcept;

    inline constexpr std::size_t kSyntheticFactoryCount =
        static_cast<std::size_t>(SyntheticFactoryV1::Count);
    inline constexpr std::size_t kSyntheticLifecyclePhaseCount =
        static_cast<std::size_t>(SyntheticLifecyclePhaseV1::Count);
    inline constexpr std::size_t kSyntheticContributionSlotCount =
        static_cast<std::size_t>(SyntheticContributionSlotV1::Count);
    inline constexpr std::size_t kSyntheticTraceCapacity = 32;

    inline constexpr std::uint8_t kSyntheticMiddleDependencyMask =
        std::uint8_t{1} << static_cast<std::uint8_t>(SyntheticFactoryV1::Root);
    inline constexpr std::uint8_t kSyntheticLeafDependencyMask =
        kSyntheticMiddleDependencyMask |
        (std::uint8_t{1} << static_cast<std::uint8_t>(SyntheticFactoryV1::Middle));

    struct SyntheticLifecycleEventV1 final {
        SyntheticFactoryV1 factory{SyntheticFactoryV1::Middle};
        SyntheticLifecyclePhaseV1 phase{SyntheticLifecyclePhaseV1::Create};

        [[nodiscard]] friend constexpr bool
        operator==(const SyntheticLifecycleEventV1&,
                   const SyntheticLifecycleEventV1&) noexcept = default;
    };

    struct SyntheticFactoryObservationV1 final {
        std::array<std::size_t, kSyntheticLifecyclePhaseCount> invocationCounts{};
        std::uint8_t expectedDependencyMask{};
        std::uint8_t observedDependencyMask{};
        std::size_t tokenIssueCount{};
        std::size_t destroyCount{};
        bool attributionMismatch{};
        bool generationMismatch{};
        bool dependencyMismatch{};
        bool instanceMismatch{};
        bool duplicateTokenIssue{};
        bool duplicateDestroy{};
        bool tokenOutstanding{};
    };

    struct SyntheticContributionAccessorObservationV1 final {
        std::size_t invocationCount{};
        std::size_t nullReturnCount{};
        bool nullReturnArmed{};
    };

    [[nodiscard]] std::string_view syntheticFactoryId(SyntheticFactoryV1 factory) noexcept;
    [[nodiscard]] std::uint8_t syntheticExpectedDependencyMask(SyntheticFactoryV1 factory) noexcept;

    [[nodiscard]] StaticFactoryRegistrationCapacityV2 syntheticRegistrationCapacity() noexcept;
    void recordSyntheticFactoryProviders(StaticFactoryRegistrationRecorder& recorder) noexcept;

    void resetSyntheticProviderFixture() noexcept;
    void clearSyntheticProviderFailures() noexcept;

    // The next recorded lifecycle event invokes this hook once. Reentrant
    // lifecycle events are still traced, but cannot invoke the hook again.
    void armSyntheticProviderLifecycleHook(SyntheticLifecycleHookV1 hook) noexcept;

    // Failure slots accumulate until cleared, so one test can inject failures in
    // several cleanup callbacks and verify that all reverse passes continue.
    [[nodiscard]] bool injectSyntheticProviderFailure(SyntheticFactoryV1 factory,
                                                      SyntheticLifecyclePhaseV1 phase,
                                                      std::uint32_t localCode) noexcept;

    // A valid factory/slot pair returns null on its next accessor invocation only.
    [[nodiscard]] bool
    injectSyntheticContributionNullOnce(SyntheticFactoryV1 factory,
                                        SyntheticContributionSlotV1 contribution) noexcept;

    [[nodiscard]] std::span<const SyntheticLifecycleEventV1> syntheticProviderTrace() noexcept;
    [[nodiscard]] std::size_t
    syntheticProviderInvocationCount(SyntheticFactoryV1 factory,
                                     SyntheticLifecyclePhaseV1 phase) noexcept;
    [[nodiscard]] SyntheticFactoryObservationV1
    syntheticProviderObservation(SyntheticFactoryV1 factory) noexcept;
    [[nodiscard]] SyntheticContributionAccessorObservationV1
    syntheticContributionAccessorObservation(SyntheticFactoryV1 factory,
                                             SyntheticContributionSlotV1 contribution) noexcept;
    [[nodiscard]] std::size_t syntheticProjectOnlyInvocationCount() noexcept;
    [[nodiscard]] bool syntheticProviderTraceOverflowed() noexcept;
    [[nodiscard]] bool syntheticProviderFixtureValid() noexcept;

} // namespace asharia::host_runtime::tests
