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

#include "editor_asset_catalog.hpp"
#include "editor_asset_icon.hpp"
#include "editor_asset_import_settings_command.hpp"
#include "editor_command.hpp"
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
        Left,
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
    class EditorSelectionSet;
    struct EditorSettings;
    class EditorSettingsController;
    class EditorToolManager;
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
        const EditorToolManager& manager;
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

    struct EditorSceneViewPanelDrawContext {
        const EditorFrameUiContext& ui;
        const EditorSettings& settings;
        const EditorFrameToolContext& tools;
        EditorInputRouter& inputRouter;
        EditorViewportPanelHost& viewportHost;
    };

    struct EditorSceneTreePanelDrawContext {
        const EditorFrameUiContext& ui;
        const EditorSelectionSet& selection;
    };

    struct EditorInspectorPanelDrawContext {
        const EditorFrameUiContext& ui;
        const EditorSelectionSet& selection;
        const EditorCommandHistory& commandHistory;
    };

    struct EditorLogPanelDrawContext {
        const EditorFrameUiContext& ui;
        EditorDiagnosticsLog& diagnosticsLog;
        EditorInputRouter& inputRouter;
    };

    struct EditorRenderGraphPanelDrawContext {
        const EditorFrameUiContext& ui;
        EditorRenderGraphSnapshotProvider& snapshots;
    };

    struct EditorFrameDebuggerPanelDrawContext {
        const EditorFrameUiContext& ui;
        EditorFrameDebugger& frameDebugger;
    };

    struct EditorSettingsPanelDrawContext {
        const EditorFrameUiContext& ui;
        EditorSettingsController& settings;
    };

    struct EditorUiStylePreviewPanelDrawContext {
        EditorSettingsController& settings;
    };

    struct EditorAssetBrowserPanelDrawContext {
        const EditorFrameUiContext& ui;
        const EditorAssetIconRegistry& icons;
        const asharia::asset::AssetCatalogView& catalogView;
        const EditorAssetCatalogSnapshot* catalogSnapshot{};
        std::span<const EditorAssetCatalogDiagnostic> catalogDiagnostics;
        EditorCommandHistory& commandHistory;
        EditorAssetReimportRequestLog& reimportRequests;
        EditorAssetReimportPendingState& pendingReimports;
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
        const EditorSelectionSet& selection;
        const EditorAssetIconRegistry& assetIcons;
        const asharia::asset::AssetCatalogView& assetCatalogView;
        const EditorAssetCatalogSnapshot* assetCatalogSnapshot{};
        std::span<const EditorAssetCatalogDiagnostic> assetCatalogDiagnostics;
        EditorCommandHistory& commandHistory;
        EditorAssetReimportRequestLog& assetReimportRequests;
        EditorAssetReimportPendingState& assetPendingReimports;
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

    class ImGuiSceneViewEditorPanel : public ImGuiEditorPanel {
    public:
        void draw(EditorPanelDrawContext& context, EditorPanelState& state) final;

    private:
        virtual void drawSceneViewPanel(EditorSceneViewPanelDrawContext& context,
                                        EditorPanelState& state) = 0;
    };

    class ImGuiSceneTreeEditorPanel : public ImGuiEditorPanel {
    public:
        void draw(EditorPanelDrawContext& context, EditorPanelState& state) final;

    private:
        virtual void drawSceneTreePanel(EditorSceneTreePanelDrawContext& context,
                                        EditorPanelState& state) = 0;
    };

    class ImGuiInspectorEditorPanel : public ImGuiEditorPanel {
    public:
        void draw(EditorPanelDrawContext& context, EditorPanelState& state) final;

    private:
        virtual void drawInspectorPanel(EditorInspectorPanelDrawContext& context,
                                        EditorPanelState& state) = 0;
    };

    class ImGuiLogEditorPanel : public ImGuiEditorPanel {
    public:
        void draw(EditorPanelDrawContext& context, EditorPanelState& state) final;

    private:
        virtual void drawLogPanel(EditorLogPanelDrawContext& context, EditorPanelState& state) = 0;
    };

    class ImGuiRenderGraphEditorPanel : public ImGuiEditorPanel {
    public:
        void draw(EditorPanelDrawContext& context, EditorPanelState& state) final;

    private:
        virtual void drawRenderGraphPanel(EditorRenderGraphPanelDrawContext& context,
                                          EditorPanelState& state) = 0;
    };

    class ImGuiFrameDebuggerEditorPanel : public ImGuiEditorPanel {
    public:
        void draw(EditorPanelDrawContext& context, EditorPanelState& state) final;

    private:
        virtual void drawFrameDebuggerPanel(EditorFrameDebuggerPanelDrawContext& context,
                                            EditorPanelState& state) = 0;
    };

    class ImGuiEditorSettingsPanel : public ImGuiEditorPanel {
    public:
        void draw(EditorPanelDrawContext& context, EditorPanelState& state) final;

    private:
        virtual void drawEditorSettingsPanel(EditorSettingsPanelDrawContext& context,
                                             EditorPanelState& state) = 0;
    };

    class ImGuiUiStylePreviewEditorPanel : public ImGuiEditorPanel {
    public:
        void draw(EditorPanelDrawContext& context, EditorPanelState& state) final;

    private:
        virtual void drawUiStylePreviewPanel(EditorUiStylePreviewPanelDrawContext& context,
                                             EditorPanelState& state) = 0;
    };

    class ImGuiAssetBrowserEditorPanel : public ImGuiEditorPanel {
    public:
        void draw(EditorPanelDrawContext& context, EditorPanelState& state) final;

    private:
        virtual void drawAssetBrowserPanel(EditorAssetBrowserPanelDrawContext& context,
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
