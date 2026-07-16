#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "asharia/host_runtime/activation_eligibility.hpp"
#include "asharia/host_runtime/admitted_static_factory_recording.hpp"

#include "static_factory_callback_table_private_access.hpp"

namespace asharia::host_runtime {

    struct ExactHostIdentityStateV1 final {
        std::string engineGenerationId;
        std::string hostKind;
        std::string targetPlatform;

        [[nodiscard]] friend bool operator==(const ExactHostIdentityStateV1&,
                                             const ExactHostIdentityStateV1&) = default;
    };

    struct HostArtifactIdentityStateV1 final {
        std::uint64_t size{};
        std::string sha256;

        [[nodiscard]] friend bool operator==(const HostArtifactIdentityStateV1&,
                                             const HostArtifactIdentityStateV1&) = default;
    };

    struct GeneratedHostInputIdentityStateV1 final {
        std::string generationId;
        std::string manifestSha256;

        [[nodiscard]] friend bool operator==(const GeneratedHostInputIdentityStateV1&,
                                             const GeneratedHostInputIdentityStateV1&) = default;
    };

    struct HostGenerationTupleStateV1 final {
        std::uint32_t templateRendererRevision{};
        std::uint32_t compositionRendererRevision{};
        std::string providerApi;

        [[nodiscard]] friend bool operator==(const HostGenerationTupleStateV1&,
                                             const HostGenerationTupleStateV1&) = default;
    };

    struct ReadySessionHandoffStateV1 final {
        ExactHostIdentityStateV1 host;
        std::string sessionFingerprint;
    };

    struct VerifiedHostActivationBlueprintHandoffStateV1 final {
        ExactHostIdentityStateV1 host;
        std::string effectiveSessionIntegrity;
        std::string blueprintIntegrity;
    };

    struct DeepVerifiedHostBindingHandoffStateV1 final {
        ExactHostIdentityStateV1 host;
        std::string bindingGenerationId;
        GeneratedHostInputIdentityStateV1 staticComposition;
        GeneratedHostInputIdentityStateV1 hostTemplate;
        HostGenerationTupleStateV1 generationTuple;
        std::string blueprintIntegrity;
        HostArtifactIdentityStateV1 artifact;
        StaticFactoryRegistrationSnapshotV1 expectedSnapshot;
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

    struct VerifiedCurrentProcessLaunchHandoffStateV1 final {
        ExactHostIdentityStateV1 host;
        std::string sessionFingerprint;
        std::string bindingGenerationId;
        GeneratedHostInputIdentityStateV1 staticComposition;
        GeneratedHostInputIdentityStateV1 hostTemplate;
        HostGenerationTupleStateV1 generationTuple;
        std::string blueprintIntegrity;
        HostArtifactIdentityStateV1 artifact;
        std::shared_ptr<const CurrentProcessEpochAnchorV1> processEpoch;
        std::shared_ptr<const ControlThreadEpochAnchorV1> controlThreadEpoch;
        StaticFactoryRegistrationCapacityFunctionV1 registrationCapacity{};
        StaticFactoryRecordingFunctionV1 recordProviders{};
    };

    struct ActivationEligibilityLineageStateV1 final {
        ExactHostIdentityStateV1 host;
        std::string sessionFingerprint;
        std::string bindingGenerationId;
        GeneratedHostInputIdentityStateV1 staticComposition;
        GeneratedHostInputIdentityStateV1 hostTemplate;
        HostGenerationTupleStateV1 generationTuple;
        std::string blueprintIntegrity;
        HostArtifactIdentityStateV1 artifact;
        StaticFactoryRegistrationSnapshotV1 expectedSnapshot;
        std::shared_ptr<const CurrentProcessEpochAnchorV1> processEpoch;
        std::shared_ptr<const ControlThreadEpochAnchorV1> controlThreadEpoch;
        StaticFactoryRegistrationCapacityFunctionV1 registrationCapacity{};
        StaticFactoryRecordingFunctionV1 recordProviders{};
    };

    enum class PendingFactoryTableOriginV1 : std::uint8_t {
        EvidenceOnly,
        AdmittedRegistration,
    };

    struct PendingActivationFactoryTableStateV1 final {
        PendingActivationFactoryTableStateV1(
            std::unique_ptr<ActivationEligibilityLineageStateV1> lineageValue,
            StaticFactoryCallbackTableV1 tableValue,
            PendingFactoryTableOriginV1 originValue) noexcept
            : lineage(std::move(lineageValue)), table(std::move(tableValue)),
              origin(originValue), expectedTableAddress(std::addressof(table)),
              expectedTableInstance(
                  StaticFactoryCallbackTablePrivateAccessV1::instanceAnchor(table)) {}

        std::unique_ptr<ActivationEligibilityLineageStateV1> lineage;
        StaticFactoryCallbackTableV1 table;
        PendingFactoryTableOriginV1 origin{PendingFactoryTableOriginV1::EvidenceOnly};
        const StaticFactoryCallbackTableV1* expectedTableAddress{};
        std::shared_ptr<const StaticFactoryCallbackTableStorageV1>
            expectedTableInstance;
    };

    struct AdmittedStaticFactoryCallbackTableStateV1 final {
        AdmittedStaticFactoryCallbackTableStateV1(
            std::unique_ptr<PendingActivationFactoryTableStateV1> pendingValue) noexcept
            : pending(std::move(pendingValue)) {}

        std::unique_ptr<PendingActivationFactoryTableStateV1> pending;
    };

    class ActivationEligibilityStateAccessV1 final {
    public:
        [[nodiscard]] static ReadySessionHandoffV1
        makeReadySession(ReadySessionHandoffStateV1 state) {
            return ReadySessionHandoffV1{
                std::make_unique<ReadySessionHandoffStateV1>(std::move(state))};
        }

        [[nodiscard]] static VerifiedHostActivationBlueprintHandoffV1
        makeBlueprint(VerifiedHostActivationBlueprintHandoffStateV1 state) {
            return VerifiedHostActivationBlueprintHandoffV1{
                std::make_unique<VerifiedHostActivationBlueprintHandoffStateV1>(
                    std::move(state))};
        }

        [[nodiscard]] static DeepVerifiedHostBindingHandoffV1
        makeBinding(DeepVerifiedHostBindingHandoffStateV1 state) {
            return DeepVerifiedHostBindingHandoffV1{
                std::make_unique<DeepVerifiedHostBindingHandoffStateV1>(std::move(state))};
        }

        [[nodiscard]] static VerifiedCurrentProcessLaunchHandoffV1
        makeLaunchHandoff(VerifiedCurrentProcessLaunchHandoffStateV1 state) {
            return VerifiedCurrentProcessLaunchHandoffV1{
                std::make_unique<VerifiedCurrentProcessLaunchHandoffStateV1>(
                    std::move(state))};
        }

        [[nodiscard]] static std::unique_ptr<ReadySessionHandoffStateV1>
        take(ReadySessionHandoffV1&& handoff) noexcept {
            return std::move(handoff.state_);
        }

        [[nodiscard]] static std::unique_ptr<VerifiedHostActivationBlueprintHandoffStateV1>
        take(VerifiedHostActivationBlueprintHandoffV1&& handoff) noexcept {
            return std::move(handoff.state_);
        }

        [[nodiscard]] static std::unique_ptr<DeepVerifiedHostBindingHandoffStateV1>
        take(DeepVerifiedHostBindingHandoffV1&& handoff) noexcept {
            return std::move(handoff.state_);
        }

        [[nodiscard]] static std::unique_ptr<VerifiedCurrentProcessLaunchHandoffStateV1>
        take(VerifiedCurrentProcessLaunchHandoffV1&& handoff) noexcept {
            return std::move(handoff.state_);
        }

        [[nodiscard]] static PreRegistrationAdmissionV1 makePreRegistrationAdmission(
            std::unique_ptr<ActivationEligibilityLineageStateV1> state) noexcept {
            return PreRegistrationAdmissionV1{std::move(state)};
        }

        [[nodiscard]] static std::unique_ptr<ActivationEligibilityLineageStateV1>
        take(PreRegistrationAdmissionV1&& admission) noexcept {
            return std::move(admission.state_);
        }

        [[nodiscard]] static PendingActivationFactoryTableV1 makePendingTable(
            std::unique_ptr<PendingActivationFactoryTableStateV1> state) noexcept {
            return PendingActivationFactoryTableV1{std::move(state)};
        }

        [[nodiscard]] static std::unique_ptr<PendingActivationFactoryTableStateV1>
        take(PendingActivationFactoryTableV1&& pendingTable) noexcept {
            return std::move(pendingTable.state_);
        }

        [[nodiscard]] static ActivationAdmissionV1 makeActivationAdmission() noexcept {
            return ActivationAdmissionV1{true};
        }

        [[nodiscard]] static AdmittedStaticFactoryCallbackTableV1 makeAdmittedTable(
            std::unique_ptr<AdmittedStaticFactoryCallbackTableStateV1> state,
            ActivationAdmissionV1 admission) noexcept {
            return AdmittedStaticFactoryCallbackTableV1{std::move(state),
                                                        std::move(admission)};
        }

    };

} // namespace asharia::host_runtime
