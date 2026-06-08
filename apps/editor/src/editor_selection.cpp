#include "editor_selection.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "asharia/scene/entity_id.hpp"

#include "editor_event.hpp"

namespace asharia::editor {

    namespace {

        [[nodiscard]] EditorId normalizedSelectionSourceId(EditorId sourceId) {
            if (sourceId.value.empty()) {
                sourceId.value = std::string{kEditorSelectionOwnerId};
            }
            return sourceId;
        }

        [[nodiscard]] bool sameSelectionTarget(const EditorSelectionItem& item,
                                               const EditorSceneEntitySelectionId& target) {
            return item.target == target;
        }

        [[nodiscard]] std::vector<EditorSelectionItem>
        normalizeSelectionItems(std::vector<EditorSelectionItem> items) {
            std::vector<EditorSelectionItem> normalized;
            normalized.reserve(items.size());
            for (EditorSelectionItem& item : items) {
                if (!isValidEditorSceneEntitySelectionId(item.target)) {
                    continue;
                }
                const bool alreadySelected = std::ranges::any_of(
                    normalized, [&](const EditorSelectionItem& selected) {
                        return sameSelectionTarget(selected, item.target);
                    });
                if (!alreadySelected) {
                    normalized.push_back(std::move(item));
                }
            }

            bool primaryAssigned = false;
            for (EditorSelectionItem& item : normalized) {
                if (item.primary && !primaryAssigned) {
                    primaryAssigned = true;
                    continue;
                }
                item.primary = false;
            }
            if (!primaryAssigned && !normalized.empty()) {
                normalized.front().primary = true;
            }
            return normalized;
        }

        [[nodiscard]] bool hasState(const std::vector<EditorSelectionItem>& items,
                                    EditorSelectionTargetState state) {
            return std::ranges::any_of(items, [state](const EditorSelectionItem& item) {
                return item.state == state;
            });
        }

        [[nodiscard]] std::optional<EditorSceneEntitySelectionId>
        primaryTargetFor(const std::vector<EditorSelectionItem>& items) {
            const auto found = std::ranges::find_if(items, [](const EditorSelectionItem& item) {
                return item.primary;
            });
            if (found == items.end()) {
                return std::nullopt;
            }
            return found->target;
        }

    } // namespace

    bool EditorSelectionSnapshot::empty() const noexcept {
        return items.empty();
    }

    std::size_t EditorSelectionSnapshot::size() const noexcept {
        return items.size();
    }

    const EditorSelectionItem* EditorSelectionSnapshot::primary() const noexcept {
        const auto found = std::ranges::find_if(items, [](const EditorSelectionItem& item) {
            return item.primary;
        });
        if (found == items.end()) {
            return nullptr;
        }
        return &*found;
    }

    bool EditorSelectionSnapshot::hasMissing() const noexcept {
        return hasState(items, EditorSelectionTargetState::Missing);
    }

    bool EditorSelectionSnapshot::hasStale() const noexcept {
        return hasState(items, EditorSelectionTargetState::Stale);
    }

    bool isValidEditorSceneEntitySelectionId(
        const EditorSceneEntitySelectionId& target) noexcept {
        return !target.sceneId.empty() && asharia::isValid(target.entity);
    }

    std::string editorSelectionTargetLabel(const EditorSceneEntitySelectionId& target) {
        return target.sceneId + ":" + std::to_string(target.entity.index) + ":" +
               std::to_string(target.entity.generation);
    }

    std::string_view
    editorSelectionTargetStateName(EditorSelectionTargetState state) noexcept {
        switch (state) {
        case EditorSelectionTargetState::Resolved:
            return "Resolved";
        case EditorSelectionTargetState::Missing:
            return "Missing";
        case EditorSelectionTargetState::Stale:
            return "Stale";
        }
        return "Unknown";
    }

    std::string_view
    editorSelectionChangeReasonName(EditorSelectionChangeReason reason) noexcept {
        switch (reason) {
        case EditorSelectionChangeReason::Replace:
            return "Replace";
        case EditorSelectionChangeReason::Refresh:
            return "Refresh";
        case EditorSelectionChangeReason::Clear:
            return "Clear";
        }
        return "Unknown";
    }

    EditorSelectionSet::EditorSelectionSet(EditorEventQueue& eventQueue)
        : eventQueue_(&eventQueue) {}

    bool EditorSelectionSet::replace(std::vector<EditorSelectionItem> items,
                                     EditorId sourceId) {
        return commit(std::move(items), EditorSelectionChangeReason::Replace,
                      std::move(sourceId));
    }

    bool EditorSelectionSet::refresh(std::vector<EditorSelectionItem> items,
                                     EditorId sourceId) {
        return commit(std::move(items), EditorSelectionChangeReason::Refresh,
                      std::move(sourceId));
    }

    bool EditorSelectionSet::clear(EditorId sourceId) {
        return commit({}, EditorSelectionChangeReason::Clear, std::move(sourceId));
    }

    const EditorSelectionSnapshot& EditorSelectionSet::snapshot() const noexcept {
        return snapshot_;
    }

    const std::optional<EditorSelectionChangedFact>&
    EditorSelectionSet::latestChange() const noexcept {
        return latestChange_;
    }

    bool EditorSelectionSet::commit(std::vector<EditorSelectionItem> items,
                                    EditorSelectionChangeReason reason, EditorId sourceId) {
        std::vector<EditorSelectionItem> normalized = normalizeSelectionItems(std::move(items));
        if (snapshot_.items == normalized) {
            return false;
        }

        const std::size_t previousCount = snapshot_.items.size();
        snapshot_.items = std::move(normalized);
        ++snapshot_.revision;

        latestChange_ = EditorSelectionChangedFact{
            .revision = snapshot_.revision,
            .reason = reason,
            .sourceId = normalizedSelectionSourceId(std::move(sourceId)),
            .previousCount = previousCount,
            .selectedCount = snapshot_.items.size(),
            .hasMissing = snapshot_.hasMissing(),
            .hasStale = snapshot_.hasStale(),
            .primaryTarget = primaryTargetFor(snapshot_.items),
        };

        if (eventQueue_ != nullptr) {
            eventQueue_->push(EditorEvent{
                .kind = EditorEventKind::SelectionChanged,
                .sourceId = EditorId{.value = std::string{kEditorSelectionOwnerId}},
            });
        }
        return true;
    }

} // namespace asharia::editor
