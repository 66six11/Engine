#include "editor_panel.hpp"

#include <algorithm>
#include <imgui.h>
#include <string>
#include <utility>

#include "asharia/core/error.hpp"

#include "editor_event.hpp"
#include "editor_i18n.hpp"

namespace asharia::editor {

    namespace {

        EditorEventKind panelEventKind(EditorPanelLifecycleEventKind kind) {
            switch (kind) {
            case EditorPanelLifecycleEventKind::Opened:
                return EditorEventKind::PanelOpened;
            case EditorPanelLifecycleEventKind::Closed:
                return EditorEventKind::PanelClosed;
            case EditorPanelLifecycleEventKind::Focused:
                return EditorEventKind::PanelFocused;
            }
            return EditorEventKind::PanelFocused;
        }

    } // namespace

    void ImGuiEditorPanel::prepareWindow(EditorFrameContext& context, EditorPanelState& state) {
        static_cast<void>(context);
        static_cast<void>(state);
    }

    asharia::VoidResult EditorPanelRegistry::registerPanel(EditorPanelFactory factory) {
        if (!factory) {
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Core, 0,
                               "Cannot register an editor panel without a factory"}};
        }

        std::unique_ptr<ImGuiEditorPanel> panel = factory();
        if (!panel) {
            return std::unexpected{asharia::Error{asharia::ErrorDomain::Core, 0,
                                                  "Editor panel factory returned null"}};
        }

        EditorPanelDesc desc = panel->desc();
        if (desc.id.value.empty()) {
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Core, 0, "Editor panel id must not be empty"}};
        }
        if (desc.title.empty()) {
            return std::unexpected{asharia::Error{asharia::ErrorDomain::Core, 0,
                                                  "Editor panel title must not be empty"}};
        }
        if (findPanel(desc.id.value) != nullptr) {
            return std::unexpected{
                asharia::Error{asharia::ErrorDomain::Core, 0,
                               "Editor panel is already registered: " + desc.id.value}};
        }

        EditorPanelState state{};
        state.open = desc.defaultOpen;
        panels_.push_back(PanelEntry{
            .desc = std::move(desc),
            .factory = std::move(factory),
            .panel = std::move(panel),
            .state = state,
        });
        if (panels_.back().state.open) {
            emit(EditorPanelLifecycleEventKind::Opened, panels_.back().desc.id);
        }

        return {};
    }

    bool EditorPanelRegistry::openPanel(std::string_view panelId) {
        PanelEntry* entry = findPanel(panelId);
        if (entry == nullptr) {
            return false;
        }
        if (!entry->state.open) {
            entry->state.open = true;
            emit(EditorPanelLifecycleEventKind::Opened, entry->desc.id);
        }
        return true;
    }

    bool EditorPanelRegistry::closePanel(std::string_view panelId) {
        PanelEntry* entry = findPanel(panelId);
        if (entry == nullptr) {
            return false;
        }
        if (entry->state.open) {
            entry->state.open = false;
            emit(EditorPanelLifecycleEventKind::Closed, entry->desc.id);
        }
        return true;
    }

    bool EditorPanelRegistry::focusPanel(std::string_view panelId) {
        PanelEntry* entry = findPanel(panelId);
        if (entry == nullptr) {
            return false;
        }
        if (!entry->state.open) {
            entry->state.open = true;
            emit(EditorPanelLifecycleEventKind::Opened, entry->desc.id);
        }
        entry->focusRequested = true;
        return true;
    }

    bool EditorPanelRegistry::isOpen(std::string_view panelId) const {
        const PanelEntry* entry = findPanel(panelId);
        return entry != nullptr && entry->state.open;
    }

    const EditorPanelDesc* EditorPanelRegistry::findPanelDesc(std::string_view panelId) const {
        const PanelEntry* entry = findPanel(panelId);
        if (entry == nullptr) {
            return nullptr;
        }
        return &entry->desc;
    }

    std::size_t EditorPanelRegistry::panelCount() const {
        return panels_.size();
    }

    std::size_t EditorPanelRegistry::openPanelCount() const {
        return static_cast<std::size_t>(std::ranges::count_if(
            panels_, [](const PanelEntry& entry) { return entry.state.open; }));
    }

    std::string EditorPanelRegistry::panelWindowTitle(std::string_view panelId,
                                                      const EditorI18n& i18n) const {
        const PanelEntry* entry = findPanel(panelId);
        if (entry == nullptr) {
            return std::string{panelId};
        }
        return i18n.label(EditorI18nLabelDesc{.key = entry->desc.titleKey,
                                              .stableId = entry->desc.id.value,
                                              .fallback = entry->desc.title});
    }

    void EditorPanelRegistry::setEventQueue(EditorEventQueue* eventQueue) {
        eventQueue_ = eventQueue;
    }

    void EditorPanelRegistry::drawPanels(EditorFrameContext& context) {
        for (PanelEntry& entry : panels_) {
            if (!entry.state.open || entry.panel == nullptr) {
                continue;
            }

            entry.panel->prepareWindow(context, entry.state);
            if (entry.focusRequested) {
                ImGui::SetNextWindowFocus();
                entry.focusRequested = false;
            }

            bool open = entry.state.open;
            const std::string windowTitle = panelWindowTitle(entry.desc.id.value, context.ui.i18n);
            const bool visible = ImGui::Begin(windowTitle.c_str(), &open);
            const bool wasOpen = entry.state.open;
            entry.state.open = open;
            if (wasOpen && !entry.state.open) {
                emit(EditorPanelLifecycleEventKind::Closed, entry.desc.id);
            }

            const bool wasFocused = entry.state.focused;
            entry.state.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            if (!wasFocused && entry.state.focused) {
                emit(EditorPanelLifecycleEventKind::Focused, entry.desc.id);
            }

            const ImVec2 available = ImGui::GetContentRegionAvail();
            entry.state.contentWidth =
                std::max(1U, static_cast<std::uint32_t>(std::max(available.x, 1.0F)));
            entry.state.contentHeight =
                std::max(1U, static_cast<std::uint32_t>(std::max(available.y, 1.0F)));

            if (visible && entry.state.open) {
                entry.panel->draw(context, entry.state);
            }
            ImGui::End();
        }
    }

    void EditorPanelRegistry::clearLifecycleEvents() {
        lifecycleEvents_.clear();
    }

    std::span<const EditorPanelLifecycleEvent> EditorPanelRegistry::lifecycleEvents() const {
        return lifecycleEvents_;
    }

    EditorPanelRegistry::PanelEntry* EditorPanelRegistry::findPanel(std::string_view panelId) {
        const auto found = std::ranges::find_if(
            panels_, [panelId](const PanelEntry& entry) { return entry.desc.id.value == panelId; });
        if (found == panels_.end()) {
            return nullptr;
        }
        return &(*found);
    }

    const EditorPanelRegistry::PanelEntry*
    EditorPanelRegistry::findPanel(std::string_view panelId) const {
        const auto found = std::ranges::find_if(
            panels_, [panelId](const PanelEntry& entry) { return entry.desc.id.value == panelId; });
        if (found == panels_.end()) {
            return nullptr;
        }
        return &(*found);
    }

    void EditorPanelRegistry::emit(EditorPanelLifecycleEventKind kind, const EditorId& panelId) {
        lifecycleEvents_.push_back(EditorPanelLifecycleEvent{
            .kind = kind,
            .panelId = panelId,
        });
        if (eventQueue_ != nullptr) {
            eventQueue_->push(EditorEvent{
                .kind = panelEventKind(kind),
                .sourceId = panelId,
            });
        }
    }

} // namespace asharia::editor
