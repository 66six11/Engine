#include "editor_command.hpp"

#include <utility>

#include "asharia/core/error.hpp"

namespace asharia::editor {

    void EditorTransaction::addCommand(std::unique_ptr<EditorCommand> cmd) {
        commands_.push_back(std::move(cmd));
    }

    asharia::Result<void> EditorTransaction::executeAll() {
        for (auto& cmd : commands_) {
            auto result = cmd->execute();
            if (!result) {
                return result;
            }
        }
        return {};
    }

    asharia::Result<void> EditorTransaction::undoAll() {
        for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) {
            auto result = (*it)->undo();
            if (!result) {
                return result;
            }
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
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Core, 0, "Nothing to undo"}};
        }
        EditorTransaction transaction = std::move(undoStack_.back());
        undoStack_.pop_back();
        auto result = transaction.undoAll();
        if (!result) {
            return result;
        }
        redoStack_.push_back(std::move(transaction));
        return {};
    }

    asharia::Result<void> EditorCommandHistory::redo() {
        if (redoStack_.empty()) {
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Core, 0, "Nothing to redo"}};
        }
        EditorTransaction transaction = std::move(redoStack_.back());
        redoStack_.pop_back();
        auto result = transaction.executeAll();
        if (!result) {
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
