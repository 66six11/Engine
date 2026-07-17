#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "asharia/host_runtime/admitted_static_factory_recording.hpp"

#include "static_factory_callback_table_state.hpp"

namespace asharia::host_runtime {

    struct ControlThreadEpochAnchorV1;
    struct CurrentProcessEpochAnchorV1;
    struct ProcessScopeBlueprintProjectionStateV1;

    enum class AdmittedFactoryExecutionAccessErrorV2 : std::uint8_t {
        MovedFrom,
        WrongControlThread,
        ProcessEpochStale,
        TableInvalid,
    };

    struct AdmittedStaticFactoryExecutionViewV2 final {
        std::span<const StaticFactoryCallbacksV1> callbacks;
        std::span<const StaticContributionRuntimeBindingV1> contributionRuntimeBindings;
        const StaticFactoryRegistrationSnapshotV2* snapshot{};
        const ProcessScopeBlueprintProjectionStateV1* processScope{};
        std::shared_ptr<const CurrentProcessEpochAnchorV1> processEpoch;
        std::shared_ptr<const ControlThreadEpochAnchorV1> controlThreadEpoch;
        std::string_view engineGenerationId;
        std::string_view blueprintIntegrity;
    };

    class AdmittedStaticFactoryCallbackTableAccessV2 final {
    public:
        // The returned view is borrowed for immediate synchronous lifecycle use.
        // It must not outlive the admitted owner or be cached across thread/epoch changes.
        [[nodiscard]] static std::expected<AdmittedStaticFactoryExecutionViewV2,
                                           AdmittedFactoryExecutionAccessErrorV2>
        executionView(const AdmittedStaticFactoryCallbackTableV2& admittedTable) noexcept;

        [[nodiscard]] static std::optional<std::span<const StaticFactoryCallbacksV1>>
        callbacks(const AdmittedStaticFactoryCallbackTableV2& admittedTable) noexcept {
            const auto view = executionView(admittedTable);
            if (!view) {
                return std::nullopt;
            }
            return view->callbacks;
        }
    };

} // namespace asharia::host_runtime
