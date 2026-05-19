#include "editor_action.hpp"

#include <algorithm>
#include <utility>

#include "asharia/core/error.hpp"

#include "editor_context.hpp"

namespace asharia::editor {

    asharia::VoidResult EditorActionRegistry::registerAction(EditorActionDesc desc,
                                                             EditorActionCallback callback) {
        if (desc.id.value.empty()) {
            return std::unexpected{asharia::Error{asharia::ErrorDomain::Core, 0,
                                                  "Editor action id must not be empty"}};
        }
        if (desc.menuPath.empty()) {
            return std::unexpected{asharia::Error{asharia::ErrorDomain::Core, 0,
                                                  "Editor action menu path must not be empty"}};
        }
        if (desc.label.empty()) {
            return std::unexpected{asharia::Error{asharia::ErrorDomain::Core, 0,
                                                  "Editor action label must not be empty"}};
        }
        if (findActionEntry(desc.id.value) != nullptr) {
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Core, 0,
                               "Editor action is already registered: " + desc.id.value}};
        }
        if (desc.enabled && !callback) {
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Core, 0,
                               "Enabled editor action has no callback: " + desc.id.value}};
        }

        actions_.push_back(ActionEntry{
            .desc = std::move(desc),
            .callback = std::move(callback),
        });
        return {};
    }

    bool EditorActionRegistry::invoke(std::string_view actionId, EditorContext& context) {
        ActionEntry* entry = findActionEntry(actionId);
        if (entry == nullptr || !entry->desc.enabled || !entry->callback) {
            return false;
        }

        context.eventQueue().push(EditorEvent{
            .kind = EditorEventKind::ActionInvoked,
            .sourceId = EditorId{.value = entry->desc.id.value},
        });
        entry->callback(context);
        ++entry->invokeCount;
        return true;
    }

    const EditorActionDesc* EditorActionRegistry::findAction(std::string_view actionId) const {
        const ActionEntry* entry = findActionEntry(actionId);
        if (entry == nullptr) {
            return nullptr;
        }
        return &entry->desc;
    }

    bool EditorActionRegistry::isEnabled(std::string_view actionId) const {
        const ActionEntry* entry = findActionEntry(actionId);
        return entry != nullptr && entry->desc.enabled;
    }

    std::size_t EditorActionRegistry::actionCount() const {
        return actions_.size();
    }

    std::size_t EditorActionRegistry::enabledActionCount() const {
        return static_cast<std::size_t>(std::ranges::count_if(
            actions_, [](const ActionEntry& entry) { return entry.desc.enabled; }));
    }

    std::uint64_t EditorActionRegistry::invokeCount(std::string_view actionId) const {
        const ActionEntry* entry = findActionEntry(actionId);
        if (entry == nullptr) {
            return 0;
        }
        return entry->invokeCount;
    }

    EditorActionRegistry::ActionEntry*
    EditorActionRegistry::findActionEntry(std::string_view actionId) {
        const auto found = std::ranges::find_if(actions_, [actionId](const ActionEntry& entry) {
            return entry.desc.id.value == actionId;
        });
        if (found == actions_.end()) {
            return nullptr;
        }
        return &(*found);
    }

    const EditorActionRegistry::ActionEntry*
    EditorActionRegistry::findActionEntry(std::string_view actionId) const {
        const auto found = std::ranges::find_if(actions_, [actionId](const ActionEntry& entry) {
            return entry.desc.id.value == actionId;
        });
        if (found == actions_.end()) {
            return nullptr;
        }
        return &(*found);
    }

} // namespace asharia::editor
