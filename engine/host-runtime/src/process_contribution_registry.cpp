#include "asharia/host_runtime/process_contribution_registry.hpp"

#include <exception>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

#include "activation_eligibility_state.hpp"
#include "process_contribution_registry_state.hpp"

namespace asharia::host_runtime {
    namespace {

        [[nodiscard]] ProcessContributionLookupErrorV1
        lookupError(ProcessContributionLookupErrorCodeV1 code) noexcept {
            return {.code = code};
        }

        [[nodiscard]] ProcessContributionRegistryPreparationErrorV1
        preparationError(ProcessContributionRegistryPreparationErrorCodeV1 code,
                         std::size_t slotIndex = 0, std::size_t conflictingSlotIndex = 0) noexcept {
            return {
                .code = code,
                .slotIndex = slotIndex,
                .conflictingSlotIndex = conflictingSlotIndex,
            };
        }

        [[nodiscard]] ProcessContributionRegistryTransitionErrorV1
        transitionError(ProcessContributionRegistryTransitionErrorCodeV1 code,
                        std::size_t factoryIndex = 0, std::size_t slotIndex = 0) noexcept {
            return {
                .code = code,
                .factoryIndex = factoryIndex,
                .slotIndex = slotIndex,
            };
        }

        [[nodiscard]] bool validCardinality(StaticContributionCardinalityV1 cardinality) noexcept {
            return cardinality == StaticContributionCardinalityV1::Single ||
                   cardinality == StaticContributionCardinalityV1::Multiple;
        }

        [[nodiscard]] std::optional<ProcessContributionRegistryPreparationErrorV1>
        validateSlotPlans(
            std::size_t factoryCount,
            std::span<const ProcessContributionRegistrySlotPlanV1> slotPlans) noexcept {
            for (std::size_t index = 0; index < slotPlans.size(); ++index) {
                const ProcessContributionRegistrySlotPlanV1& plan = slotPlans[index];
                if (plan.contributionId.empty() || plan.contributionKind.empty() ||
                    !validCardinality(plan.cardinality) || plan.typeKey == nullptr ||
                    plan.payloadAccessor == nullptr) {
                    return preparationError(
                        ProcessContributionRegistryPreparationErrorCodeV1::SlotInvalid, index);
                }
                if (plan.ownerFactoryIndex >= factoryCount) {
                    return preparationError(
                        ProcessContributionRegistryPreparationErrorCodeV1::OwnerFactoryOutOfRange,
                        index);
                }
                if (index != 0 && slotPlans[index - 1].ownerFactoryIndex > plan.ownerFactoryIndex) {
                    return preparationError(
                        ProcessContributionRegistryPreparationErrorCodeV1::OwnerFactoryOrderInvalid,
                        index, index - 1);
                }

                for (std::size_t previousIndex = 0; previousIndex < index; ++previousIndex) {
                    const ProcessContributionRegistrySlotPlanV1& previous =
                        slotPlans[previousIndex];
                    if (previous.contributionKind != plan.contributionKind) {
                        continue;
                    }
                    if (previous.cardinality != plan.cardinality) {
                        return preparationError(ProcessContributionRegistryPreparationErrorCodeV1::
                                                    ContractCardinalityConflict,
                                                index, previousIndex);
                    }
                    if (previous.typeKey != plan.typeKey) {
                        return preparationError(
                            ProcessContributionRegistryPreparationErrorCodeV1::ContractTypeConflict,
                            index, previousIndex);
                    }
                    if (plan.cardinality == StaticContributionCardinalityV1::Single) {
                        return preparationError(ProcessContributionRegistryPreparationErrorCodeV1::
                                                    SingleContractConflict,
                                                index, previousIndex);
                    }
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] ProcessContributionLookupErrorCodeV1
        unavailableCode(ProcessContributionRegistryPhaseV1 phase) noexcept {
            switch (phase) {
            case ProcessContributionRegistryPhaseV1::Staging:
                return ProcessContributionLookupErrorCodeV1::RegistryNotActive;
            case ProcessContributionRegistryPhaseV1::Revoking:
                return ProcessContributionLookupErrorCodeV1::RegistryRevoking;
            case ProcessContributionRegistryPhaseV1::Revoked:
                return ProcessContributionLookupErrorCodeV1::RegistryRevoked;
            case ProcessContributionRegistryPhaseV1::Active:
                break;
            }
            return ProcessContributionLookupErrorCodeV1::RegistryNotActive;
        }

        [[nodiscard]] std::expected<
            std::shared_ptr<const ProcessContributionRegistryGenerationStateV1>,
            ProcessContributionLookupErrorV1>
        lockActiveGeneration(
            const std::weak_ptr<const ProcessContributionRegistryGenerationStateV1>&
                weak) noexcept {
            std::shared_ptr<const ProcessContributionRegistryGenerationStateV1> generation =
                weak.lock();
            if (!generation) {
                return std::unexpected(
                    lookupError(ProcessContributionLookupErrorCodeV1::RegistryExpired));
            }
            if (!isCurrentControlThread(generation->controlThreadEpoch)) {
                return std::unexpected(
                    lookupError(ProcessContributionLookupErrorCodeV1::WrongControlThread));
            }
            if (!isClaimedCurrentProcessEpoch(generation->processEpoch)) {
                return std::unexpected(
                    lookupError(ProcessContributionLookupErrorCodeV1::ProcessEpochStale));
            }
            if (generation->phase != ProcessContributionRegistryPhaseV1::Active) {
                return std::unexpected(lookupError(unavailableCode(generation->phase)));
            }
            return generation;
        }

        [[nodiscard]] std::expected<std::size_t, ProcessContributionLookupErrorV1>
        validateContract(const ProcessContributionRegistryGenerationStateV1& generation,
                         std::string_view contributionKind,
                         StaticContributionCardinalityV1 cardinality,
                         const void* typeKey) noexcept {
            std::size_t count{};
            for (const ProcessContributionRegistrySlotStateV1& slot : generation.slots) {
                if (slot.contributionKind != contributionKind) {
                    continue;
                }
                if (slot.cardinality != cardinality) {
                    return std::unexpected(lookupError(
                        ProcessContributionLookupErrorCodeV1::ContractCardinalityMismatch));
                }
                if (slot.typeKey != typeKey) {
                    return std::unexpected(
                        lookupError(ProcessContributionLookupErrorCodeV1::ContractTypeMismatch));
                }
                ++count;
            }
            if (count == 0) {
                return std::unexpected(
                    lookupError(ProcessContributionLookupErrorCodeV1::ContractAbsent));
            }
            return count;
        }

        [[nodiscard]] std::optional<std::size_t>
        findOrdinalSlot(const ProcessContributionRegistryGenerationStateV1& generation,
                        std::string_view contributionKind, std::size_t ordinal) noexcept {
            std::size_t currentOrdinal{};
            for (std::size_t index = 0; index < generation.slots.size(); ++index) {
                if (generation.slots[index].contributionKind != contributionKind) {
                    continue;
                }
                if (currentOrdinal == ordinal) {
                    return index;
                }
                ++currentOrdinal;
            }
            return std::nullopt;
        }

        void
        requireMutationAccess(const std::shared_ptr<ProcessContributionRegistryGenerationStateV1>&
                                  generation) noexcept {
            if (!generation || !isCurrentControlThread(generation->controlThreadEpoch) ||
                !isClaimedCurrentProcessEpoch(generation->processEpoch)) {
                std::terminate();
            }
        }

    } // namespace

    ProcessContributionRegistryPreparationResultV1 prepareProcessContributionRegistryV1(
        std::size_t factoryCount, std::span<const ProcessContributionRegistrySlotPlanV1> slotPlans,
        std::shared_ptr<const CurrentProcessEpochAnchorV1> processEpoch,
        std::shared_ptr<const ControlThreadEpochAnchorV1> controlThreadEpoch) noexcept {
        if (!isCurrentControlThread(controlThreadEpoch)) {
            return std::unexpected(preparationError(
                ProcessContributionRegistryPreparationErrorCodeV1::WrongControlThread));
        }
        if (!isClaimedCurrentProcessEpoch(processEpoch)) {
            return std::unexpected(preparationError(
                ProcessContributionRegistryPreparationErrorCodeV1::ProcessEpochStale));
        }
        if (const auto failure = validateSlotPlans(factoryCount, slotPlans)) {
            return std::unexpected(*failure);
        }

        try {
            std::vector<ProcessContributionRegistrySlotStateV1> slots;
            slots.reserve(slotPlans.size());
            std::vector<ProcessContributionFactorySlotRangeStateV1> factorySlots(factoryCount);

            std::size_t nextSlot{};
            for (std::size_t factoryIndex = 0; factoryIndex < factoryCount; ++factoryIndex) {
                ProcessContributionFactorySlotRangeStateV1& range = factorySlots[factoryIndex];
                range.firstSlot = slots.size();
                while (nextSlot < slotPlans.size() &&
                       slotPlans[nextSlot].ownerFactoryIndex == factoryIndex) {
                    const ProcessContributionRegistrySlotPlanV1& plan = slotPlans[nextSlot];
                    slots.push_back({
                        .ownerFactoryIndex = plan.ownerFactoryIndex,
                        .contributionId = std::string(plan.contributionId),
                        .contributionKind = std::string(plan.contributionKind),
                        .cardinality = plan.cardinality,
                        .typeKey = plan.typeKey,
                        .payloadAccessor = plan.payloadAccessor,
                        .payload = nullptr,
                        .committed = false,
                    });
                    ++range.slotCount;
                    ++nextSlot;
                }
                range.leaseCommitted = range.slotCount == 0;
                range.leaseRevoked = range.slotCount == 0;
            }

            return std::make_shared<ProcessContributionRegistryGenerationStateV1>(
                std::move(processEpoch), std::move(controlThreadEpoch), std::move(slots),
                std::move(factorySlots));
        } catch (const std::bad_alloc&) {
            return std::unexpected(preparationError(
                ProcessContributionRegistryPreparationErrorCodeV1::AllocationFailed));
        } catch (const std::length_error&) {
            return std::unexpected(preparationError(
                ProcessContributionRegistryPreparationErrorCodeV1::AllocationFailed));
        }
    }

    std::size_t processFactoryContributionSlotCountV1(
        const std::shared_ptr<ProcessContributionRegistryGenerationStateV1>& generation,
        std::size_t factoryIndex) noexcept {
        requireMutationAccess(generation);
        if (factoryIndex >= generation->factorySlots.size()) {
            std::terminate();
        }
        return generation->factorySlots[factoryIndex].slotCount;
    }

    std::expected<void, ProcessContributionRegistryTransitionErrorV1>
    openProcessContributionRegistryV1(
        const std::shared_ptr<ProcessContributionRegistryGenerationStateV1>& generation) noexcept {
        if (!generation || !isCurrentControlThread(generation->controlThreadEpoch)) {
            return std::unexpected(transitionError(
                ProcessContributionRegistryTransitionErrorCodeV1::WrongControlThread));
        }
        if (!isClaimedCurrentProcessEpoch(generation->processEpoch)) {
            return std::unexpected(transitionError(
                ProcessContributionRegistryTransitionErrorCodeV1::ProcessEpochStale));
        }
        if (generation->phase != ProcessContributionRegistryPhaseV1::Staging) {
            return std::unexpected(transitionError(
                ProcessContributionRegistryTransitionErrorCodeV1::RegistryNotStaging));
        }
        for (std::size_t factoryIndex = 0; factoryIndex < generation->factorySlots.size();
             ++factoryIndex) {
            if (!generation->factorySlots[factoryIndex].leaseCommitted) {
                return std::unexpected(transitionError(
                    ProcessContributionRegistryTransitionErrorCodeV1::FactoryPublicationIncomplete,
                    factoryIndex));
            }
        }
        for (std::size_t slotIndex = 0; slotIndex < generation->slots.size(); ++slotIndex) {
            const ProcessContributionRegistrySlotStateV1& slot = generation->slots[slotIndex];
            if (!slot.committed || slot.payload == nullptr) {
                return std::unexpected(transitionError(
                    ProcessContributionRegistryTransitionErrorCodeV1::SlotPublicationIncomplete,
                    slot.ownerFactoryIndex, slotIndex));
            }
        }

        generation->phase = ProcessContributionRegistryPhaseV1::Active;
        return {};
    }

    void beginProcessContributionRegistryRevocationV1(
        const std::shared_ptr<ProcessContributionRegistryGenerationStateV1>& generation) noexcept {
        requireMutationAccess(generation);
        if (generation->phase != ProcessContributionRegistryPhaseV1::Staging &&
            generation->phase != ProcessContributionRegistryPhaseV1::Active) {
            std::terminate();
        }
        generation->phase = ProcessContributionRegistryPhaseV1::Revoking;
    }

    void finishProcessContributionRegistryRevocationV1(
        const std::shared_ptr<ProcessContributionRegistryGenerationStateV1>& generation) noexcept {
        requireMutationAccess(generation);
        if (generation->phase != ProcessContributionRegistryPhaseV1::Revoking) {
            std::terminate();
        }
        for (const ProcessContributionFactorySlotRangeStateV1& range : generation->factorySlots) {
            if (range.leaseCommitted && !range.leaseRevoked) {
                std::terminate();
            }
        }
        for (ProcessContributionRegistrySlotStateV1& slot : generation->slots) {
            slot.payload = nullptr;
            slot.committed = false;
        }
        generation->phase = ProcessContributionRegistryPhaseV1::Revoked;
    }

    std::expected<std::size_t, ProcessContributionLookupErrorV1>
    ProcessContributionRegistryViewV1::sizeErased(std::string_view contributionKind,
                                                  StaticContributionCardinalityV1 cardinality,
                                                  const void* typeKey) const noexcept {
        const auto generation = lockActiveGeneration(generation_);
        if (!generation) {
            return std::unexpected(generation.error());
        }
        return validateContract(**generation, contributionKind, cardinality, typeKey);
    }

    std::expected<ProcessContributionHandleCoreV1, ProcessContributionLookupErrorV1>
    ProcessContributionRegistryViewV1::atErased(std::string_view contributionKind,
                                                StaticContributionCardinalityV1 cardinality,
                                                const void* typeKey,
                                                std::size_t ordinal) const noexcept {
        const auto generation = lockActiveGeneration(generation_);
        if (!generation) {
            return std::unexpected(generation.error());
        }
        const auto count = validateContract(**generation, contributionKind, cardinality, typeKey);
        if (!count) {
            return std::unexpected(count.error());
        }
        if (cardinality != StaticContributionCardinalityV1::Multiple) {
            return std::unexpected(
                lookupError(ProcessContributionLookupErrorCodeV1::ContractCardinalityMismatch));
        }
        if (ordinal >= *count) {
            return std::unexpected(
                lookupError(ProcessContributionLookupErrorCodeV1::OrdinalOutOfRange));
        }
        const std::optional<std::size_t> slotIndex =
            findOrdinalSlot(**generation, contributionKind, ordinal);
        if (!slotIndex) {
            return std::unexpected(
                lookupError(ProcessContributionLookupErrorCodeV1::PayloadUnavailable));
        }
        return ProcessContributionHandleCoreV1{*generation, *slotIndex};
    }

    std::expected<ProcessContributionHandleCoreV1, ProcessContributionLookupErrorV1>
    ProcessContributionRegistryViewV1::singleErased(std::string_view contributionKind,
                                                    StaticContributionCardinalityV1 cardinality,
                                                    const void* typeKey) const noexcept {
        const auto generation = lockActiveGeneration(generation_);
        if (!generation) {
            return std::unexpected(generation.error());
        }
        const auto count = validateContract(**generation, contributionKind, cardinality, typeKey);
        if (!count) {
            return std::unexpected(count.error());
        }
        if (cardinality != StaticContributionCardinalityV1::Single || *count != 1) {
            return std::unexpected(
                lookupError(ProcessContributionLookupErrorCodeV1::ContractCardinalityMismatch));
        }
        const std::optional<std::size_t> slotIndex =
            findOrdinalSlot(**generation, contributionKind, 0);
        if (!slotIndex) {
            return std::unexpected(
                lookupError(ProcessContributionLookupErrorCodeV1::PayloadUnavailable));
        }
        return ProcessContributionHandleCoreV1{*generation, *slotIndex};
    }

    std::expected<void*, ProcessContributionLookupErrorV1>
    ProcessContributionHandleCoreV1::tryBorrowErased(std::string_view contributionKind,
                                                     StaticContributionCardinalityV1 cardinality,
                                                     const void* typeKey) const noexcept {
        const auto generation = lockActiveGeneration(generation_);
        if (!generation) {
            return std::unexpected(generation.error());
        }
        if (slotIndex_ >= (*generation)->slots.size()) {
            return std::unexpected(
                lookupError(ProcessContributionLookupErrorCodeV1::PayloadUnavailable));
        }
        const ProcessContributionRegistrySlotStateV1& slot = (*generation)->slots[slotIndex_];
        if (slot.contributionKind != contributionKind) {
            return std::unexpected(
                lookupError(ProcessContributionLookupErrorCodeV1::ContractAbsent));
        }
        if (slot.cardinality != cardinality) {
            return std::unexpected(
                lookupError(ProcessContributionLookupErrorCodeV1::ContractCardinalityMismatch));
        }
        if (slot.typeKey != typeKey) {
            return std::unexpected(
                lookupError(ProcessContributionLookupErrorCodeV1::ContractTypeMismatch));
        }
        if (!slot.committed || slot.payload == nullptr) {
            return std::unexpected(
                lookupError(ProcessContributionLookupErrorCodeV1::PayloadUnavailable));
        }
        return slot.payload;
    }

} // namespace asharia::host_runtime
