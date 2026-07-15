#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "asharia/host_runtime/static_factory_callback_table.hpp"
#include "asharia/host_runtime/static_factory_provider.hpp"

namespace asharia::host_runtime {

    struct StaticFactoryRegistrationCapacityV1 final {
        std::size_t providerCount{};
        std::size_t factoryCount{};
        std::size_t textBytes{};
        std::size_t diagnosticFactoryIdBytes{};

        [[nodiscard]] friend constexpr bool
        operator==(const StaticFactoryRegistrationCapacityV1&,
                   const StaticFactoryRegistrationCapacityV1&) noexcept = default;
    };

    struct StaticCompositionRegistrationContextV1 final {
        std::string_view generationId;
        std::string_view hostActivationBlueprintSha256;
        StaticFactoryRegistrationCapacityV1 capacity;
    };

    struct StaticFactoryProviderContextV1 final {
        std::string_view packageId;
        std::string_view packageVersion;
        std::string_view moduleId;
        std::string_view entryPoint;
        std::span<const std::string_view> expectedFactoryIds;
    };

    enum class StaticFactoryRegistrationErrorCode {
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
        TextCapacityExceeded,
        DiagnosticFactoryIdCapacityExceeded,
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
        std::size_t observedFactoryIdBytes{};
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

        void beginComposition(StaticCompositionRegistrationContextV1 context) noexcept;
        void invokeProvider(StaticFactoryProviderContextV1 context,
                            StaticFactoryProviderV2 provider) noexcept;
        void endComposition() noexcept;

        [[nodiscard]] StaticFactoryRegistrationResult<StaticFactoryCallbackTableV1>
        finish() && noexcept;

    private:
        explicit StaticFactoryRegistrationRecorder(
            std::unique_ptr<StaticFactoryRegistrationState> state) noexcept;

        std::unique_ptr<StaticFactoryRegistrationState> state_;

        friend StaticFactoryRegistrationResult<StaticFactoryRegistrationRecorder>
        createStaticFactoryRegistrationRecorder(
            StaticFactoryRegistrationCapacityV1 capacity) noexcept;
    };

    [[nodiscard]] StaticFactoryRegistrationResult<StaticFactoryRegistrationRecorder>
    createStaticFactoryRegistrationRecorder(StaticFactoryRegistrationCapacityV1 capacity) noexcept;

} // namespace asharia::host_runtime
