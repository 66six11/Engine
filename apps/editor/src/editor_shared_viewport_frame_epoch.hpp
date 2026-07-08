#pragma once

#include <cstdint>
#include <memory>

namespace asharia::editor {

    struct EditorSharedViewportFrameEpochStats {
        std::uint64_t submitted{};
        std::uint64_t completed{};
        std::uint64_t pending{};
    };

    struct EditorSharedViewportFrameEpochState;

    class EditorSharedViewportFrameEpochTracker;

    class EditorSharedViewportFrameEpochLease final {
    public:
        EditorSharedViewportFrameEpochLease() = default;
        EditorSharedViewportFrameEpochLease(const EditorSharedViewportFrameEpochLease&) = delete;
        EditorSharedViewportFrameEpochLease& operator=(const EditorSharedViewportFrameEpochLease&) =
            delete;
        EditorSharedViewportFrameEpochLease(EditorSharedViewportFrameEpochLease&& other) noexcept;
        EditorSharedViewportFrameEpochLease& operator=(EditorSharedViewportFrameEpochLease&& other)
            noexcept;
        ~EditorSharedViewportFrameEpochLease();

        [[nodiscard]] bool hasEpoch() const noexcept;
        [[nodiscard]] std::uint64_t epoch() const noexcept;
        void complete() noexcept;
        void abandon() noexcept;

    private:
        friend class EditorSharedViewportFrameEpochTracker;

        EditorSharedViewportFrameEpochLease(
            std::shared_ptr<EditorSharedViewportFrameEpochState> state,
            std::uint64_t epoch) noexcept;

        std::shared_ptr<EditorSharedViewportFrameEpochState> state_;
        std::uint64_t epoch_{};
        bool completed_{true};
    };

    class EditorSharedViewportFrameEpochTracker final {
    public:
        EditorSharedViewportFrameEpochTracker();
        EditorSharedViewportFrameEpochTracker(const EditorSharedViewportFrameEpochTracker&) =
            delete;
        EditorSharedViewportFrameEpochTracker& operator=(
            const EditorSharedViewportFrameEpochTracker&) = delete;
        EditorSharedViewportFrameEpochTracker(EditorSharedViewportFrameEpochTracker&&) noexcept =
            default;
        EditorSharedViewportFrameEpochTracker& operator=(
            EditorSharedViewportFrameEpochTracker&&) noexcept = default;
        ~EditorSharedViewportFrameEpochTracker() = default;

        [[nodiscard]] EditorSharedViewportFrameEpochLease submit();
        [[nodiscard]] EditorSharedViewportFrameEpochStats stats() const;

    private:
        std::shared_ptr<EditorSharedViewportFrameEpochState> state_;
    };

} // namespace asharia::editor
