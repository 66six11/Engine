#include "editor_command.hpp"

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/core/error.hpp"

#include "editor_event.hpp"

namespace asharia::editor {

    namespace {

        [[nodiscard]] asharia::Error commandHistoryError(std::string message) {
            return asharia::Error{asharia::ErrorDomain::Core, 0, std::move(message)};
        }

        [[nodiscard]] asharia::Error transactionRecoveryError(std::string_view operation,
                                                              const asharia::Error& original,
                                                              const asharia::Error& recovery) {
            return asharia::Error{original.domain, original.code,
                                  std::string{operation} + " failed: " + original.message +
                                      "; transaction recovery also failed: " + recovery.message};
        }

    } // namespace

    void EditorTransaction::addCommand(std::unique_ptr<EditorCommand> cmd) {
        commands_.push_back(std::move(cmd));
    }

    asharia::Result<void> EditorTransaction::executeAll() {
        std::size_t executedCount = 0U;
        for (auto& cmd : commands_) {
            auto result = cmd->execute();
            if (!result) {
                asharia::Error executeError = result.error();
                for (std::size_t rollbackCount = executedCount; rollbackCount > 0U;
                     --rollbackCount) {
                    auto rollback = commands_[rollbackCount - 1U]->undo();
                    if (!rollback) {
                        return std::unexpected{transactionRecoveryError(
                            "Editor transaction execute", executeError, rollback.error())};
                    }
                }
                return std::unexpected{std::move(executeError)};
            }
            ++executedCount;
        }
        return {};
    }

    asharia::Result<void> EditorTransaction::undoAll() {
        std::size_t undoneCount = 0U;
        for (std::size_t reverseIndex = commands_.size(); reverseIndex > 0U; --reverseIndex) {
            auto result = commands_[reverseIndex - 1U]->undo();
            if (!result) {
                asharia::Error undoError = result.error();
                const std::size_t restoreStart = commands_.size() - undoneCount;
                for (std::size_t restoreIndex = restoreStart; restoreIndex < commands_.size();
                     ++restoreIndex) {
                    auto restore = commands_[restoreIndex]->execute();
                    if (!restore) {
                        return std::unexpected{transactionRecoveryError(
                            "Editor transaction undo", undoError, restore.error())};
                    }
                }
                return std::unexpected{std::move(undoError)};
            }
            ++undoneCount;
        }
        return {};
    }

    const std::vector<std::unique_ptr<EditorCommand>>& EditorTransaction::commands() const {
        return commands_;
    }

    std::string EditorTransaction::description() const {
        if (commands_.empty()) {
            return "(empty)";
        }
        if (commands_.size() == 1) {
            return commands_.front()->description();
        }
        return commands_.front()->description() + " (+" + std::to_string(commands_.size() - 1) +
               " more)";
    }

    std::size_t EditorTransaction::commandCount() const {
        return commands_.size();
    }

    EditorCommandHistory::EditorCommandHistory(EditorEventQueue& eventQueue)
        : eventQueue_(&eventQueue) {}

    void EditorCommandHistory::setEventQueue(EditorEventQueue* eventQueue) noexcept {
        eventQueue_ = eventQueue;
    }

    void EditorCommandHistory::push(EditorTransaction&& transaction) {
        const std::string description = transaction.description();
        redoStack_.clear();
        undoStack_.push_back(std::move(transaction));
        while (undoStack_.size() > static_cast<std::size_t>(kMaxHistoryDepth)) {
            undoStack_.pop_front();
        }
        bumpRevision();
        emitHistoryChanged("push", true, description);
    }

    asharia::Result<void> EditorCommandHistory::undo() {
        if (undoStack_.empty()) {
            asharia::Error error = commandHistoryError("Nothing to undo");
            emitHistoryChanged("undo", false, error.message);
            return std::unexpected{std::move(error)};
        }
        EditorTransaction transaction = std::move(undoStack_.back());
        const std::string description = transaction.description();
        undoStack_.pop_back();
        auto result = transaction.undoAll();
        if (!result) {
            undoStack_.push_back(std::move(transaction));
            emitHistoryChanged("undo", false, result.error().message);
            return result;
        }
        redoStack_.push_back(std::move(transaction));
        bumpRevision();
        emitHistoryChanged("undo", true, description);
        return {};
    }

    asharia::Result<void> EditorCommandHistory::redo() {
        if (redoStack_.empty()) {
            asharia::Error error = commandHistoryError("Nothing to redo");
            emitHistoryChanged("redo", false, error.message);
            return std::unexpected{std::move(error)};
        }
        EditorTransaction transaction = std::move(redoStack_.back());
        const std::string description = transaction.description();
        redoStack_.pop_back();
        auto result = transaction.executeAll();
        if (!result) {
            redoStack_.push_back(std::move(transaction));
            emitHistoryChanged("redo", false, result.error().message);
            return result;
        }
        undoStack_.push_back(std::move(transaction));
        bumpRevision();
        emitHistoryChanged("redo", true, description);
        return {};
    }

    void EditorCommandHistory::clear() {
        if (undoStack_.empty() && redoStack_.empty()) {
            return;
        }
        undoStack_.clear();
        redoStack_.clear();
        bumpRevision();
        emitHistoryChanged("clear", true, "clear");
    }

    int EditorCommandHistory::undoDepth() const {
        return static_cast<int>(undoStack_.size());
    }

    int EditorCommandHistory::redoDepth() const {
        return static_cast<int>(redoStack_.size());
    }

    std::uint64_t EditorCommandHistory::revision() const noexcept {
        return revision_;
    }

    void EditorCommandHistory::emitHistoryChanged(std::string_view operation, bool succeeded,
                                                  std::string message) {
        if (eventQueue_ == nullptr) {
            return;
        }
        eventQueue_->push(EditorEvent{
            .kind = EditorEventKind::CommandHistoryChanged,
            .sourceId = EditorId{.value = std::string{kEditorCommandHistoryOwnerId}},
            .metadata =
                EditorEventMetadata{
                    .revision = revision_,
                    .subjectId = {},
                    .label = std::string{operation},
                    .message = std::move(message),
                    .severity = succeeded ? EditorEventSeverity::Info : EditorEventSeverity::Error,
                    .outcome =
                        succeeded ? EditorEventOutcome::Succeeded : EditorEventOutcome::Failed,
                },
        });
    }

    void EditorCommandHistory::bumpRevision() noexcept {
        ++revision_;
    }

} // namespace asharia::editor
