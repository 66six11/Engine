#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "asharia/host_runtime/static_factory_registration.hpp"

namespace asharia::host_runtime {

    [[nodiscard]] StaticFactoryRegistrationError
    makeStaticFactoryRegistrationError(StaticFactoryRegistrationErrorCode code) noexcept;

    class StaticFactoryRegistrationState final {
    public:
        enum class Phase : std::uint8_t {
            NotStarted,
            Recording,
            Ended,
            Finished,
        };

        struct ProviderObservation final {
            std::string_view packageId;
            std::string_view packageVersion;
            std::string_view moduleId;
            std::string_view entryPoint;
        };

        struct FactoryObservation final {
            std::string_view packageId;
            std::string_view packageVersion;
            std::string_view moduleId;
            std::string_view factoryId;
            std::string_view providerEntryPoint;
            StaticFactoryCallbacksV1 callbacks;
        };

        struct ContributionObservation final {
            std::size_t factoryIndex{};
            std::string_view contributionId;
            std::string_view contributionKind;
            StaticContributionCardinalityV1 cardinality{};
            const void* typeKey{};
        };

        struct PendingFailure final {
            StaticFactoryRegistrationErrorCode code;
            ProviderObservation provider;
            std::size_t factoryIdSize{};
            std::size_t contributionIdSize{};
            std::size_t observedFactoryIdBytes{};
            std::size_t observedContributionIdBytes{};
        };

        explicit StaticFactoryRegistrationState(
            StaticFactoryRegistrationCapacityV2 requestedCapacity);

        [[nodiscard]] std::string_view copyText(std::string_view value) noexcept;
        void fail(StaticFactoryRegistrationErrorCode code,
                  const ProviderObservation* provider = nullptr, std::string_view factoryId = {},
                  std::string_view contributionId = {}) noexcept;
        [[nodiscard]] StaticFactoryRegistrationError owningFailure() const noexcept;
        void registerFactory(
            std::string_view localFactoryId, StaticFactoryCallbacksV1 callbacks,
            std::span<const StaticContributionBindingV1> availableContributions) noexcept;
        void validateContributionContracts() noexcept;

        [[nodiscard]] bool validateFactoryCallbacks(
            const ProviderObservation& provider, std::string_view localFactoryId,
            StaticFactoryCallbacksV1 callbacks) noexcept;
        [[nodiscard]] bool validateAvailableContributionBindings(
            const ProviderObservation& provider, std::string_view localFactoryId,
            std::span<const StaticContributionBindingV1> availableContributions) noexcept;
        [[nodiscard]] bool validateSelectedContributionCapacity(
            const ProviderObservation& provider, std::string_view localFactoryId,
            const StaticFactoryExpectationV1& expected) noexcept;
        [[nodiscard]] bool recordSelectedContributions(
            std::size_t factoryIndex, const ProviderObservation& provider,
            std::string_view localFactoryId, const StaticFactoryExpectationV1& expected,
            std::span<const StaticContributionBindingV1> availableContributions) noexcept;

        StaticFactoryRegistrationCapacityV2 capacity;
        std::vector<char> textStorage;
        std::vector<char> failureFactoryIdStorage;
        std::vector<char> failureContributionIdStorage;
        std::size_t textBytesUsed{};
        std::vector<ProviderObservation> providers;
        std::vector<FactoryObservation> factories;
        std::vector<ContributionObservation> contributions;
        // Registrar lives with heap-stable state so Recorder moves cannot invalidate it.
        StaticFactoryRegistrar registrar;
        std::size_t observedProviderCount{};
        std::size_t observedFactoryCount{};
        std::size_t observedContributionCount{};
        std::size_t expectedFactoryCount{};
        std::size_t expectedContributionCount{};
        std::string_view generationId;
        std::string_view hostActivationBlueprintSha256;
        std::span<const StaticFactoryExpectationV1> activeExpectedFactories;
        std::size_t activeFactoryBegin{};
        std::size_t activeProviderIndex{};
        bool providerActive{};
        Phase phase{Phase::NotStarted};
        std::optional<PendingFailure> failure;
    };

} // namespace asharia::host_runtime
