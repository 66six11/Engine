#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>

#include "asharia/host_runtime/static_factory_callbacks.hpp"

namespace asharia::host_runtime {

    struct ProcessContributionRegistryGenerationStateV1;

    enum class ProcessContributionPublicationErrorCodeV1 : std::uint8_t {
        WrongControlThread,
        ProcessEpochStale,
        RegistryNotStaging,
        OwnerFactoryOutOfRange,
        FactoryHasNoContributions,
        FactoryLeaseAlreadyCommitted,
        FactoryInstanceInvalid,
        PayloadAccessorMissing,
        PayloadNull,
    };

    struct ProcessContributionPublicationErrorV1 final {
        ProcessContributionPublicationErrorCodeV1 code{
            ProcessContributionPublicationErrorCodeV1::RegistryNotStaging};
        std::size_t factoryIndex{};
        std::size_t slotIndex{};
    };

    class ProcessContributionActivationLeaseV1 final {
    public:
        ~ProcessContributionActivationLeaseV1() noexcept;

        ProcessContributionActivationLeaseV1(ProcessContributionActivationLeaseV1&& other) noexcept;
        ProcessContributionActivationLeaseV1&
        operator=(ProcessContributionActivationLeaseV1&&) = delete;
        ProcessContributionActivationLeaseV1(const ProcessContributionActivationLeaseV1&) = delete;
        ProcessContributionActivationLeaseV1&
        operator=(const ProcessContributionActivationLeaseV1&) = delete;

        [[nodiscard]] bool isCommitted() const noexcept {
            return requiresRevoke_;
        }

        // A committed lease must be revoked exactly once during the registry's
        // Revoking phase. Dropping it while committed is a lifecycle bug.
        void revoke() noexcept;

    private:
        ProcessContributionActivationLeaseV1(
            std::shared_ptr<ProcessContributionRegistryGenerationStateV1> generation,
            std::size_t factoryIndex) noexcept
            : generation_(std::move(generation)), factoryIndex_(factoryIndex),
              requiresRevoke_(true) {}

        std::shared_ptr<ProcessContributionRegistryGenerationStateV1> generation_;
        std::size_t factoryIndex_{};
        bool requiresRevoke_{};

        friend std::expected<ProcessContributionActivationLeaseV1,
                             ProcessContributionPublicationErrorV1>
        publishProcessFactoryContributionsV1(
            const std::shared_ptr<ProcessContributionRegistryGenerationStateV1>& generation,
            std::size_t factoryIndex, FactoryInstanceViewV1 instance) noexcept;
    };

    [[nodiscard]] std::expected<ProcessContributionActivationLeaseV1,
                                ProcessContributionPublicationErrorV1>
    publishProcessFactoryContributionsV1(
        const std::shared_ptr<ProcessContributionRegistryGenerationStateV1>& generation,
        std::size_t factoryIndex, FactoryInstanceViewV1 instance) noexcept;

} // namespace asharia::host_runtime
