# 项目管理

## GitHub Project 长期管理流程

GitHub Project `Engine` 是长期路线图和当前执行状态的管理入口：
<https://github.com/users/66six11/projects/2>。

本地文档记录流程规则；GitHub Project README、Issues、PR body 和 Project 字段记录当前事实。AI 或维护者修改 GitHub 元数据时必须按下面顺序操作。

### 核心原则

- 记录当前事实，管理未来工作；不要为了补一个理想历史而倒拆已经存在或已经合并的长分支。
- 任务载体统一使用 GitHub Issues；Project 字段只记录状态、优先级和规模，不作为第二套任务数据库。
- Epic 表示长期阶段，标题格式为 `[Epic] 子系统: 阶段目标`。
- Slice 表示 PR 粒度工作，标题格式为 `[Slice] 子系统: PR 切片目标`。
- 已存在长分支需要合并时，用 integration Slice 记录范围、风险、验证和合并结果；合并后的剩余缺口再从当前 `main` 创建新的 Slice。
- PR 只有完整满足 Issue 验收标准时才使用 `Closes #N`；部分覆盖、外部阻塞或等待验证时只使用 `Refs #N`。

### AI 操作顺序

1. 先审计当前状态：`gh project item-list`、open issues、open PRs、labels、相关 PR body 和 Project 字段。
2. 先查重，再创建或更新 Issue、label、Project item；不要重复建 Epic/Slice。
3. 确认每个 Project item 都有 `Status`、`Priority`、`Size`。
4. 对 open PR 检查是否有关联 Issue；没有时创建或复用 Slice 并加入 Project。
5. 对 blocked PR 使用 draft + `Refs #N`，并在 Issue/PR 写清 blocker、下一步和刷新验证要求。
6. 对可关闭 PR 使用 `Closes #N` 前，确认验收标准、验证门禁、文档更新和 Done evidence 都齐全。
7. 完成后再跑一次 Project audit，并把同步记录写到 #20 `[Epic] Workflow: roadmap, docs, and Project sync`。

### Project 字段

Status 只使用三态：

- `Todo`：已进入路线图，但尚未开始执行。
- `In progress`：当前正在实现、验证、合并、审查或维护。
- `Done`：实现、文档、验证和审查结论已闭环，且留下完成证据。

Priority：

- `P0`：阻塞当前主线，或必须立即收敛的工程风险。
- `P1`：下一阶段必须推进的路线图任务。
- `P2`：可排期、可延后，或依赖前置系统的任务。

Size：

- `XS/S`：文档、门禁、局部验证或小型维护。
- `M`：单一子系统切片。
- `L`：跨 editor / rendergraph / renderer 的阶段切片。
- `XL`：长期大型能力，例如 asset / material / play session。

`Estimate`、`Iteration`、`Start date`、`Target date` 暂不填写，直到有真实排期。

### Issue 正文要求

Epic 和 Slice 正文必须包含：

- 目标
- 范围
- 不做事项
- 验收标准
- 验证门禁
- 来源文档或当前证据
- 状态更新约定
- Done 前完成证据

labels 使用现有 taxonomy：

- 类型：`type:epic`、`type:slice`、`type:architecture`、`type:validation`
- 区域：`area:editor`、`area:rendergraph`、`area:renderer`、`area:rhi-vulkan`、`area:assets`、`area:workflow`
- 门禁/风险：`gate:smoke`、`gate:docs`、`risk:vulkan-sync`

### Sub-issues 和 dependencies

- Sub-issues 只用于稳定父子分解，例如 integration / coordination Slice 下已经确定的验证 blocker 或 PR-sized child Slice。
- 不为了“看起来完整”把所有 Epic 都强行挂 sub-issues；未来阶段尚不稳定时，用正文或评论记录关系即可。
- Issue dependencies 只用于真实执行 blocker，例如验证、工具链、合并冲突、外部 counterpart 或前置 contract。
- blocker 解决后必须同步更新正文、评论和原生依赖，避免 Project、Issue body 和 dependency 元数据漂移。

### 状态更新和 Done evidence

进入 `In progress` 的 Issue 按需用 comment 更新：

- `TL;DR:` 当前状态一句话。
- `Since last update:` 已完成的 PR、决定或验证。
- `Next:` 下一步 PR-sized Slice 或具体动作。
- `Blocked:` blocking issue / 外部依赖，或 `None`。

Issue 移到 `Done` 前必须留下：

- closing PR / commit 链接。
- 实际运行过的验证门禁。
- 文档已更新，或无需更新的原因。
- follow-up / blocking issue 链接；不要通过扩大原 Issue scope 来收尾。

### 当前基线

- Shell / viewport 长分支已通过 #31 / PR #43 合入 `main`；#31 是该集成的 canonical record。
- 旧的 retro-split / isolate 任务不再作为未来执行入口。
- #20 是长期 Project / roadmap / workflow 同步入口。
- 后续新增开发从当前 `main` baseline 出发，按真实剩余缺口创建 PR-sized Slice。
- 如果 PR 依赖外部仓库或 counterpart 未就绪，保持 draft、使用 `Refs #N`，并在 Issue/PR 写清 blocker 和刷新验证要求。

## 仓库布局

当前仓库布局：

```text
AshariaEngine/
  CMakeLists.txt
  CMakePresets.json
  conanfile.py
  profiles/
    windows-msvc-debug
    windows-msvc-release
    windows-clangcl-debug
    windows-clangcl-release
  docs/
    architecture/
    planning/
    rendergraph/
    research/
    standards/
    systems/
    workflow/
  apps/
    editor/
    sample-viewer/
  engine/
    core/
    platform/
  packages/
    archive/
    asset-core/
    asset-pipeline/
    cpp-binding/
    persistence/
    profiling/
    project-core/
    reflection/
    rendergraph/
    renderer-basic/
    rhi-vulkan/
    schema/
    serialization/
    shader-slang/
    window-glfw/
  scripts/
  tools/
    check-doc-sync.ps1
    check-text-encoding.ps1
    count-code-lines.ps1
```

后续可能新增 `tests/`、`package-registry/`、`packages/scene-core/`、`packages/editor-core/`、
`packages/input/` 等目录，但新增前应先明确 package 边界、CMake target 关系和文档归属。

## 变更策略

- 每个变更绑定一个里程碑。
- 保持小闭环和强门禁，避免在当前阶段一次性扩成完整 editor/runtime/asset 系统。
- 优先维护 package 边界，避免把 runtime、editor、renderer 全塞进一个 app。
- 构建文件、依赖文件和 renderer 代码如果互相影响，应一起审核。
- 构建目录、Conan 输出目录、生成的 toolchain/preset 不提交。
- 仓库维护脚本位于 `tools/`；新增或修改工具时必须同步 `docs/workflow/build.md` 或相关 workflow 文档。

## 里程碑门禁

- Gate 0 Scope：目标平台、Vulkan 版本、依赖和 validation 方案明确。
- Gate 1 Research：版本敏感结论有一手资料支撑。
- Gate 2 Design：生命周期、同步、内存分配、错误路径明确。
- Gate 3 Implementation：改动窄，符合 C++23/Vulkan 规范。
- Gate 4 Validation：build、shader validation、Vulkan validation 已运行或明确阻塞原因。
- Gate 5 Review：记录 findings、风险和后续任务。

## 初始任务拆分

1. 创建 CMake、Conan、preset、dependency profile 骨架。[x]
2. 建立 `apps/`、`engine/`、`packages/` 的 package-first 目录和 CMake target 边界。[x]
3. 添加 `engine/core` 日志、错误、断言工具。[x]
4. 添加 `packages/window-glfw` 的 GLFW window 和 Vulkan surface。[x]
5. 添加 `packages/rhi-vulkan` 的 Vulkan instance/device/queue/allocator。[x]
6. 添加 swapchain 和 frame pacing。[x]
7. 添加 `packages/rendergraph` builder/compiler 骨架。[x]
8. 添加 clear pass。[x]
9. 添加 `packages/shader-slang` shader build 路径和 triangle pass。[x]
10. 添加基础 resize/recreate 路径和专项 smoke。[x]
11. 添加 validation checklist 和 smoke test 文档。[x]

## 第一版引擎 Definition Of Done

- 开发者能根据文档 configure、build、run，并能无参数启动交互式 sample viewer。
- 窗口能通过 render graph execution 持续呈现 triangle，并保留 clear/dynamic clear smoke。
- validation layer 在普通 startup-frame-shutdown 路径没有已知 error。
- resource lifetime 和 synchronization 决策在代码和日志中可见。
- shader 语言决策已确认，或已改为可工作的编译链路。

## 已完成阶段任务拆分

1. 增加 Slang reflection 基线，输出可审查的 `*.reflection.json`。[x]
2. 基于 reflection 建立 descriptor set/binding 和 pipeline layout 契约。[x]
3. 增加 RenderGraph transient image 声明、Vulkan binding 和 `--smoke-transient`。[x]
4. 增加 depth attachment 抽象状态、Vulkan 翻译和 `--smoke-depth-triangle`。[x]
5. 从固定顶点数据扩展到最小 mesh asset/index buffer 路线和 `--smoke-mesh`。[x]
6. 增加最小 3D mesh、depth 和 MVP push constants 路线，不提前引入全局相机系统。[x]

后续完整开发计划只维护在 `docs/planning/next-development-plan.md`，避免项目管理文档、RenderGraph 专项路线图和
性能文档各自维护一套阶段顺序。新增里程碑时先更新 `docs/planning/next-development-plan.md`，本文件只记录已经完成
或需要项目管理视角补充的门禁事项。

## 风险表

- Slang 工具链集成：Slang 支持 Vulkan SPIR-V，但 compiler 获取、版本 pin 和 Conan/CMake
  集成需要明确。
  缓解：shader metadata 记录 `slangc` 路径/版本，shader 输出统一走 `spirv-val`。
- Vulkan 1.4 可用性：本机驱动可能不暴露 Vulkan 1.4。
  缓解：请求 1.4，输出能力报告，再决定 fail fast 或支持 1.3 fallback。
- 同步复杂度：render graph barrier 很容易细节错误。
  缓解：首版单 queue、只用 synchronization2、开启详细 debug log 和 sync validation。
- Swapchain resize：自动 smoke 已覆盖 zero extent 和主动 recreate；真实交互 resize/minimize
  已在无参数 sample viewer 中手动验证通过。
  缓解：保留 `--smoke-resize` 作为回归门禁；后续若接入平台 resize 事件回调，再补充事件级验证记录。
- 依赖漂移：未审查的 Conan 依赖升级可能改变行为。
  缓解：提交 `conan.lock` 并让 bootstrap 自动使用；依赖改动时审查 lockfile diff。
- 包边界膨胀：为了快速跑通，代码容易滑向 monolithic app。
  缓解：从第一版 CMake target 开始按 `apps/engine/packages` 分层，app 只组合 package。
- Shader reflection 风险：Slang reflection 需要接入 API，而不是只靠 `slangc` 命令行。
  缓解：先生成 JSON 作为构建产物，不直接生成 C++ 代码；descriptor/layout 自动化放到下一步。
- Transient/depth 同步风险：新增 image 状态会扩大 layout、stage、access 组合。
  缓解：每新增一种 RenderGraph state 都同时增加 Vulkan adapter 单元验证和 smoke。
