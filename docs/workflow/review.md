# 审查流程规范

本文档定义每次代码审查、修复和提交前必须执行的门禁。目标是让代码正确性、Vulkan 同步安全、包边界、文档同步和下一步开发判断保持一致。

## 适用范围

- 用户要求“审查代码”“审查并提交”“再次审查”时，必须执行本文档。
- 用户给出 review findings 时，先判断每条 finding 是否仍适用，再修复。
- 涉及 Vulkan、RenderGraph、renderer、shader、构建脚本或包依赖的改动，必须增加设计审查门禁。

## 审查输出顺序

审查回复必须先列 findings，再列验证与总结。

若发现问题：

1. 按 P1/P2/P3 标注优先级。
2. 给出文件和行号。
3. 说明风险、触发条件和建议修法。
4. 若用户要求修复，则修复后重新跑完整门禁。

若无问题：

1. 明确写“未发现新的阻塞问题”。
2. 列出已跑命令和结果。
3. 若提交成功，说明 commit hash。

## 固定门禁

每次提交前必须执行：

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
python C:/Users/C66/.codex/skills/vulkan-cpp23-engineering/scripts/review_vulkan_cpp.py . --fail-on warning
```

涉及 frame loop、swapchain、RenderGraph、renderer 或 Vulkan adapter 时，必须跑：

```powershell
$smokes = @(
    "--smoke-window",
    "--smoke-vulkan",
    "--smoke-frame",
    "--smoke-rendergraph",
    "--smoke-transient",
    "--smoke-dynamic-rendering",
    "--smoke-resize",
    "--smoke-triangle",
    "--smoke-depth-triangle",
    "--smoke-mesh",
    "--smoke-mesh-3d",
    "--smoke-draw-list",
    "--smoke-descriptor-layout",
    "--smoke-fullscreen-texture",
    "--smoke-offscreen-viewport",
    "--smoke-deferred-deletion"
)

foreach ($preset in @("clangcl-debug", "msvc-debug")) {
    $exe = "build\cmake\$preset\apps\sample-viewer\asharia-sample-viewer.exe"
    foreach ($smoke in $smokes) {
        & $exe $smoke
        if ($LASTEXITCODE -ne 0) {
            throw "$preset $smoke failed with exit code $LASTEXITCODE"
        }
    }
}
```

如果某个 smoke 命令尚不存在，审查回复必须说明原因，不能默默跳过。

## 设计审查门禁

提交前必须结合相关资料做设计审查。优先资料：

1. Khronos Vulkan spec、refpage、Vulkan Guide。
2. VMA、Slang、SPIR-V、shader toolchain 官方文档。
3. 成熟案例：Frostbite FrameGraph、Granite、Diligent Engine、RenderDoc/Nsight 的资源视图思路。
4. 本仓库文档：`docs/architecture/flow.md`、`docs/rendergraph/rhi-boundary.md`、`docs/architecture/package-first.md`、`docs/planning/next-development-plan.md`。

设计审查必须覆盖：

- RenderGraph 是否保持后端无关。
- Vulkan layout、stage、access、barrier 是否只出现在 RHI 或 Vulkan backend。
- frame loop 是否只管理 acquire、submit、present、swapchain 生命周期，而不承载 renderer 策略。
- frame callback 是否声明 acquire semaphore 的正确 wait stage。
- `renderer-basic` 和 `renderer-basic-vulkan` 是否分层清楚。
- CMake target 依赖、package manifest 依赖和源码 include 是否一致。
- swapchain recreate、image view、semaphore、fence、command buffer 的生命周期是否闭合。
- 文档是否同步更新了真实流程。

审查回复中必须写：

```text
设计审查：通过
参考资料：...
```

若未通过，必须列出设计 finding，并优先修复 P1/P2。

## Vulkan 同步审查重点

- 使用 `vkQueueSubmit2` 时，`VkSemaphoreSubmitInfo::stageMask` 必须覆盖等待资源的首次实际使用阶段。
- transfer clear 路径可等待 `VK_PIPELINE_STAGE_2_TRANSFER_BIT`。
- dynamic rendering color attachment 路径应等待 `VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT`。
- triangle dynamic rendering 路径应等待 `VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT`，并用 dynamic viewport/scissor 覆盖当前 swapchain extent。
- 若 callback 无法精确声明阶段，短期 fallback 可使用 `VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT`，但应记录为待细化问题。
- layout transition 应由 RenderGraph 编译结果经 Vulkan adapter 生成，避免在业务层手写重复 barrier。

## 包边界审查重点

- `asharia::rhi_vulkan` 是基础 Vulkan 后端，不公开依赖 RenderGraph。
- `asharia::rhi_vulkan_rendergraph` 是 RenderGraph 到 Vulkan 的翻译 target。
- `asharia::renderer_basic` 保持后端无关，只描述 basic renderer graph 片段。
- `asharia::renderer_basic_vulkan` 负责 Vulkan 命令录制。
- app 可以承载 smoke 入口，但不应持有 pass/barrier/pipeline 编排细节。

## 文档同步要求

以下变化必须同步文档：

- 新增或修改 smoke 命令。
- 修改包依赖、target 依赖或 manifest。
- 修改 RenderGraph pass/resource/transition 语义。
- 修改 frame loop、swapchain、同步、资源生命周期。
- 新增 renderer backend 或 shader pipeline 阶段。

优先更新：

- `docs/architecture/flow.md`
- `docs/planning/next-development-plan.md`
- `docs/rendergraph/mvp.md`
- `docs/rendergraph/rhi-boundary.md`
- `docs/architecture/package-first.md`

## 提交规则

- 只暂存本次任务相关文件。
- 不提交用户已有的无关本地改动。
- 提交前再跑一次 `git status --short`。
- 提交回复必须包含 commit hash 和已通过门禁。

## 阶段完成 tag

当某个 `docs/planning/next-development-plan.md` 中定义的阶段已经完成实现、文档同步和对应验收门禁后，可以给完成该阶段的提交打 tag。

- tag 只打在“完成阶段验收”的实现提交上；如果后续只是补文档或流程，不移动已有阶段 tag。
- 命名使用 `stage-<number>-<short-slug>`，例如 `stage-14-render-view-target-recording`。
- 优先使用 annotated tag，消息格式为 `Stage <number>: <stage title>`。
- 打 tag 前必须确认该阶段的验收标准已在当前机器跑过，并在提交回复中列出关键门禁。
- 如果阶段仍有 P1/P2 finding、未解释的 validation error、失败 smoke 或未同步文档，不打 tag。
- 打 tag 后再跑一次 `git tag --list "stage-*"` 或 `git show <tag>` 核实 tag 指向正确提交。
