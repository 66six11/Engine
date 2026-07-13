# 项目管理

## GitHub Project 长期管理流程

GitHub Project `Engine` 是长期路线图和当前执行状态的管理入口：
<https://github.com/users/66six11/projects/2>。

本地文档记录流程规则；GitHub Project README、Issues、PR body 和 Project 字段记录当前事实。AI 或维护者修改 GitHub 元数据时必须按下面顺序操作。

### 核心原则

- 记录当前事实，管理未来工作；不要为了补一个理想历史而倒拆已经存在或已经合并的长分支。
- 任务载体统一使用 GitHub Issues；Project 字段只记录状态、优先级和规模，不作为第二套任务数据库。
- Epic 表示一个有限、可验收、可关闭的系统能力里程碑，标题格式为 `[Epic] 系统: 里程碑结果`。完整 package 或永久维护域本身不是 Epic。
- Slice 表示单 PR 粒度的可验证结果，标题格式为 `[Slice] 系统[/组件]: PR 结果`。组件名只用于定位内部 module/target，不改变完整系统所有权。
- 路线图 Slice 只能有一个 primary parent Epic。跨系统影响在正文引用其他 Epic；真实执行前置使用 issue dependency，不建立多个模糊父级。
- 独立 bug、维护或一次性验证可以没有 parent Epic，但必须在“Primary Epic”中写明 `None` 和原因；不得为了满足格式把所有任务都挂到 #20。
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

- `XS/S`：文档、门禁、局部验证或小型维护，通常是 Slice。
- `M`：一个有明确边界的系统切片，或较小的 Epic。
- `L`：需要多个 Slice 才能完成的系统里程碑；单 PR Slice 只有在 integration 风险确实不可再拆时才能使用。
- `XL`：长期大型 Epic，例如完整 Content、Rendering programmable pipeline 或 Project Product 闭环；Slice 不得使用。

Epic 使用 `M/L/XL`；Slice 默认使用 `XS/S/M`。如果一个 Epic 只有 `XS/S`，通常应降为 Slice；如果一个 Slice 需要
`XL`，必须先拆分。Priority 不自动继承：Slice 默认继承 primary Epic 的 Priority，但真正阻塞主线的 Slice 可以提升，延期工作也可以降低。

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

目标 labels taxonomy 只在一个维度表达一种事实：

- 层级：`type:epic`、`type:slice`，二选一；
- 工作关注点：`concern:architecture`、`concern:validation`，可选；
- primary system area：每个 active Issue 恰好一个，例如 `area:foundation`、`area:platform`、`area:data`、
  `area:content`、`area:world`、`area:rendering`、`area:editor`、`area:product`、`area:workflow`；
- 内部组件：按需添加多个，例如 `component:rendergraph`、`component:renderer`、`component:rhi-vulkan`；
- 门禁/风险：`gate:smoke`、`gate:docs`、`risk:vulkan-sync`。

当前 `type:architecture`、`type:validation` 和 `area:rendergraph`、`area:renderer`、`area:rhi-vulkan`、`area:assets`
是迁移前 taxonomy。它们分别迁移为 `concern:*`、`area:rendering + component:*` 和 `area:content`。必须通过一次独立
Project maintenance pass 查重、批量迁移、审计并同步 #20；在该维护完成前不得创建同义 label 或让新旧标签长期并用。

不新增 Project `System` 或 `Area` 自定义字段。primary area 以 Issue label 为唯一事实源，Project 视图直接过滤 labels，
避免字段和 labels 漂移。

### Epic、Sub-issues 和 dependencies

- 使用两层执行结构：finite Epic → PR-sized Slice。当前不增加 Initiative，也不建立 package → Epic → stage → Slice 的深层镜像。
- 路线图 Slice 使用 GitHub 原生 Parent issue 指向唯一 primary Epic；Epic 的 Sub-issues progress 用于观察交付进度。
- 一个 Slice 触及多个系统时，以最终交付结果的 owner 作为 primary area/parent，其他 Epic 只在“Related Epics”中引用。
- Epic 完成不能只看 Sub-issues progress；还必须满足 Epic 自己的验收标准、验证门禁和 Done evidence。
- 如果一个所谓 Slice 需要多个 PR-sized child Slice，它本身已经是 Epic，应重新定级；integration Slice 仍必须能由一个 PR 闭环。
- 不为了“看起来完整”提前创建未来 Slice；尚不稳定的分解只写在 Epic 正文或 roadmap 文档中。
- Issue dependencies 只用于真实执行 blocker，例如验证、工具链、合并冲突、外部 counterpart 或前置 contract，不能代替普通先后顺序或相关链接。
- blocker 解决后必须同步更新正文、评论和原生依赖，避免 Project、Issue body 和 dependency 元数据漂移。

### Project 视图与 WIP 规则

Project 保留现有 `Status`、`Priority`、`Size` 字段，不新增与 labels 重复的系统字段。建议维护以下视图：

- `Roadmap`：只显示 `type:epic`，按 primary area 和 Priority 观察有限里程碑；
- `Delivery`：只显示 open `type:slice`，按 Status 分组，用于当前 PR 工作；
- `Blocked`：显示具有原生 blocked-by 关系的 open Issues；
- `Triage`：显示缺少层级 label、primary area、Priority、Size 或路线图 parent/standalone reason 的 active Issues；
- `Recently Done`：短期保留完成证据，之后自动 archive，避免长期看板堆积。

不设置脱离真实团队容量的硬 WIP 数字，但每个 `In progress` Epic 必须同时具备：至少一个当前 active Slice、一个明确
`Next`、以及最新 `Blocked` 状态。没有 active Slice 的 Epic 应回到 `Todo`，而不是长期占用 `In progress`。

尚未形成目标、范围、验收和验证门禁的 idea/feedback 不进入执行 Project。它先保留在 roadmap 文档或 GitHub Discussion；
当决定投入时，再转成 finite Epic 或 PR-sized Slice。已有未分类 Feedback Issues 只在专门维护 pass 中审计，不在本次文档调整中批量改写历史。

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
    material-instance/
    persistence/
    profiling/
    project-core/
    reflection/
    rendergraph/
    renderer-basic/
    resource-runtime/
    rhi-vulkan/
    scene-core/
    schema/
    serialization/
    shader-authoring/
    shader-material-adapter/
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

目标物理布局按安装语义增加稳定分类层：

```text
AshariaEngine/
  engine/
    core/
    platform/
    package-runtime/
    host-runtime/
  packages/
    systems/
      desktop-platform/
      memory/
      runtime-storage/
      settings/
      data-model/
      content/
      world/
      tasks/
      input/
      scripting-dotnet/
      rendering-vulkan/
      editor/
      project-product/
      observability/
    features/
    integrations/
    asset-packs/
    templates/
  package-registry/
  tests/
```

这是目标方向，不授权一次性搬迁当前目录。完整系统的 `asharia.package.json` 位于 `packages/<kind>/<package>/` 根，
内部 `modules/` 可以有多个 CMake targets；GitHub Epic/Slice 跟踪可验证交付，不按目录或 target 一一建任务。

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

## 进度记录归属

本文件只维护 Project / Issue / PR 的操作规则，不再保存已完成任务拆分、阶段 DoD 或风险流水账。

- 当前路线和下一阶段顺序维护在 `docs/planning/next-development-plan.md`。
- 已完成阶段、历史风险和跨 PR 清理记录维护在 GitHub Issues / Project；#20 `[Epic] Workflow: roadmap, docs, and Project sync` 是长期同步入口。
- 需要保留的新进度先按本文件规则查重，再写入对应 Epic / Slice 的正文或评论。
