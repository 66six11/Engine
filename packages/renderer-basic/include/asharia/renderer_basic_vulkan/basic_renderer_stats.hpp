#pragma once

#include <cstdint>

namespace asharia {

    struct BasicPipelineCacheStats {
        std::uint64_t created{};
        std::uint64_t reused{};
    };

    struct BasicOffscreenViewportStats {
        std::uint64_t renderTargetsCreated{};
        std::uint64_t renderTargetsReused{};
        std::uint64_t renderTargetsDeferredForDeletion{};
    };

    struct BasicComputeDispatchStats {
        std::uint64_t bufferFillsRecorded{};
        std::uint64_t dispatchesRecorded{};
    };

} // namespace asharia
