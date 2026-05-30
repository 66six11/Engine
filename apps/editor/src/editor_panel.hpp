#pragma once

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
#include "editor_render_graph_snapshot.hpp"
#include "editor_viewport.hpp"

namespace asharia::editor {

    enum class EditorPanelCategory {
        Viewport,
        Diagnostics,
        Tools,
        Settings,
    };

    enum class EditorDockSlot {
        Center,
        RightTop,
        RightBottom,
        Bottom,
    };

    class EditorDiagnosticsLog;
    class EditorEventQueue;
    class EditorFrameDebugger;
    class EditorI18n;
    class EditorInputRouter;
    class EditorSettingsController;
    class EditorToolRegistry;

    struct EditorPanelDesc {
        EditorId id;
        std::string title;
        std::string titleKey;
        bool defaultOpen{true};
        bool singleton{true};
        EditorPanelCategory category{EditorPanelCategory::Tools};
        EditorDockSlot preferredDock{EditorDockSlot::Center};
    };

    struct EditorPanelState {
        bool open{true};
        bool focused{false};
        std::uint32_t contentWidth{1};
        std::uint32_t contentHeight{1};
    };

    struct EditorFrameUiContext {
        int frameIndex{};
        EditorExtent2D swapchainExtent;
        bool smokeMode{};
        EditorI18n& i18n;
    };

    struct EditorPanelWindowContext {
        const EditorFrameUiContext& ui;
    };

    struct EditorFrameDiagnosticsContext {
        EditorDiagnosticsLog& log;
        EditorFrameDebugger& frameDebugger;
    };

    struct EditorFrameSettingsContext {
        EditorSettingsController& controller;
    };

    struct EditorFrameToolContext {
        const EditorToolRegistry& registry;
    };

    struct EditorFrameInputContext {
        EditorInputRouter& router;
    };

    struct EditorFrameRenderGraphContext {
        EditorRenderGraphSnapshotProvider& snapshots;
    };

    struct EditorFrameViewportContext {
        EditorViewportPanelHost& host;
    };

    struct EditorViewportPanelDrawContext {
        const EditorFrameUiContext& ui;
        const EditorFrameToolContext& tools;
        EditorFrameInputContext& input;
        EditorFrameViewportContext& viewport;
    };

    struct EditorDiagnosticsPanelDrawContext {
        const EditorFrameUiContext& ui;
        EditorFrameDiagnosticsContext& diagnostics;
        EditorFrameInputContext& input;
        EditorFrameRenderGraphContext& renderGraph;
    };

    struct EditorSettingsPanelDrawContext {
        const EditorFrameUiContext& ui;
        EditorFrameSettingsContext& settings;
    };

    struct EditorToolsPanelDrawContext {
        const EditorFrameUiContext& ui;
        EditorFrameSettingsContext& settings;
    };

    struct EditorPanelDrawContext;

    struct EditorFrameContext {
        EditorFrameUiContext ui;
        EditorFrameDiagnosticsContext diagnostics;
        EditorFrameSettingsContext settings;
        EditorFrameToolContext tools;
        EditorFrameInputContext input;
        EditorFrameRenderGraphContext renderGraph;
        EditorFrameViewportContext viewport;
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
        virtual void prepareWindow(EditorPanelWindowContext& context, EditorPanelState& state);
        virtual void draw(EditorPanelDrawContext& context, EditorPanelState& state) = 0;
    };

    class ImGuiViewportEditorPanel : public ImGuiEditorPanel {
    public:
        void draw(EditorPanelDrawContext& context, EditorPanelState& state) final;

    private:
        virtual void drawViewportPanel(EditorViewportPanelDrawContext& context,
                                       EditorPanelState& state) = 0;
    };

    class ImGuiDiagnosticsEditorPanel : public ImGuiEditorPanel {
    public:
        void draw(EditorPanelDrawContext& context, EditorPanelState& state) final;

    private:
        virtual void drawDiagnosticsPanel(EditorDiagnosticsPanelDrawContext& context,
                                          EditorPanelState& state) = 0;
    };

    class ImGuiSettingsEditorPanel : public ImGuiEditorPanel {
    public:
        void draw(EditorPanelDrawContext& context, EditorPanelState& state) final;

    private:
        virtual void drawSettingsPanel(EditorSettingsPanelDrawContext& context,
                                       EditorPanelState& state) = 0;
    };

    class ImGuiToolsEditorPanel : public ImGuiEditorPanel {
    public:
        void draw(EditorPanelDrawContext& context, EditorPanelState& state) final;

    private:
        virtual void drawToolsPanel(EditorToolsPanelDrawContext& context,
                                    EditorPanelState& state) = 0;
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
        [[nodiscard]] const EditorPanelDesc* findPanelDesc(std::string_view panelId) const;
        [[nodiscard]] std::size_t panelCount() const;
        [[nodiscard]] std::size_t openPanelCount() const;
        [[nodiscard]] std::string panelWindowTitle(std::string_view panelId,
                                                   const EditorI18n& i18n) const;

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
