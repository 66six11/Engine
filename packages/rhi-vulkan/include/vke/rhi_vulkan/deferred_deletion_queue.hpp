#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace vke {

    using VulkanDeferredDeletionCallback = std::function<void()>;

    struct VulkanDeferredDeletionStats {
        std::uint64_t pending{};
        std::uint64_t enqueued{};
        std::uint64_t retired{};
        std::uint64_t flushed{};
    };

    class VulkanDeferredDeletionQueue {
    public:
        [[nodiscard]] bool enqueue(std::uint64_t retireEpoch,
                                   VulkanDeferredDeletionCallback callback);
        [[nodiscard]] std::uint64_t retireCompleted(std::uint64_t completedEpoch);
        [[nodiscard]] std::uint64_t flush();

        [[nodiscard]] VulkanDeferredDeletionStats stats() const;
        [[nodiscard]] std::size_t pendingCount() const;
        [[nodiscard]] bool empty() const;

    private:
        struct Record {
            std::uint64_t retireEpoch{};
            VulkanDeferredDeletionCallback callback;
        };

        std::vector<Record> records_;
        std::uint64_t enqueued_{};
        std::uint64_t retired_{};
        std::uint64_t flushed_{};
    };

} // namespace vke
