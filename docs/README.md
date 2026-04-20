# VkEngine 开发准备包

这里是 VkEngine 第一个里程碑的开发前审核材料。当前阶段只做 Scope、Research、
Design，不直接写运行时代码，目标是先把技术路线、编码规范、项目组织和 render graph
最小闭环说清楚。

## 建议审核顺序

1. `research-sources.md` - 一手资料、版本依据和高风险技术点。
2. `technical-stack.md` - 工具链、依赖、CMake/Conan/MSVC/Vulkan 策略。
3. `build-workflow.md` - Conan 生成 preset、CMake 继承 preset 的本地构建流程。
4. `architecture.md` - 模块边界、对象所有权、生命周期和 render graph 设计。
5. `rendergraph-mvp.md` - 首个 render graph 闭环的最小里程碑。
6. `coding-standard.md` - C++23、Vulkan、shader、同步和工程风格规范。
7. `package-architecture.md` - 类 Unity package 的模块化文件与包管理设计。
8. `encoding-policy.md` - UTF-8 BOM 使用边界、检查脚本和工具兼容性说明。
9. `project-management.md` - 仓库布局、任务拆分、门禁和风险表。

## 当前门禁状态

- Scope：Windows 桌面端，Vulkan 1.4，C++23，GLFW，VMA，CMake，Conan，MSVC。
- Research：已在 2026-04-19 核对官方公开资料。
- Design：已准备初版设计供你审核。
- Implementation：尚未开始。

## 需要你重点确认

shader 语言已更正为 Slang。Slang 官方文档支持 SPIR-V/Vulkan 目标，因此它可以作为
VkEngine 的默认 shader 路线。实现前需要固定 Slang compiler 版本，并把 shader 编译、
SPIR-V validation 和 Vulkan feature/capability 对齐纳入构建流程。

文件组织方向已调整为 package-first：引擎核心保持小而稳定，render graph、renderer、
editor 等能力通过 `packages/` 目录独立组合，最终应用和编辑器只是不同 host。
