#include "editor_i18n.hpp"

#include <array>
#include <ranges>

namespace asharia::editor {

    namespace {

        constexpr std::array kCatalog{
            EditorI18nTextEntry{.key = "menu.file", .enUs = "File", .zhHans = "文件"},
            EditorI18nTextEntry{.key = "menu.view", .enUs = "View", .zhHans = "视图"},
            EditorI18nTextEntry{.key = "menu.debug", .enUs = "Debug", .zhHans = "调试"},
            EditorI18nTextEntry{
                .key = "action.file.newScene", .enUs = "New Scene", .zhHans = "新建场景"},
            EditorI18nTextEntry{.key = "action.file.open", .enUs = "Open...", .zhHans = "打开..."},
            EditorI18nTextEntry{.key = "action.file.exit", .enUs = "Exit", .zhHans = "退出"},
            EditorI18nTextEntry{
                .key = "action.view.sceneView", .enUs = "Scene View", .zhHans = "场景视图"},
            EditorI18nTextEntry{.key = "action.view.log", .enUs = "Log", .zhHans = "日志"},
            EditorI18nTextEntry{
                .key = "action.view.renderGraph", .enUs = "Live RG View", .zhHans = "实时 RG 视图"},
            EditorI18nTextEntry{
                .key = "action.view.frameDebugger", .enUs = "Frame Debugger", .zhHans = "帧调试器"},
            EditorI18nTextEntry{.key = "action.view.uiStylePreview",
                                .enUs = "UI Style Preview",
                                .zhHans = "UI 样式预览"},
            EditorI18nTextEntry{
                .key = "action.debug.captureFrame", .enUs = "Capture Frame", .zhHans = "捕获帧"},
            EditorI18nTextEntry{
                .key = "action.debug.resumeFrame", .enUs = "Resume", .zhHans = "继续"},
            EditorI18nTextEntry{
                .key = "panel.sceneView", .enUs = "Scene View", .zhHans = "场景视图"},
            EditorI18nTextEntry{
                .key = "panel.renderGraph", .enUs = "Live RG View", .zhHans = "实时 RG 视图"},
            EditorI18nTextEntry{
                .key = "panel.frameDebugger", .enUs = "Frame Debugger", .zhHans = "帧调试器"},
            EditorI18nTextEntry{.key = "panel.log", .enUs = "Log", .zhHans = "日志"},
            EditorI18nTextEntry{
                .key = "panel.uiStylePreview", .enUs = "UI Style Preview", .zhHans = "UI 样式预览"},
            EditorI18nTextEntry{.key = "common.yes", .enUs = "yes", .zhHans = "是"},
            EditorI18nTextEntry{.key = "common.no", .enUs = "no", .zhHans = "否"},
            EditorI18nTextEntry{.key = "scene.swapchain", .enUs = "Swapchain", .zhHans = "交换链"},
            EditorI18nTextEntry{.key = "scene.viewport", .enUs = "Viewport", .zhHans = "视口"},
            EditorI18nTextEntry{
                .key = "scene.viewportFrame", .enUs = "Viewport Frame", .zhHans = "视口帧"},
            EditorI18nTextEntry{.key = "log.initialized",
                                .enUs = "Editor shell initialized with GLFW + Vulkan + Dear ImGui.",
                                .zhHans =
                                    "编辑器 shell 已通过 GLFW + Vulkan + Dear ImGui 初始化。"},
            EditorI18nTextEntry{.key = "log.mode", .enUs = "Mode", .zhHans = "模式"},
            EditorI18nTextEntry{.key = "log.mode.smoke", .enUs = "smoke", .zhHans = "smoke"},
            EditorI18nTextEntry{
                .key = "log.mode.interactive", .enUs = "interactive", .zhHans = "交互"},
            EditorI18nTextEntry{
                .key = "log.inputCapture", .enUs = "Input capture", .zhHans = "输入捕获"},
            EditorI18nTextEntry{
                .key = "log.sceneViewInput", .enUs = "Scene View input", .zhHans = "场景视图输入"},
            EditorI18nTextEntry{.key = "log.mouse", .enUs = "mouse", .zhHans = "鼠标"},
            EditorI18nTextEntry{.key = "log.keyboard", .enUs = "keyboard", .zhHans = "键盘"},
            EditorI18nTextEntry{.key = "log.text", .enUs = "text", .zhHans = "文本"},
            EditorI18nTextEntry{.key = "log.hovered", .enUs = "hovered", .zhHans = "悬停"},
            EditorI18nTextEntry{.key = "log.focused", .enUs = "focused", .zhHans = "聚焦"},
            EditorI18nTextEntry{
                .key = "log.acceptsMouse", .enUs = "accepts mouse", .zhHans = "接受鼠标"},
            EditorI18nTextEntry{.key = "log.shortcuts", .enUs = "shortcuts", .zhHans = "快捷键"},
            EditorI18nTextEntry{.key = "log.recentEvents",
                                .enUs = "Recent editor events:",
                                .zhHans = "最近编辑器事件："},
            EditorI18nTextEntry{.key = "log.noEvents",
                                .enUs = "No editor events this session.",
                                .zhHans = "本次会话没有编辑器事件。"},
            EditorI18nTextEntry{.key = "frameDebug.noCapture",
                                .enUs = "No frame debug capture.",
                                .zhHans = "没有帧调试捕获。"},
            EditorI18nTextEntry{
                .key = "frameDebug.imageEmpty", .enUs = "Image: -", .zhHans = "图像：-"},
            EditorI18nTextEntry{.key = "frameDebug.image", .enUs = "Image", .zhHans = "图像"},
            EditorI18nTextEntry{
                .key = "frameDebug.noImage", .enUs = "No image", .zhHans = "无图像"},
            EditorI18nTextEntry{.key = "frameDebug.preview", .enUs = "Preview", .zhHans = "预览"},
            EditorI18nTextEntry{.key = "frameDebug.status.frozen",
                                .enUs = "frozen captured frame",
                                .zhHans = "冻结的已捕获帧"},
            EditorI18nTextEntry{.key = "frameDebug.status.latestNotPaused",
                                .enUs = "latest captured frame, not paused",
                                .zhHans = "最新捕获帧，未暂停"},
            EditorI18nTextEntry{.key = "frameDebug.rgSource",
                                .enUs = "Frame Debug RG View",
                                .zhHans = "帧调试 RG 视图"},
            EditorI18nTextEntry{.key = "renderGraph.noLiveSnapshot",
                                .enUs = "No live RenderGraph snapshot yet.",
                                .zhHans = "暂无实时 RenderGraph 快照。"},
            EditorI18nTextEntry{
                .key = "renderGraph.liveSource", .enUs = "Live RG View", .zhHans = "实时 RG 视图"},
            EditorI18nTextEntry{.key = "renderGraph.status.latestCompiled",
                                .enUs = "latest compiled RenderView",
                                .zhHans = "最新编译的 RenderView"},
            EditorI18nTextEntry{
                .key = "renderGraph.snapshot", .enUs = "Snapshot", .zhHans = "快照"},
            EditorI18nTextEntry{.key = "renderGraph.submittedEpoch",
                                .enUs = "submitted epoch",
                                .zhHans = "提交帧序号"},
            EditorI18nTextEntry{.key = "renderGraph.passes", .enUs = "Passes", .zhHans = "Pass"},
            EditorI18nTextEntry{
                .key = "renderGraph.resources", .enUs = "Resources", .zhHans = "资源"},
            EditorI18nTextEntry{
                .key = "renderGraph.accessEdges", .enUs = "Access edges", .zhHans = "访问边"},
            EditorI18nTextEntry{
                .key = "renderGraph.dependencies", .enUs = "Dependencies", .zhHans = "依赖"},
            EditorI18nTextEntry{
                .key = "renderGraph.transitions", .enUs = "Transitions", .zhHans = "转换"},
            EditorI18nTextEntry{.key = "renderGraph.colors", .enUs = "Colors:", .zhHans = "颜色："},
            EditorI18nTextEntry{.key = "renderGraph.read", .enUs = "read", .zhHans = "读"},
            EditorI18nTextEntry{.key = "renderGraph.write", .enUs = "write", .zhHans = "写"},
            EditorI18nTextEntry{
                .key = "renderGraph.readWrite", .enUs = "read/write", .zhHans = "读/写"},
            EditorI18nTextEntry{
                .key = "renderGraph.resource", .enUs = "Resource", .zhHans = "资源"},
            EditorI18nTextEntry{.key = "renderGraph.noTimeline",
                                .enUs = "No pass/resource timeline.",
                                .zhHans = "没有 pass/resource 时间线。"},
            EditorI18nTextEntry{
                .key = "renderGraph.accessEvents", .enUs = "Access Events", .zhHans = "访问事件"},
            EditorI18nTextEntry{.key = "renderGraph.noAccessEvents",
                                .enUs = "No resource access events.",
                                .zhHans = "没有资源访问事件。"},
            EditorI18nTextEntry{.key = "renderGraph.pass", .enUs = "Pass", .zhHans = "Pass"},
            EditorI18nTextEntry{.key = "renderGraph.slot", .enUs = "Slot", .zhHans = "槽位"},
            EditorI18nTextEntry{.key = "renderGraph.use", .enUs = "Use", .zhHans = "用途"},
            EditorI18nTextEntry{
                .key = "renderGraph.direction", .enUs = "Direction", .zhHans = "方向"},
            EditorI18nTextEntry{
                .key = "renderGraph.resourceList", .enUs = "Resource List", .zhHans = "资源列表"},
            EditorI18nTextEntry{.key = "renderGraph.name", .enUs = "Name", .zhHans = "名称"},
            EditorI18nTextEntry{.key = "renderGraph.type", .enUs = "Type", .zhHans = "类型"},
            EditorI18nTextEntry{.key = "renderGraph.shape", .enUs = "Shape", .zhHans = "形状"},
            EditorI18nTextEntry{.key = "renderGraph.lifetimeState",
                                .enUs = "Lifetime / State",
                                .zhHans = "生命周期 / 状态"},
            EditorI18nTextEntry{
                .key = "renderGraph.passList", .enUs = "Pass List", .zhHans = "Pass 列表"},
            EditorI18nTextEntry{
                .key = "renderGraph.commands", .enUs = "Commands", .zhHans = "命令"},
            EditorI18nTextEntry{
                .key = "renderGraph.cullable", .enUs = "Cullable", .zhHans = "可剔除"},
            EditorI18nTextEntry{.key = "renderGraph.noDependencies",
                                .enUs = "No dependency edges.",
                                .zhHans = "没有依赖边。"},
            EditorI18nTextEntry{.key = "renderGraph.from", .enUs = "From", .zhHans = "从"},
            EditorI18nTextEntry{.key = "renderGraph.to", .enUs = "To", .zhHans = "到"},
            EditorI18nTextEntry{.key = "renderGraph.reason", .enUs = "Reason", .zhHans = "原因"},
        };

        [[nodiscard]] const EditorI18nTextEntry* findEntry(std::string_view key) {
            const auto found = std::ranges::find_if(
                kCatalog, [key](const EditorI18nTextEntry& entry) { return entry.key == key; });
            return found == kCatalog.end() ? nullptr : &(*found);
        }

    } // namespace

    std::string_view editorLocaleName(EditorLocale locale) {
        switch (locale) {
        case EditorLocale::ZhHans:
            return "zh-Hans";
        case EditorLocale::EnUs:
        default:
            return "en-US";
        }
    }

    std::optional<EditorLocale> editorLocaleFromName(std::string_view name) {
        if (name == "en" || name == "en-US" || name == "en_US") {
            return EditorLocale::EnUs;
        }
        if (name == "zh" || name == "zh-Hans" || name == "zh-CN" || name == "zh_CN") {
            return EditorLocale::ZhHans;
        }
        return std::nullopt;
    }

    std::span<const EditorI18nTextEntry> editorI18nCatalog() {
        return kCatalog;
    }

    EditorI18n::EditorI18n(EditorLocale locale) : locale_(locale) {}

    void EditorI18n::setLocale(EditorLocale locale) {
        locale_ = locale;
    }

    EditorLocale EditorI18n::locale() const {
        return locale_;
    }

    std::string_view EditorI18n::text(std::string_view key) const {
        return text(EditorI18nTextQuery{.key = key, .fallback = {}});
    }

    std::string_view EditorI18n::text(const EditorI18nTextQuery& query) const {
        if (query.key.empty()) {
            return query.fallback;
        }

        const EditorI18nTextEntry* entry = findEntry(query.key);
        if (entry == nullptr) {
            return query.fallback.empty() ? query.key : query.fallback;
        }

        if (locale_ == EditorLocale::ZhHans && !entry->zhHans.empty()) {
            return entry->zhHans;
        }
        if (!entry->enUs.empty()) {
            return entry->enUs;
        }
        return query.fallback.empty() ? query.key : query.fallback;
    }

    std::string EditorI18n::label(const EditorI18nLabelDesc& desc) const {
        const std::string_view visible =
            text(EditorI18nTextQuery{.key = desc.key, .fallback = desc.fallback});
        std::string label;
        label.reserve(visible.size() + desc.stableId.size() + 3U);
        label += visible;
        label += "###";
        label += desc.stableId;
        return label;
    }

} // namespace asharia::editor
