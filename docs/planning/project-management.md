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
    material-core/
    persistence/
    profiling/
    project-core/
    reflection/
    rendergraph/
    renderer-basic/
    rhi-vulkan/
    scene-core/
    schema/
    serialization/
    shader-slang/
    window-glfw/
  scripts/
  tools/
    asset-processor/
    check-asset-boundaries.ps1
    check-doc-sync.ps1
    check-text-encoding.ps1
    count-code-lines.ps1
    pre-pr.ps1
```

后续可能新增 `tests/`、`package-registry/`、`packages/editor-core/`、`packages/input/` 等目录，
但新增前应先明确 package 边界、CMake target 关系和文档归属。

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

## 历史进度记录

本文件只维护 Project / Issue / PR 的操作规则，不再保存已完成任务拆分、阶段 DoD 或风险流水账。

- 当前路线和下一阶段顺序维护在 `docs/planning/next-development-plan.md`。
- 已完成阶段、历史风险和跨 PR 清理记录维护在 GitHub Issues / Project；#20 `[Epic] Workflow: roadmap, docs, and Project sync` 是长期同步入口。
- 需要保留的新进度先按本文件规则查重，再写入对应 Epic / Slice 的正文或评论。
