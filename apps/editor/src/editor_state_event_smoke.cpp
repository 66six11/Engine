#include "editor_state_event_smoke.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "asharia/core/log.hpp"
#include "asharia/core/result.hpp"
#include "asharia/scene/entity_id.hpp"

#include "editor_command.hpp"
#include "editor_dirty_state.hpp"
#include "editor_event.hpp"
#include "editor_selection.hpp"
#include "editor_smoke.hpp"

namespace asharia::editor {

    namespace {

        class TestSetIntCommand final : public EditorCommand {
        public:
            TestSetIntCommand(int& target, int newValue)
                : target_(&target), newValue_(newValue), oldValue_(target) {}

            [[nodiscard]] std::string description() const override {
                return "SetInt " + std::to_string(oldValue_) + " -> " + std::to_string(newValue_);
            }

            [[nodiscard]] asharia::Result<void> execute() override {
                *target_ = newValue_;
                return {};
            }

            [[nodiscard]] asharia::Result<void> undo() override {
                *target_ = oldValue_;
                return {};
            }

        private:
            int* target_{};
            int newValue_{};
            int oldValue_{};
        };

        [[nodiscard]] EditorSceneEntitySelectionId target(std::uint32_t index) {
            return EditorSceneEntitySelectionId{
                .sceneId = std::string{kEditorInspectedWorldSceneId},
                .entity = asharia::EntityId{.index = index, .generation = 1U},
            };
        }

        [[nodiscard]] EditorSelectionItem item(EditorSceneEntitySelectionId targetId) {
            return EditorSelectionItem{
                .target = std::move(targetId),
                .state = EditorSelectionTargetState::Resolved,
                .primary = true,
                .displayName = "Camera",
            };
        }

        [[nodiscard]] bool expectEvent(const EditorDiagnosticEvent& diagnostic,
                                       std::uint64_t expectedSequence, EditorEventKind expectedKind,
                                       std::string_view expectedSource,
                                       std::uint64_t expectedRevision,
                                       std::string_view expectedLabel,
                                       EditorEventOutcome expectedOutcome) {
            if (diagnostic.sequence != expectedSequence || diagnostic.event.kind != expectedKind ||
                diagnostic.event.sourceId.value != expectedSource ||
                diagnostic.event.metadata.revision != expectedRevision ||
                diagnostic.event.metadata.label != expectedLabel ||
                diagnostic.event.metadata.outcome != expectedOutcome) {
                asharia::logError("Editor state event smoke saw unexpected event metadata.");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool validateStateEventOrdering() {
            EditorEventQueue eventQueue;
            EditorDiagnosticsLog diagnosticsLog;
            EditorSelectionSet selection{eventQueue};
            EditorDirtyState dirty{eventQueue};
            EditorCommandHistory history{eventQueue};

            const EditorSceneEntitySelectionId cameraTarget = target(1U);
            if (!selection.replace(std::vector<EditorSelectionItem>{item(cameraTarget)},
                                   EditorId{.value = "scene-tree"})) {
                asharia::logError("Editor state event smoke did not emit selection event.");
                return false;
            }
            if (!dirty.markDocumentDirty("scene:main", "Main Scene")) {
                asharia::logError("Editor state event smoke did not emit dirty event.");
                return false;
            }

            int value = 0;
            EditorTransaction transaction;
            transaction.addCommand(std::make_unique<TestSetIntCommand>(value, 7));
            if (!transaction.executeAll()) {
                asharia::logError("Editor state event smoke command fixture did not execute.");
                return false;
            }
            history.push(std::move(transaction));
            if (!history.undo()) {
                asharia::logError("Editor state event smoke command fixture did not undo.");
                return false;
            }
            eventQueue.push(EditorEvent{
                .kind = EditorEventKind::ValidationReported,
                .sourceId = EditorId{.value = "inspector"},
                .metadata =
                    EditorEventMetadata{
                        .revision = selection.snapshot().revision,
                        .subjectId = "selection",
                        .label = "Selection",
                        .message = "Selection validation warning.",
                        .severity = EditorEventSeverity::Warning,
                        .outcome = EditorEventOutcome::Succeeded,
                    },
            });

            diagnosticsLog.appendEvents(eventQueue.events());
            const std::span<const EditorDiagnosticEvent> recent = diagnosticsLog.recentEvents();
            if (recent.size() != 5U ||
                !expectEvent(recent[0], 1U, EditorEventKind::SelectionChanged,
                             kEditorSelectionOwnerId, 1U, "Replace",
                             EditorEventOutcome::Succeeded) ||
                !expectEvent(recent[1], 2U, EditorEventKind::DirtyStateChanged,
                             kEditorDirtyStateOwnerId, 1U, "Document",
                             EditorEventOutcome::Succeeded) ||
                !expectEvent(recent[2], 3U, EditorEventKind::CommandHistoryChanged,
                             kEditorCommandHistoryOwnerId, 1U, "push",
                             EditorEventOutcome::Succeeded) ||
                !expectEvent(recent[3], 4U, EditorEventKind::CommandHistoryChanged,
                             kEditorCommandHistoryOwnerId, 2U, "undo",
                             EditorEventOutcome::Succeeded) ||
                !expectEvent(recent[4], 5U, EditorEventKind::ValidationReported, "inspector", 1U,
                             "Selection", EditorEventOutcome::Succeeded) ||
                recent[4].event.metadata.severity != EditorEventSeverity::Warning) {
                asharia::logError("Editor state event smoke ordering contract failed.");
                return false;
            }

            eventQueue.clear();
            if (selection.replace(std::vector<EditorSelectionItem>{item(cameraTarget)},
                                  EditorId{.value = "scene-tree"}) ||
                dirty.markDocumentDirty("scene:main", "Duplicate") || !eventQueue.empty()) {
                asharia::logError("Editor state event smoke emitted no-op state events.");
                return false;
            }

            const auto emptyUndo = history.undo();
            if (emptyUndo || eventQueue.events().size() != 1U ||
                eventQueue.events().front().kind != EditorEventKind::CommandHistoryChanged ||
                eventQueue.events().front().metadata.outcome != EditorEventOutcome::Failed ||
                eventQueue.events().front().metadata.revision != history.revision()) {
                asharia::logError("Editor state event smoke missed failed command event.");
                return false;
            }

            return true;
        }

    } // namespace

    bool validateEditorStateEventSmoke(EditorRunMode mode) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }

        return validateStateEventOrdering();
    }

} // namespace asharia::editor
