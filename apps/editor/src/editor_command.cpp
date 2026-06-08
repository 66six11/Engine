#include "editor_command.hpp"

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

#include "asharia/core/error.hpp"

namespace asharia::editor {

    namespace {

        [[nodiscard]] asharia::Error commandHistoryError(std::string message) {
            return asharia::Error{asharia::ErrorDomain::Core, 0, std::move(message)};
        }

        [[nodiscard]] asharia::Error transactionRecoveryError(std::string_view operation,
                                                              const asharia::Error& original,
                                                              const asharia::Error& recovery) {
            return asharia::Error{
                original.domain, original.code,
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
        return commands_.front()->description() + " (+" +
               std::to_string(commands_.size() - 1) + " more)";
    }

    std::size_t EditorTransaction::commandCount() const {
        return commands_.size();
    }

    void EditorCommandHistory::push(EditorTransaction&& transaction) {
        redoStack_.clear();
        undoStack_.push_back(std::move(transaction));
        while (undoStack_.size() > static_cast<std::size_t>(kMaxHistoryDepth)) {
            undoStack_.pop_front();
        }
    }

    asharia::Result<void> EditorCommandHistory::undo() {
        if (undoStack_.empty()) {
            return std::unexpected{commandHistoryError("Nothing to undo")};
        }
        EditorTransaction transaction = std::move(undoStack_.back());
        undoStack_.pop_back();
        auto result = transaction.undoAll();
        if (!result) {
            undoStack_.push_back(std::move(transaction));
            return result;
        }
        redoStack_.push_back(std::move(transaction));
        return {};
    }

    asharia::Result<void> EditorCommandHistory::redo() {
        if (redoStack_.empty()) {
            return std::unexpected{commandHistoryError("Nothing to redo")};
        }
        EditorTransaction transaction = std::move(redoStack_.back());
        redoStack_.pop_back();
        auto result = transaction.executeAll();
        if (!result) {
            redoStack_.push_back(std::move(transaction));
            return result;
        }
        undoStack_.push_back(std::move(transaction));
        return {};
    }

    void EditorCommandHistory::clear() {
        undoStack_.clear();
        redoStack_.clear();
    }

    int EditorCommandHistory::undoDepth() const {
        return static_cast<int>(undoStack_.size());
    }

    int EditorCommandHistory::redoDepth() const {
        return static_cast<int>(redoStack_.size());
    }

} // namespace asharia::editor
