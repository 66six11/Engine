#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

#include "asharia/host_runtime/admitted_static_factory_recording.hpp"

namespace asharia::host_runtime {

    struct ProcessScopeBlueprintProjectionStateV1;

    enum class AdmittedFactoryExecutionAccessErrorV1 : std::uint8_t {
        MovedFrom,
        WrongControlThread,
        ProcessEpochStale,
        TableInvalid,
    };

    struct AdmittedStaticFactoryExecutionViewV1 final {
        std::span<const StaticFactoryCallbacksV1> callbacks;
        const StaticFactoryRegistrationSnapshotV1* snapshot{};
        const ProcessScopeBlueprintProjectionStateV1* processScope{};
        std::string_view engineGenerationId;
        std::string_view blueprintIntegrity;
    };

    class AdmittedStaticFactoryCallbackTableAccessV1 final {
    public:
        // The returned view is borrowed for immediate synchronous lifecycle use.
        // It must not outlive the admitted owner or be cached across thread/epoch changes.
        [[nodiscard]] static std::expected<AdmittedStaticFactoryExecutionViewV1,
                                           AdmittedFactoryExecutionAccessErrorV1>
        executionView(
            const AdmittedStaticFactoryCallbackTableV1& admittedTable) noexcept;

        [[nodiscard]] static std::optional<std::span<const StaticFactoryCallbacksV1>>
        callbacks(const AdmittedStaticFactoryCallbackTableV1& admittedTable) noexcept {
            const auto view = executionView(admittedTable);
            if (!view) {
                return std::nullopt;
            }
            return view->callbacks;
        }
    };

} // namespace asharia::host_runtime
