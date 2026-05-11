#include "asharia/rhi_vulkan/deferred_deletion_queue.hpp"

#include <utility>

namespace asharia {
    bool VulkanDeferredDeletionQueue::enqueue(std::uint64_t retireEpoch,
                                              VulkanDeferredDeletionCallback callback) {
        if (!callback) {
            return false;
        }

        records_.push_back(Record{
            .retireEpoch = retireEpoch,
            .callback = std::move(callback),
        });
        ++enqueued_;
        return true;
    }

    std::uint64_t VulkanDeferredDeletionQueue::retireCompleted(std::uint64_t completedEpoch) {
        std::vector<Record> ready;
        std::vector<Record> pending;
        ready.reserve(records_.size());
        pending.reserve(records_.size());

        for (Record& record : records_) {
            if (record.retireEpoch <= completedEpoch) {
                ready.push_back(std::move(record));
            } else {
                pending.push_back(std::move(record));
            }
        }

        records_ = std::move(pending);

        for (Record& record : ready) {
            record.callback();
        }

        const auto retired = static_cast<std::uint64_t>(ready.size());
        retired_ += retired;
        return retired;
    }

    std::uint64_t VulkanDeferredDeletionQueue::flush() {
        std::vector<Record> ready = std::move(records_);
        records_.clear();

        for (Record& record : ready) {
            record.callback();
        }

        const auto retired = static_cast<std::uint64_t>(ready.size());
        retired_ += retired;
        flushed_ += retired;
        return retired;
    }

    VulkanDeferredDeletionStats VulkanDeferredDeletionQueue::stats() const {
        return VulkanDeferredDeletionStats{
            .pending = static_cast<std::uint64_t>(records_.size()),
            .enqueued = enqueued_,
            .retired = retired_,
            .flushed = flushed_,
        };
    }

    std::size_t VulkanDeferredDeletionQueue::pendingCount() const {
        return records_.size();
    }

    bool VulkanDeferredDeletionQueue::empty() const {
        return records_.empty();
    }

} // namespace asharia
