# VkEngine 知识库

这里是 VkEngine 的项目知识入口。根目录 `README.md` 面向快速上手；本目录记录技术依据、架构决策、构建流程、审查门禁和后续演进计划。

## 快速阅读路径

新开发者建议按下面顺序阅读：

1. `build-workflow.md` - 本地 bootstrap、configure、build 和 preset 使用方式。
2. `technical-stack.md` - 平台、依赖、Vulkan、shader、CMake/Conan 策略。
3. `architecture.md` - 模块边界、所有权、生命周期和 RenderGraph 设计。
4. `flow-architecture.md` - 当前包依赖、启动流程、frame loop、RenderGraph/RHI 数据流。
5. `next-development-plan.md` - 下一阶段 shader reflection、descriptor、transient/depth 和 mesh 路线。
6. `review-workflow.md` - 每次审查、修复和提交前必须执行的门禁。
7. `coding-standard.md` - C++23、Vulkan、shader、同步和工程风格规范。

## 知识分类

| 分类 | 文档 | 用途 |
| --- | --- | --- |
| 上手与构建 | `build-workflow.md` | 解释 Conan 生成物、CMake Presets、Visual Studio/Ninja 构建入口。 |
| 技术依据 | `research-sources.md`、`technical-stack.md` | 记录一手资料、版本依据、依赖和工具链决策。 |
| 架构设计 | `architecture.md`、`flow-architecture.md`、`package-architecture.md` | 记录 package-first 组织、对象生命周期、流程图和依赖方向。 |
| RenderGraph | `rendergraph-mvp.md`、`rendergraph-rhi-boundary.md` | 记录 MVP 闭环、API 草图、后端无关边界和 Vulkan 翻译归属。 |
| 工程规范 | `coding-standard.md`、`encoding-policy.md`、`review-workflow.md` | 记录编码规范、文本编码策略、审查和提交门禁。 |
| 项目节奏 | `project-management.md`、`next-development-plan.md` | 记录仓库布局、里程碑、Definition of Done、风险表和下一阶段开发顺序。 |

## 当前门禁状态

- Scope：Windows 桌面端，Vulkan 1.4，C++23，GLFW，VMA，CMake，Conan，MSVC/ClangCL。
- Research：已有官方资料索引；涉及最新 SDK、扩展、工具行为时仍需重新核对一手资料。
- Design：已形成 package-first、RenderGraph/RHI 边界、frame loop 和 shader pipeline 初版设计。
- Implementation：已接入窗口、Vulkan context、swapchain frame loop、RenderGraph clear、dynamic rendering clear、resize/recreate smoke、triangle smoke 和无参数交互式 triangle viewer。
- Validation：提交前按 `review-workflow.md` 运行编码检查、构建、smoke 和 Vulkan/C++ 审查脚本。

## 文档维护规则

- 根目录 `README.md` 只保留项目简介、快速开始、常用命令和文档入口。
- 本目录文档承载详细设计、决策依据和门禁规则。
- 修改包依赖、CMake target、smoke 命令、RenderGraph 语义、frame loop、shader pipeline 或 Vulkan 生命周期时，必须同步相关文档。
- 文档中的“当前状态”应描述仓库真实状态；未来计划需要明确写成“后续”或“计划”。
- Markdown 文件按 `encoding-policy.md` 使用 UTF-8 without BOM。

## 近期重点

- 优先实现 Slang reflection 基线，生成可审查的 `*.reflection.json`。
- 在 reflection 基线上建立 descriptor/layout 契约。
- 接入 RenderGraph transient image 和 depth attachment smoke。
- 维护 `conan.lock`，依赖改动时重新生成并审查 recipe revision 变化。
