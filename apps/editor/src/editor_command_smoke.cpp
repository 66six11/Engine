#include "editor_command_smoke.hpp"

#include <memory>
#include <string>
#include <utility>

#include "asharia/core/log.hpp"
#include "asharia/core/result.hpp"

#include "editor_command.hpp"
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

    } // namespace

    bool validateEditorCommandSmoke(EditorRunMode mode) {
        if (!isEditorSmokeMode(mode)) {
            return true;
        }

        int testValue = 0;
        constexpr int kNewValue = 42;
        EditorCommandHistory history;
        {
            auto transaction = EditorTransaction{};
            transaction.addCommand(std::make_unique<TestSetIntCommand>(testValue, kNewValue));
            history.push(std::move(transaction));
        }
        if (history.undoDepth() != 1 || history.redoDepth() != 0) {
            asharia::logError("Editor command smoke: invalid depth after push.");
            return false;
        }
        auto undoResult = history.undo();
        if (!undoResult || testValue != 0 || history.undoDepth() != 0 || history.redoDepth() != 1) {
            asharia::logError("Editor command smoke: undo did not restore value.");
            return false;
        }
        auto redoResult = history.redo();
        if (!redoResult || testValue != kNewValue || history.undoDepth() != 1 ||
            history.redoDepth() != 0) {
            asharia::logError("Editor command smoke: redo did not reapply value.");
            return false;
        }
        auto emptyUndo = history.undo();
        auto doubleUndo = history.undo();
        if (doubleUndo) {
            asharia::logError("Editor command smoke: double undo should have failed.");
            return false;
        }
        static_cast<void>(emptyUndo);
        return true;
    }

} // namespace asharia::editor
