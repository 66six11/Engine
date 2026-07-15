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
        };

        struct PendingFailure final {
            StaticFactoryRegistrationErrorCode code;
            ProviderObservation provider;
            std::size_t factoryIdSize{};
            std::size_t observedFactoryIdBytes{};
        };

        explicit StaticFactoryRegistrationState(
            StaticFactoryRegistrationCapacityV1 requestedCapacity);

        [[nodiscard]] std::string_view copyText(std::string_view value) noexcept;
        void fail(StaticFactoryRegistrationErrorCode code,
                  const ProviderObservation* provider = nullptr,
                  std::string_view factoryId = {}) noexcept;
        [[nodiscard]] StaticFactoryRegistrationError owningFailure() const noexcept;
        void registerFactory(std::string_view localFactoryId) noexcept;

        StaticFactoryRegistrationCapacityV1 capacity;
        std::vector<char> textStorage;
        std::vector<char> failureFactoryIdStorage;
        std::size_t textBytesUsed{};
        std::vector<ProviderObservation> providers;
        std::vector<FactoryObservation> factories;
        // Registrar lives with heap-stable state so Recorder moves cannot invalidate it.
        StaticFactoryRegistrar registrar;
        std::size_t observedProviderCount{};
        std::size_t observedFactoryCount{};
        std::size_t expectedFactoryCount{};
        std::string_view generationId;
        std::string_view hostActivationBlueprintSha256;
        std::span<const std::string_view> activeExpectedFactoryIds;
        std::size_t activeFactoryBegin{};
        std::size_t activeProviderIndex{};
        bool providerActive{};
        Phase phase{Phase::NotStarted};
        std::optional<PendingFailure> failure;
    };

} // namespace asharia::host_runtime
