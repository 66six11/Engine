#include "editor_shortcut_router.hpp"

#include <charconv>
#include <imgui.h>
#include <optional>
#include <string>
#include <string_view>

#include "editor_action.hpp"
#include "editor_context.hpp"

namespace asharia::editor {

    namespace {

        [[nodiscard]] std::string normalizeShortcutToken(std::string_view token) {
            std::string normalized;
            normalized.reserve(token.size());
            for (const char character : token) {
                if (character == ' ' || character == '\t') {
                    continue;
                }
                if (character >= 'a' && character <= 'z') {
                    normalized.push_back(
                        static_cast<char>('A' + static_cast<int>(character - 'a')));
                } else {
                    normalized.push_back(character);
                }
            }
            return normalized;
        }

        [[nodiscard]] std::optional<ImGuiKey> functionKeyFromToken(std::string_view token) {
            if (token.size() < 2 || token.front() != 'F') {
                return std::nullopt;
            }

            int functionKeyIndex = 0;
            const char* first = token.data() + 1;
            const char* last = token.data() + token.size();
            const auto parsed = std::from_chars(first, last, functionKeyIndex);
            if (parsed.ec != std::errc{} || parsed.ptr != last || functionKeyIndex < 1 ||
                functionKeyIndex > 24) {
                return std::nullopt;
            }

            return static_cast<ImGuiKey>(static_cast<int>(ImGuiKey_F1) + functionKeyIndex - 1);
        }

        [[nodiscard]] std::optional<ImGuiKey> keyFromToken(std::string_view token) {
            if (token.size() == 1) {
                const char character = token.front();
                if (character >= 'A' && character <= 'Z') {
                    return static_cast<ImGuiKey>(static_cast<int>(ImGuiKey_A) +
                                                static_cast<int>(character - 'A'));
                }
                if (character >= '0' && character <= '9') {
                    return static_cast<ImGuiKey>(static_cast<int>(ImGuiKey_0) +
                                                static_cast<int>(character - '0'));
                }
            }

            if (const std::optional<ImGuiKey> functionKey = functionKeyFromToken(token);
                functionKey) {
                return functionKey;
            }
            if (token == "TAB") {
                return ImGuiKey_Tab;
            }
            if (token == "ENTER") {
                return ImGuiKey_Enter;
            }
            if (token == "ESC" || token == "ESCAPE") {
                return ImGuiKey_Escape;
            }
            if (token == "SPACE") {
                return ImGuiKey_Space;
            }
            if (token == "DELETE" || token == "DEL") {
                return ImGuiKey_Delete;
            }

            return std::nullopt;
        }

        [[nodiscard]] std::optional<ImGuiKeyChord> shortcutChordFromText(
            std::string_view shortcut) {
            if (shortcut.empty()) {
                return std::nullopt;
            }

            ImGuiKeyChord chord = 0;
            bool hasKey = false;
            std::size_t tokenStart = 0;
            while (tokenStart <= shortcut.size()) {
                const std::size_t tokenEnd = shortcut.find('+', tokenStart);
                const std::string token = normalizeShortcutToken(shortcut.substr(
                    tokenStart, tokenEnd == std::string_view::npos
                                    ? std::string_view::npos
                                    : tokenEnd - tokenStart));
                if (token.empty()) {
                    return std::nullopt;
                }

                if (token == "CTRL" || token == "CONTROL") {
                    chord |= ImGuiMod_Ctrl;
                } else if (token == "SHIFT") {
                    chord |= ImGuiMod_Shift;
                } else if (token == "ALT") {
                    chord |= ImGuiMod_Alt;
                } else if (token == "SUPER" || token == "CMD" || token == "COMMAND") {
                    chord |= ImGuiMod_Super;
                } else if (const std::optional<ImGuiKey> key = keyFromToken(token); key) {
                    if (hasKey) {
                        return std::nullopt;
                    }
                    chord |= *key;
                    hasKey = true;
                } else {
                    return std::nullopt;
                }

                if (tokenEnd == std::string_view::npos) {
                    break;
                }
                tokenStart = tokenEnd + 1;
            }

            if (!hasKey) {
                return std::nullopt;
            }
            return chord;
        }

    } // namespace

    void EditorShortcutRouter::beginFrame(const EditorInputSnapshot& input) {
        shortcutsEnabled_ = input.shortcutsEnabled;
        ++stats_.evaluatedFrames;
        if (!shortcutsEnabled_) {
            ++stats_.blockedFrames;
        }
    }

    bool EditorShortcutRouter::routeImGuiShortcuts(EditorActionRegistry& actionRegistry,
                                                   EditorContext& context) {
        bool invoked = false;
        actionRegistry.visitActions([&](const EditorActionDesc& action) {
            if (invoked || action.shortcut.empty()) {
                return;
            }

            const std::optional<ImGuiKeyChord> chord = shortcutChordFromText(action.shortcut);
            if (!chord) {
                ++stats_.invalidShortcuts;
                return;
            }
            if (!action.enabled) {
                return;
            }
            invoked = routeShortcut(actionRegistry, context, action.id.value,
                                    shortcutsEnabled_ &&
                                        ImGui::Shortcut(*chord, ImGuiInputFlags_RouteGlobal));
        });
        return invoked;
    }

    bool EditorShortcutRouter::routeShortcut(EditorActionRegistry& actionRegistry,
                                             EditorContext& context, std::string_view actionId,
                                             bool pressed) {
        if (!shortcutsEnabled_ || !pressed) {
            return false;
        }

        ++stats_.shortcutMatches;
        if (!actionRegistry.invoke(actionId, context)) {
            return false;
        }

        ++stats_.shortcutInvocations;
        return true;
    }

    EditorShortcutRouterStats EditorShortcutRouter::stats() const {
        return stats_;
    }

} // namespace asharia::editor
