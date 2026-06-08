#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"

namespace asharia::editor {

    class EditorEventQueue;

    inline constexpr std::string_view kEditorCommandHistoryOwnerId = "editor.commandHistory";

    class EditorCommand {
    public:
        EditorCommand() = default;
        EditorCommand(const EditorCommand&) = delete;
        EditorCommand& operator=(const EditorCommand&) = delete;
        EditorCommand(EditorCommand&&) = default;
        EditorCommand& operator=(EditorCommand&&) = default;
        virtual ~EditorCommand() = default;

        [[nodiscard]] virtual std::string description() const = 0;
        [[nodiscard]] virtual asharia::Result<void> execute() = 0;
        [[nodiscard]] virtual asharia::Result<void> undo() = 0;
    };

    class EditorTransaction {
    public:
        EditorTransaction() = default;
        EditorTransaction(const EditorTransaction&) = delete;
        EditorTransaction& operator=(const EditorTransaction&) = delete;
        EditorTransaction(EditorTransaction&&) = default;
        EditorTransaction& operator=(EditorTransaction&&) = default;

        void addCommand(std::unique_ptr<EditorCommand> cmd);

        [[nodiscard]] asharia::Result<void> executeAll();
        [[nodiscard]] asharia::Result<void> undoAll();

        [[nodiscard]] const std::vector<std::unique_ptr<EditorCommand>>& commands() const;
        [[nodiscard]] std::string description() const;
        [[nodiscard]] std::size_t commandCount() const;

    private:
        std::vector<std::unique_ptr<EditorCommand>> commands_;
    };

    class EditorCommandHistory {
    public:
        static constexpr int kMaxHistoryDepth = 256;

        EditorCommandHistory() = default;
        explicit EditorCommandHistory(EditorEventQueue& eventQueue);
        EditorCommandHistory(const EditorCommandHistory&) = delete;
        EditorCommandHistory& operator=(const EditorCommandHistory&) = delete;
        EditorCommandHistory(EditorCommandHistory&&) = default;
        EditorCommandHistory& operator=(EditorCommandHistory&&) = default;

        void push(EditorTransaction&& transaction);
        void setEventQueue(EditorEventQueue* eventQueue) noexcept;

        [[nodiscard]] asharia::Result<void> undo();
        [[nodiscard]] asharia::Result<void> redo();

        void clear();

        [[nodiscard]] int undoDepth() const;
        [[nodiscard]] int redoDepth() const;
        [[nodiscard]] std::uint64_t revision() const noexcept;

    private:
        void emitHistoryChanged(std::string_view operation, bool succeeded, std::string message);
        void bumpRevision() noexcept;

        EditorEventQueue* eventQueue_{};
        std::uint64_t revision_{};
        std::deque<EditorTransaction> undoStack_;
        std::deque<EditorTransaction> redoStack_;
    };

} // namespace asharia::editor
