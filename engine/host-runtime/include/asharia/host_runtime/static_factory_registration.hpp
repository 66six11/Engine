#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "asharia/host_runtime/static_factory_callback_table.hpp"
#include "asharia/host_runtime/static_factory_provider.hpp"

namespace asharia::host_runtime {

    struct StaticFactoryRegistrationCapacityV2 final {
        std::size_t providerCount{};
        std::size_t factoryCount{};
        std::size_t contributionCount{};
        std::size_t textBytes{};
        std::size_t diagnosticFactoryIdBytes{};
        std::size_t diagnosticContributionIdBytes{};

        [[nodiscard]] friend constexpr bool
        operator==(const StaticFactoryRegistrationCapacityV2&,
                   const StaticFactoryRegistrationCapacityV2&) noexcept = default;
    };

    struct StaticCompositionRegistrationContextV2 final {
        std::string_view generationId;
        std::string_view hostActivationBlueprintSha256;
        StaticFactoryRegistrationCapacityV2 capacity;
    };

    struct StaticContributionExpectationV1 final {
        std::string_view contributionId;
        std::string_view contributionKind;
    };

    struct StaticFactoryExpectationV1 final {
        std::string_view factoryId;
        std::span<const StaticContributionExpectationV1> contributions;
    };

    struct StaticFactoryProviderContextV2 final {
        std::string_view packageId;
        std::string_view packageVersion;
        std::string_view moduleId;
        std::string_view entryPoint;
        std::span<const StaticFactoryExpectationV1> expectedFactories;
    };

    enum class StaticFactoryRegistrationErrorCode : std::uint8_t {
        InvalidCapacity,
        AllocationFailed,
        InvalidCompositionContext,
        CompositionAlreadyStarted,
        CompositionNotStarted,
        CompositionNotEnded,
        CompositionAlreadyEnded,
        ProviderContextInvalid,
        ProviderOutsideComposition,
        ProviderNested,
        ProviderMissing,
        ProviderDuplicate,
        ProviderCountMismatch,
        ExpectedFactoriesNotCanonical,
        ExpectedContributionsNotCanonical,
        FactoryOutsideProvider,
        FactoryUnknown,
        FactoryDuplicate,
        FactoryMissing,
        FactoryCountMismatch,
        FactoryCreateCallbackMissing,
        FactoryActivateCallbackMissing,
        FactoryQuiesceCallbackMissing,
        FactoryDeactivateCallbackMissing,
        FactoryDestroyCallbackMissing,
        ContributionBindingInvalid,
        ContributionBindingDuplicate,
        ContributionMissing,
        ContributionKindMismatch,
        ContributionContractCardinalityConflict,
        ContributionContractTypeConflict,
        ContributionCountMismatch,
        TextCapacityExceeded,
        DiagnosticFactoryIdCapacityExceeded,
        DiagnosticContributionIdCapacityExceeded,
        RecorderMovedFrom,
        RecorderAlreadyFinished,
    };

    struct StaticFactoryRegistrationError final {
        StaticFactoryRegistrationErrorCode code{
            StaticFactoryRegistrationErrorCode::InvalidCapacity};
        std::string packageId;
        std::string packageVersion;
        std::string moduleId;
        std::string entryPoint;
        std::string factoryId;
        std::string contributionId;
        std::size_t observedFactoryIdBytes{};
        std::size_t observedContributionIdBytes{};
    };

    template <typename T>
    using StaticFactoryRegistrationResult = std::expected<T, StaticFactoryRegistrationError>;

    class StaticFactoryRegistrationRecorder final {
    public:
        ~StaticFactoryRegistrationRecorder();

        StaticFactoryRegistrationRecorder(StaticFactoryRegistrationRecorder&& other) noexcept;
        StaticFactoryRegistrationRecorder&
        operator=(StaticFactoryRegistrationRecorder&& other) noexcept = delete;

        StaticFactoryRegistrationRecorder(const StaticFactoryRegistrationRecorder&) = delete;
        StaticFactoryRegistrationRecorder&
        operator=(const StaticFactoryRegistrationRecorder&) = delete;

        void beginComposition(StaticCompositionRegistrationContextV2 context) noexcept;
        void invokeProvider(StaticFactoryProviderContextV2 context,
                            StaticFactoryProviderV3 provider) noexcept;
        void endComposition() noexcept;

        [[nodiscard]] StaticFactoryRegistrationResult<StaticFactoryCallbackTableV1>
        finish() && noexcept;

    private:
        explicit StaticFactoryRegistrationRecorder(
            std::unique_ptr<StaticFactoryRegistrationState> state) noexcept;

        std::unique_ptr<StaticFactoryRegistrationState> state_;

        friend StaticFactoryRegistrationResult<StaticFactoryRegistrationRecorder>
        createStaticFactoryRegistrationRecorder(
            StaticFactoryRegistrationCapacityV2 capacity) noexcept;
    };

    [[nodiscard]] StaticFactoryRegistrationResult<StaticFactoryRegistrationRecorder>
    createStaticFactoryRegistrationRecorder(StaticFactoryRegistrationCapacityV2 capacity) noexcept;

} // namespace asharia::host_runtime
