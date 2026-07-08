#include "editor_shared_viewport_frame_epoch.hpp"

#include <algorithm>
#include <mutex>
#include <utility>

namespace asharia::editor {

    struct EditorSharedViewportFrameEpochState {
        mutable std::mutex mutex;
        std::uint64_t submitted{};
        std::uint64_t completed{};
    };

    EditorSharedViewportFrameEpochLease::EditorSharedViewportFrameEpochLease(
        std::shared_ptr<EditorSharedViewportFrameEpochState> state, std::uint64_t epoch) noexcept
        : state_{std::move(state)}, epoch_{epoch}, completed_{false} {}

    EditorSharedViewportFrameEpochLease::EditorSharedViewportFrameEpochLease(
        EditorSharedViewportFrameEpochLease&& other) noexcept {
        *this = std::move(other);
    }

    EditorSharedViewportFrameEpochLease& EditorSharedViewportFrameEpochLease::operator=(
        EditorSharedViewportFrameEpochLease&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        complete();
        state_ = std::move(other.state_);
        epoch_ = std::exchange(other.epoch_, 0U);
        completed_ = std::exchange(other.completed_, true);
        return *this;
    }

    EditorSharedViewportFrameEpochLease::~EditorSharedViewportFrameEpochLease() {
        complete();
    }

    bool EditorSharedViewportFrameEpochLease::hasEpoch() const noexcept {
        return static_cast<bool>(state_) && epoch_ != 0U;
    }

    std::uint64_t EditorSharedViewportFrameEpochLease::epoch() const noexcept {
        return epoch_;
    }

    void EditorSharedViewportFrameEpochLease::complete() noexcept {
        if (completed_ || !state_) {
            return;
        }

        {
            std::lock_guard lock{state_->mutex};
            state_->completed = std::max(state_->completed, epoch_);
        }

        completed_ = true;
        state_.reset();
        epoch_ = 0U;
    }

    void EditorSharedViewportFrameEpochLease::abandon() noexcept {
        completed_ = true;
        state_.reset();
        epoch_ = 0U;
    }

    EditorSharedViewportFrameEpochTracker::EditorSharedViewportFrameEpochTracker()
        : state_{std::make_shared<EditorSharedViewportFrameEpochState>()} {}

    EditorSharedViewportFrameEpochLease EditorSharedViewportFrameEpochTracker::submit() {
        if (!state_) {
            state_ = std::make_shared<EditorSharedViewportFrameEpochState>();
        }

        std::lock_guard lock{state_->mutex};
        const std::uint64_t epoch = ++state_->submitted;
        return EditorSharedViewportFrameEpochLease{state_, epoch};
    }

    EditorSharedViewportFrameEpochStats
    EditorSharedViewportFrameEpochTracker::stats() const {
        if (!state_) {
            return {};
        }

        std::lock_guard lock{state_->mutex};
        const std::uint64_t completed = std::min(state_->completed, state_->submitted);
        return EditorSharedViewportFrameEpochStats{
            .submitted = state_->submitted,
            .completed = completed,
            .pending = state_->submitted - completed,
        };
    }

} // namespace asharia::editor
