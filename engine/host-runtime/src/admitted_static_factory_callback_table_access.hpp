#pragma once

#include <optional>
#include <span>

#include "asharia/host_runtime/admitted_static_factory_recording.hpp"

namespace asharia::host_runtime {

    class AdmittedStaticFactoryCallbackTableAccessV1 final {
    public:
        // The returned view is borrowed for immediate synchronous lifecycle use.
        // It must not outlive the admitted owner or be cached across thread/epoch changes.
        [[nodiscard]] static std::optional<std::span<const StaticFactoryCallbacksV1>>
        callbacks(const AdmittedStaticFactoryCallbackTableV1& admittedTable) noexcept {
            return admittedTable.callbackDescriptorsForHostRuntime();
        }
    };

} // namespace asharia::host_runtime
