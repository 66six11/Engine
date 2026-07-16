#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/host_runtime/process_contribution_registry.hpp"

namespace asharia::host_runtime {

    struct ControlThreadEpochAnchorV1;
    struct CurrentProcessEpochAnchorV1;

    enum class ProcessContributionRegistryPhaseV1 : std::uint8_t {
        Staging,
        Active,
        Revoking,
        Revoked,
    };

    struct ProcessContributionRegistrySlotPlanV1 final {
        std::size_t ownerFactoryIndex{};
        std::string_view contributionId;
        std::string_view contributionKind;
        StaticContributionCardinalityV1 cardinality{};
        const void* typeKey{};
        detail::ErasedStaticContributionPayloadAccessorV1 payloadAccessor{};
    };

    enum class ProcessContributionRegistryPreparationErrorCodeV1 : std::uint8_t {
        WrongControlThread,
        ProcessEpochStale,
        SlotInvalid,
        OwnerFactoryOutOfRange,
        OwnerFactoryOrderInvalid,
        ContractCardinalityConflict,
        ContractTypeConflict,
        SingleContractConflict,
        AllocationFailed,
    };

    struct ProcessContributionRegistryPreparationErrorV1 final {
        ProcessContributionRegistryPreparationErrorCodeV1 code{
            ProcessContributionRegistryPreparationErrorCodeV1::SlotInvalid};
        std::size_t slotIndex{};
        std::size_t conflictingSlotIndex{};
    };

    enum class ProcessContributionRegistryTransitionErrorCodeV1 : std::uint8_t {
        WrongControlThread,
        ProcessEpochStale,
        RegistryNotStaging,
        FactoryPublicationIncomplete,
        SlotPublicationIncomplete,
    };

    struct ProcessContributionRegistryTransitionErrorV1 final {
        ProcessContributionRegistryTransitionErrorCodeV1 code{
            ProcessContributionRegistryTransitionErrorCodeV1::RegistryNotStaging};
        std::size_t factoryIndex{};
        std::size_t slotIndex{};
    };

    struct ProcessContributionRegistrySlotStateV1 final {
        std::size_t ownerFactoryIndex{};
        std::string contributionId;
        std::string contributionKind;
        StaticContributionCardinalityV1 cardinality{};
        const void* typeKey{};
        detail::ErasedStaticContributionPayloadAccessorV1 payloadAccessor{};
        void* payload{};
        bool committed{};
    };

    struct ProcessContributionFactorySlotRangeStateV1 final {
        std::size_t firstSlot{};
        std::size_t slotCount{};
        bool leaseCommitted{};
        bool leaseRevoked{};
    };

    struct ProcessContributionRegistryGenerationStateV1 final {
        ProcessContributionRegistryGenerationStateV1(
            std::shared_ptr<const CurrentProcessEpochAnchorV1> processEpochValue,
            std::shared_ptr<const ControlThreadEpochAnchorV1> controlThreadEpochValue,
            std::vector<ProcessContributionRegistrySlotStateV1> slotsValue,
            std::vector<ProcessContributionFactorySlotRangeStateV1> factorySlotsValue) noexcept
            : processEpoch(std::move(processEpochValue)),
              controlThreadEpoch(std::move(controlThreadEpochValue)), slots(std::move(slotsValue)),
              factorySlots(std::move(factorySlotsValue)) {}

        std::shared_ptr<const CurrentProcessEpochAnchorV1> processEpoch;
        std::shared_ptr<const ControlThreadEpochAnchorV1> controlThreadEpoch;
        std::vector<ProcessContributionRegistrySlotStateV1> slots;
        std::vector<ProcessContributionFactorySlotRangeStateV1> factorySlots;
        ProcessContributionRegistryPhaseV1 phase{ProcessContributionRegistryPhaseV1::Staging};
    };

    using ProcessContributionRegistryPreparationResultV1 =
        std::expected<std::shared_ptr<ProcessContributionRegistryGenerationStateV1>,
                      ProcessContributionRegistryPreparationErrorV1>;

    [[nodiscard]] ProcessContributionRegistryPreparationResultV1
    prepareProcessContributionRegistryV1(
        std::size_t factoryCount, std::span<const ProcessContributionRegistrySlotPlanV1> slotPlans,
        std::shared_ptr<const CurrentProcessEpochAnchorV1> processEpoch,
        std::shared_ptr<const ControlThreadEpochAnchorV1> controlThreadEpoch) noexcept;

    [[nodiscard]] std::size_t processFactoryContributionSlotCountV1(
        const std::shared_ptr<ProcessContributionRegistryGenerationStateV1>& generation,
        std::size_t factoryIndex) noexcept;

    [[nodiscard]] std::expected<void, ProcessContributionRegistryTransitionErrorV1>
    openProcessContributionRegistryV1(
        const std::shared_ptr<ProcessContributionRegistryGenerationStateV1>& generation) noexcept;

    // Cleanup owns these transitions. Any failure is an executor invariant violation,
    // so the mutation path is deliberately fail-fast instead of partially revoking.
    void beginProcessContributionRegistryRevocationV1(
        const std::shared_ptr<ProcessContributionRegistryGenerationStateV1>& generation) noexcept;

    void finishProcessContributionRegistryRevocationV1(
        const std::shared_ptr<ProcessContributionRegistryGenerationStateV1>& generation) noexcept;

    class ProcessContributionRegistryStateAccessV1 final {
    public:
        [[nodiscard]] static ProcessContributionRegistryViewV1
        view(const std::shared_ptr<ProcessContributionRegistryGenerationStateV1>&
                 generation) noexcept {
            if (!generation) {
                return {};
            }
            return ProcessContributionRegistryViewV1{generation};
        }
    };

} // namespace asharia::host_runtime
