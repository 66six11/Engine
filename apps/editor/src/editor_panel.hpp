#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "asharia/core/result.hpp"

#include "editor_id.hpp"

namespace asharia::editor {

    class EditorDiagnosticsLog;
    class EditorEventQueue;

    struct EditorPanelDesc {
        EditorId id;
        std::string title;
        bool defaultOpen{true};
        bool singleton{true};
    };

    struct EditorPanelState {
        bool open{true};
        bool focused{false};
        std::uint32_t contentWidth{1};
        std::uint32_t contentHeight{1};
    };

    class EditorViewportPanelHost {
    public:
        EditorViewportPanelHost() = default;
        EditorViewportPanelHost(const EditorViewportPanelHost&) = delete;
        EditorViewportPanelHost& operator=(const EditorViewportPanelHost&) = delete;
        EditorViewportPanelHost(EditorViewportPanelHost&&) = delete;
        EditorViewportPanelHost& operator=(EditorViewportPanelHost&&) = delete;
        virtual ~EditorViewportPanelHost() = default;

        virtual void requestViewport(VkExtent2D extent, VkFormat format) = 0;
        [[nodiscard]] virtual bool canDrawRequestedTexture() const = 0;
        virtual void drawRequestedTexture() = 0;
    };

    struct EditorFrameContext {
        int frameIndex{};
        VkExtent2D swapchainExtent{};
        VkFormat swapchainFormat{VK_FORMAT_UNDEFINED};
        bool smokeMode{};
        EditorEventQueue& eventQueue;
        EditorDiagnosticsLog& diagnosticsLog;
        EditorViewportPanelHost& viewportHost;
    };

    class ImGuiEditorPanel {
    public:
        ImGuiEditorPanel() = default;
        ImGuiEditorPanel(const ImGuiEditorPanel&) = delete;
        ImGuiEditorPanel& operator=(const ImGuiEditorPanel&) = delete;
        ImGuiEditorPanel(ImGuiEditorPanel&&) = delete;
        ImGuiEditorPanel& operator=(ImGuiEditorPanel&&) = delete;
        virtual ~ImGuiEditorPanel() = default;

        [[nodiscard]] virtual const EditorPanelDesc& desc() const = 0;
        virtual void prepareWindow(EditorFrameContext& context, EditorPanelState& state);
        virtual void draw(EditorFrameContext& context, EditorPanelState& state) = 0;
    };

    enum class EditorPanelLifecycleEventKind {
        Opened,
        Closed,
        Focused,
    };

    struct EditorPanelLifecycleEvent {
        EditorPanelLifecycleEventKind kind{};
        EditorId panelId;
    };

    using EditorPanelFactory = std::function<std::unique_ptr<ImGuiEditorPanel>()>;

    class EditorPanelRegistry {
    public:
        [[nodiscard]] asharia::VoidResult registerPanel(EditorPanelFactory factory);
        [[nodiscard]] bool openPanel(std::string_view panelId);
        [[nodiscard]] bool closePanel(std::string_view panelId);
        [[nodiscard]] bool focusPanel(std::string_view panelId);
        [[nodiscard]] bool isOpen(std::string_view panelId) const;
        [[nodiscard]] std::size_t panelCount() const;
        [[nodiscard]] std::size_t openPanelCount() const;

        void setEventQueue(EditorEventQueue* eventQueue);
        void drawPanels(EditorFrameContext& context);
        void clearLifecycleEvents();
        [[nodiscard]] std::span<const EditorPanelLifecycleEvent> lifecycleEvents() const;

    private:
        struct PanelEntry {
            EditorPanelDesc desc;
            EditorPanelFactory factory;
            std::unique_ptr<ImGuiEditorPanel> panel;
            EditorPanelState state;
            bool focusRequested{false};
        };

        [[nodiscard]] PanelEntry* findPanel(std::string_view panelId);
        [[nodiscard]] const PanelEntry* findPanel(std::string_view panelId) const;
        void emit(EditorPanelLifecycleEventKind kind, const EditorId& panelId);

        std::vector<PanelEntry> panels_;
        std::vector<EditorPanelLifecycleEvent> lifecycleEvents_;
        EditorEventQueue* eventQueue_{nullptr};
    };

} // namespace asharia::editor
