#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>

namespace asharia::host_runtime {

    enum class StaticFactoryRegistrationErrorCode : std::uint8_t;

    enum class ActivationEligibilityStageV2 : std::uint8_t {
        PreRegistration,
        ProviderRecording,
        Activation,
    };

    enum class ActivationEligibilityFieldV2 : std::uint8_t {
        None,
        CurrentImageDescriptor,
        EffectiveSessionIntegrity,
        HostIdentity,
        BlueprintIntegrity,
        StaticComposition,
        GenerationTuple,
        ProcessProjection,
        ControlThread,
        CurrentProcess,
        Admission,
        RecordingFunction,
        Registration,
        TableOrigin,
        TableInstance,
        TableSnapshot,
    };

    enum class ActivationEligibilityErrorCodeV2 : std::uint8_t {
        DescriptorMovedFrom,
        CurrentImageDescriptorInvalid,
        UnsupportedGenerationTuple,
        ProcessProjectionInvalid,
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

    struct ActivationEligibilityErrorV2 final {
        ActivationEligibilityStageV2 stage{ActivationEligibilityStageV2::PreRegistration};
        ActivationEligibilityErrorCodeV2 code{
            ActivationEligibilityErrorCodeV2::DescriptorMovedFrom};
        ActivationEligibilityFieldV2 field{ActivationEligibilityFieldV2::None};
        std::optional<StaticFactoryRegistrationErrorCode> registrationCode;

        [[nodiscard]] friend bool operator==(const ActivationEligibilityErrorV2&,
                                             const ActivationEligibilityErrorV2&) = default;
    };

    template <typename T>
    using ActivationEligibilityResultV2 = std::expected<T, ActivationEligibilityErrorV2>;

    struct ActivationEligibilityLineageStateV2;
    class ActivationEligibilityStateAccessV2;

    class CurrentImageActivationDescriptorV2 final {
    public:
        ~CurrentImageActivationDescriptorV2();

        CurrentImageActivationDescriptorV2(CurrentImageActivationDescriptorV2&&) noexcept;
        CurrentImageActivationDescriptorV2&
        operator=(CurrentImageActivationDescriptorV2&&) = delete;
        CurrentImageActivationDescriptorV2(const CurrentImageActivationDescriptorV2&) = delete;
        CurrentImageActivationDescriptorV2&
        operator=(const CurrentImageActivationDescriptorV2&) = delete;

    private:
        explicit CurrentImageActivationDescriptorV2(
            std::unique_ptr<ActivationEligibilityLineageStateV2> state) noexcept;

        std::unique_ptr<ActivationEligibilityLineageStateV2> state_;

        friend class ActivationEligibilityStateAccessV2;
    };

    class PreRegistrationAdmissionV2 final {
    public:
        ~PreRegistrationAdmissionV2();

        PreRegistrationAdmissionV2(PreRegistrationAdmissionV2&&) noexcept;
        PreRegistrationAdmissionV2& operator=(PreRegistrationAdmissionV2&&) = delete;
        PreRegistrationAdmissionV2(const PreRegistrationAdmissionV2&) = delete;
        PreRegistrationAdmissionV2& operator=(const PreRegistrationAdmissionV2&) = delete;

    private:
        explicit PreRegistrationAdmissionV2(
            std::unique_ptr<ActivationEligibilityLineageStateV2> state) noexcept;

        std::unique_ptr<ActivationEligibilityLineageStateV2> state_;

        friend class ActivationEligibilityStateAccessV2;
    };

    [[nodiscard]] ActivationEligibilityResultV2<PreRegistrationAdmissionV2>
    admitCurrentImagePreRegistration(CurrentImageActivationDescriptorV2 descriptor) noexcept;

} // namespace asharia::host_runtime
