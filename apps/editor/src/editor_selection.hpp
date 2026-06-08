#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/scene/entity_id.hpp"

#include "editor_id.hpp"

namespace asharia::editor {

    class EditorEventQueue;

    inline constexpr std::string_view kEditorSelectionOwnerId = "editor.selection";
    inline constexpr std::string_view kEditorInspectedWorldSceneId = "inspected-world";

    enum class EditorSelectionTargetState {
        Resolved,
        Missing,
        Stale,
    };

    enum class EditorSelectionChangeReason {
        Replace,
        Refresh,
        Clear,
    };

    struct EditorSceneEntitySelectionId {
        std::string sceneId;
        asharia::EntityId entity;

        [[nodiscard]] friend bool operator==(const EditorSceneEntitySelectionId&,
                                             const EditorSceneEntitySelectionId&) = default;
    };

    struct EditorSelectionItem {
        EditorSceneEntitySelectionId target;
        EditorSelectionTargetState state{EditorSelectionTargetState::Resolved};
        bool primary{};
        std::string displayName;

        [[nodiscard]] friend bool operator==(const EditorSelectionItem&,
                                             const EditorSelectionItem&) = default;
    };

    struct EditorSelectionChangedFact {
        std::uint64_t revision{};
        EditorSelectionChangeReason reason{EditorSelectionChangeReason::Replace};
        EditorId sourceId;
        std::size_t previousCount{};
        std::size_t selectedCount{};
        bool hasMissing{};
        bool hasStale{};
        std::optional<EditorSceneEntitySelectionId> primaryTarget;
    };

    struct EditorSelectionSnapshot {
        std::uint64_t revision{};
        std::vector<EditorSelectionItem> items;

        [[nodiscard]] bool empty() const noexcept;
        [[nodiscard]] std::size_t size() const noexcept;
        [[nodiscard]] const EditorSelectionItem* primary() const noexcept;
        [[nodiscard]] bool hasMissing() const noexcept;
        [[nodiscard]] bool hasStale() const noexcept;
    };

    [[nodiscard]] bool isValidEditorSceneEntitySelectionId(
        const EditorSceneEntitySelectionId& target) noexcept;
    [[nodiscard]] std::string editorSelectionTargetLabel(
        const EditorSceneEntitySelectionId& target);
    [[nodiscard]] std::string_view editorSelectionTargetStateName(
        EditorSelectionTargetState state) noexcept;
    [[nodiscard]] std::string_view editorSelectionChangeReasonName(
        EditorSelectionChangeReason reason) noexcept;

    class EditorSelectionSet {
    public:
        explicit EditorSelectionSet(EditorEventQueue& eventQueue);
        EditorSelectionSet(const EditorSelectionSet&) = delete;
        EditorSelectionSet& operator=(const EditorSelectionSet&) = delete;
        EditorSelectionSet(EditorSelectionSet&&) = delete;
        EditorSelectionSet& operator=(EditorSelectionSet&&) = delete;

        [[nodiscard]] bool replace(std::vector<EditorSelectionItem> items,
                                   EditorId sourceId = {});
        [[nodiscard]] bool refresh(std::vector<EditorSelectionItem> items,
                                   EditorId sourceId = {});
        [[nodiscard]] bool clear(EditorId sourceId = {});

        [[nodiscard]] const EditorSelectionSnapshot& snapshot() const noexcept;
        [[nodiscard]] const std::optional<EditorSelectionChangedFact>&
        latestChange() const noexcept;

    private:
        [[nodiscard]] bool commit(std::vector<EditorSelectionItem> items,
                                  EditorSelectionChangeReason reason, EditorId sourceId);

        EditorEventQueue* eventQueue_{};
        EditorSelectionSnapshot snapshot_;
        std::optional<EditorSelectionChangedFact> latestChange_;
    };

} // namespace asharia::editor
