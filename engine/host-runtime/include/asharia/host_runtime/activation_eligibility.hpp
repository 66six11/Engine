#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>

namespace asharia::host_runtime {

    enum class StaticFactoryRegistrationErrorCode : std::uint8_t;

    enum class ActivationEligibilityStageV1 : std::uint8_t {
        PreRegistration,
        ProviderRecording,
        Activation,
    };

    enum class ActivationEligibilityFieldV1 : std::uint8_t {
        None,
        ReadySession,
        Blueprint,
        Binding,
        LaunchHandoff,
        EffectiveSessionIntegrity,
        HostIdentity,
        BlueprintIntegrity,
        StaticComposition,
        HostTemplate,
        GenerationTuple,
        BindingGeneration,
        ArtifactIdentity,
        ExpectedSnapshot,
        ControlThread,
        CurrentProcess,
        Admission,
        RecordingFunction,
        Registration,
        TableOrigin,
        TableInstance,
        TableSnapshot,
    };

    enum class ActivationEligibilityErrorCodeV1 : std::uint8_t {
        HandoffMovedFrom,
        ReadySessionInvalid,
        BlueprintInvalid,
        BindingInvalid,
        LaunchHandoffInvalid,
        EffectiveSessionMismatch,
        HostIdentityMismatch,
        BlueprintMismatch,
        StaticCompositionMismatch,
        HostTemplateMismatch,
        UnsupportedGenerationTuple,
        BindingGenerationMismatch,
        ArtifactMismatch,
        ExpectedSnapshotInvalid,
        WrongControlThread,
        ProcessEpochStale,
        ProcessEpochConsumed,
        AllocationFailed,
        AdmissionMovedFrom,
        RecordingFunctionMissing,
        RegistrationFailed,
        PendingTableMovedFrom,
        TableOriginInvalid,
        TableInstanceMismatch,
        TableSnapshotMismatch,
    };

    struct ActivationEligibilityErrorV1 final {
        ActivationEligibilityStageV1 stage{ActivationEligibilityStageV1::PreRegistration};
        ActivationEligibilityErrorCodeV1 code{
            ActivationEligibilityErrorCodeV1::HandoffMovedFrom};
        ActivationEligibilityFieldV1 field{ActivationEligibilityFieldV1::None};
        std::optional<StaticFactoryRegistrationErrorCode> registrationCode;

        [[nodiscard]] friend bool operator==(const ActivationEligibilityErrorV1&,
                                             const ActivationEligibilityErrorV1&) = default;
    };

    template <typename T>
    using ActivationEligibilityResultV1 = std::expected<T, ActivationEligibilityErrorV1>;

    struct ReadySessionHandoffStateV1;
    struct VerifiedHostActivationBlueprintHandoffStateV1;
    struct DeepVerifiedHostBindingHandoffStateV1;
    struct VerifiedCurrentProcessLaunchHandoffStateV1;
    struct ActivationEligibilityLineageStateV1;
    class ActivationEligibilityStateAccessV1;

    class ReadySessionHandoffV1 final {
    public:
        ~ReadySessionHandoffV1();

        ReadySessionHandoffV1(ReadySessionHandoffV1&&) noexcept;
        ReadySessionHandoffV1& operator=(ReadySessionHandoffV1&&) = delete;
        ReadySessionHandoffV1(const ReadySessionHandoffV1&) = delete;
        ReadySessionHandoffV1& operator=(const ReadySessionHandoffV1&) = delete;

    private:
        explicit ReadySessionHandoffV1(
            std::unique_ptr<ReadySessionHandoffStateV1> state) noexcept;

        std::unique_ptr<ReadySessionHandoffStateV1> state_;

        friend class ActivationEligibilityStateAccessV1;
    };

    class VerifiedHostActivationBlueprintHandoffV1 final {
    public:
        ~VerifiedHostActivationBlueprintHandoffV1();

        VerifiedHostActivationBlueprintHandoffV1(
            VerifiedHostActivationBlueprintHandoffV1&&) noexcept;
        VerifiedHostActivationBlueprintHandoffV1&
        operator=(VerifiedHostActivationBlueprintHandoffV1&&) = delete;
        VerifiedHostActivationBlueprintHandoffV1(
            const VerifiedHostActivationBlueprintHandoffV1&) = delete;
        VerifiedHostActivationBlueprintHandoffV1&
        operator=(const VerifiedHostActivationBlueprintHandoffV1&) = delete;

    private:
        explicit VerifiedHostActivationBlueprintHandoffV1(
            std::unique_ptr<VerifiedHostActivationBlueprintHandoffStateV1> state) noexcept;

        std::unique_ptr<VerifiedHostActivationBlueprintHandoffStateV1> state_;

        friend class ActivationEligibilityStateAccessV1;
    };

    class DeepVerifiedHostBindingHandoffV1 final {
    public:
        ~DeepVerifiedHostBindingHandoffV1();

        DeepVerifiedHostBindingHandoffV1(DeepVerifiedHostBindingHandoffV1&&) noexcept;
        DeepVerifiedHostBindingHandoffV1&
        operator=(DeepVerifiedHostBindingHandoffV1&&) = delete;
        DeepVerifiedHostBindingHandoffV1(const DeepVerifiedHostBindingHandoffV1&) = delete;
        DeepVerifiedHostBindingHandoffV1&
        operator=(const DeepVerifiedHostBindingHandoffV1&) = delete;

    private:
        explicit DeepVerifiedHostBindingHandoffV1(
            std::unique_ptr<DeepVerifiedHostBindingHandoffStateV1> state) noexcept;

        std::unique_ptr<DeepVerifiedHostBindingHandoffStateV1> state_;

        friend class ActivationEligibilityStateAccessV1;
    };

    class VerifiedCurrentProcessLaunchHandoffV1 final {
    public:
        ~VerifiedCurrentProcessLaunchHandoffV1();

        VerifiedCurrentProcessLaunchHandoffV1(
            VerifiedCurrentProcessLaunchHandoffV1&&) noexcept;
        VerifiedCurrentProcessLaunchHandoffV1&
        operator=(VerifiedCurrentProcessLaunchHandoffV1&&) = delete;
        VerifiedCurrentProcessLaunchHandoffV1(
            const VerifiedCurrentProcessLaunchHandoffV1&) = delete;
        VerifiedCurrentProcessLaunchHandoffV1&
        operator=(const VerifiedCurrentProcessLaunchHandoffV1&) = delete;

    private:
        explicit VerifiedCurrentProcessLaunchHandoffV1(
            std::unique_ptr<VerifiedCurrentProcessLaunchHandoffStateV1> state) noexcept;

        std::unique_ptr<VerifiedCurrentProcessLaunchHandoffStateV1> state_;

        friend class ActivationEligibilityStateAccessV1;
    };

    class PreRegistrationAdmissionV1 final {
    public:
        ~PreRegistrationAdmissionV1();

        PreRegistrationAdmissionV1(PreRegistrationAdmissionV1&&) noexcept;
        PreRegistrationAdmissionV1& operator=(PreRegistrationAdmissionV1&&) = delete;
        PreRegistrationAdmissionV1(const PreRegistrationAdmissionV1&) = delete;
        PreRegistrationAdmissionV1& operator=(const PreRegistrationAdmissionV1&) = delete;

    private:
        explicit PreRegistrationAdmissionV1(
            std::unique_ptr<ActivationEligibilityLineageStateV1> state) noexcept;

        std::unique_ptr<ActivationEligibilityLineageStateV1> state_;

        friend class ActivationEligibilityStateAccessV1;
    };

    [[nodiscard]] ActivationEligibilityResultV1<PreRegistrationAdmissionV1>
    admitPreRegistration(ReadySessionHandoffV1 readySession,
                         VerifiedHostActivationBlueprintHandoffV1 blueprint,
                         DeepVerifiedHostBindingHandoffV1 binding,
                         VerifiedCurrentProcessLaunchHandoffV1 launchHandoff) noexcept;

} // namespace asharia::host_runtime
