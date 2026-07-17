#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "asharia/host_runtime/activation_eligibility.hpp"
#include "asharia/host_runtime/admitted_static_factory_recording.hpp"

#include "host_activation_blueprint_projection_state.hpp"
#include "static_factory_callback_table_private_access.hpp"

namespace asharia::host_runtime {

    struct ExactHostIdentityStateV2 final {
        std::string engineGenerationId;
        std::string hostKind;
        std::string targetPlatform;

        [[nodiscard]] friend bool operator==(const ExactHostIdentityStateV2&,
                                             const ExactHostIdentityStateV2&) = default;
    };

    struct HostGenerationTupleStateV2 final {
        std::uint32_t templateRendererRevision{};
        std::uint32_t compositionRendererRevision{};
        std::string providerApi;
        std::uint32_t registrationSnapshotSchemaVersion{};

        [[nodiscard]] friend bool operator==(const HostGenerationTupleStateV2&,
                                             const HostGenerationTupleStateV2&) = default;
    };

    struct CurrentProcessEpochAnchorV1 final {
        mutable std::atomic_bool claimed{};
    };
    struct ControlThreadEpochAnchorV1 final {};

    [[nodiscard]] std::shared_ptr<const CurrentProcessEpochAnchorV1>
    createAndBindCurrentProcessEpoch();

    [[nodiscard]] bool isCurrentProcessEpoch(
        const std::shared_ptr<const CurrentProcessEpochAnchorV1>& expected) noexcept;

    [[nodiscard]] bool tryClaimCurrentProcessEpoch(
        const std::shared_ptr<const CurrentProcessEpochAnchorV1>& expected) noexcept;

    [[nodiscard]] bool isClaimedCurrentProcessEpoch(
        const std::shared_ptr<const CurrentProcessEpochAnchorV1>& expected) noexcept;

    [[nodiscard]] std::shared_ptr<const ControlThreadEpochAnchorV1>
    createAndBindCurrentControlThreadEpoch();

    [[nodiscard]] bool isCurrentControlThread(
        const std::shared_ptr<const ControlThreadEpochAnchorV1>& expected) noexcept;

    struct ActivationEligibilityLineageStateV2 final {
        ExactHostIdentityStateV2 host;
        std::string effectiveSessionIntegrity;
        std::string staticCompositionGenerationId;
        std::string blueprintIntegrity;
        HostGenerationTupleStateV2 generationTuple;
        std::string lifecycleModel;
        ProcessScopeBlueprintProjectionStateV1 processScope;
        std::shared_ptr<const CurrentProcessEpochAnchorV1> processEpoch;
        std::shared_ptr<const ControlThreadEpochAnchorV1> controlThreadEpoch;
        StaticFactoryRegistrationCapacityFunctionV2 registrationCapacity{};
        StaticFactoryRecordingFunctionV2 recordProviders{};
    };

    enum class PendingFactoryTableOriginV2 : std::uint8_t {
        EvidenceOnly,
        AdmittedRegistration,
    };

    struct PendingActivationFactoryTableStateV2 final {
        PendingActivationFactoryTableStateV2(
            std::unique_ptr<ActivationEligibilityLineageStateV2> lineageValue,
            StaticFactoryCallbackTableV1 tableValue, PendingFactoryTableOriginV2 originValue)
            : lineage(std::move(lineageValue)), table(std::move(tableValue)), origin(originValue),
              expectedTableAddress(std::addressof(table)),
              expectedTableInstance(
                  StaticFactoryCallbackTablePrivateAccessV1::instanceAnchor(table)) {}

        std::unique_ptr<ActivationEligibilityLineageStateV2> lineage;
        StaticFactoryCallbackTableV1 table;
        PendingFactoryTableOriginV2 origin{PendingFactoryTableOriginV2::EvidenceOnly};
        const StaticFactoryCallbackTableV1* expectedTableAddress{};
        std::shared_ptr<const StaticFactoryCallbackTableStorageV1> expectedTableInstance;
    };

    struct AdmittedStaticFactoryCallbackTableStateV2 final {
        explicit AdmittedStaticFactoryCallbackTableStateV2(
            std::unique_ptr<PendingActivationFactoryTableStateV2> pendingValue) noexcept
            : pending(std::move(pendingValue)) {}

        std::unique_ptr<PendingActivationFactoryTableStateV2> pending;
    };

    class ActivationEligibilityStateAccessV2 final {
    public:
        [[nodiscard]] static CurrentImageActivationDescriptorV2
        makeDescriptor(std::unique_ptr<ActivationEligibilityLineageStateV2> state) noexcept {
            return CurrentImageActivationDescriptorV2{std::move(state)};
        }

        [[nodiscard]] static std::unique_ptr<ActivationEligibilityLineageStateV2>
        take(CurrentImageActivationDescriptorV2&& descriptor) noexcept {
            return std::move(descriptor.state_);
        }

        [[nodiscard]] static PreRegistrationAdmissionV2 makePreRegistrationAdmission(
            std::unique_ptr<ActivationEligibilityLineageStateV2> state) noexcept {
            return PreRegistrationAdmissionV2{std::move(state)};
        }

        [[nodiscard]] static std::unique_ptr<ActivationEligibilityLineageStateV2>
        take(PreRegistrationAdmissionV2&& admission) noexcept {
            return std::move(admission.state_);
        }

        [[nodiscard]] static PendingActivationFactoryTableV2
        makePendingTable(std::unique_ptr<PendingActivationFactoryTableStateV2> state) noexcept {
            return PendingActivationFactoryTableV2{std::move(state)};
        }

        [[nodiscard]] static std::unique_ptr<PendingActivationFactoryTableStateV2>
        take(PendingActivationFactoryTableV2&& pendingTable) noexcept {
            return std::move(pendingTable.state_);
        }

        [[nodiscard]] static ActivationAdmissionV2 makeActivationAdmission() noexcept {
            return ActivationAdmissionV2{true};
        }

        [[nodiscard]] static AdmittedStaticFactoryCallbackTableV2
        makeAdmittedTable(std::unique_ptr<AdmittedStaticFactoryCallbackTableStateV2> state,
                          ActivationAdmissionV2 admission) noexcept {
            return AdmittedStaticFactoryCallbackTableV2{std::move(state), std::move(admission)};
        }
    };

} // namespace asharia::host_runtime
