# Asharia Engine

Asharia Engine 是一个 package-first 的 C++23 Vulkan 引擎原型。仓库用 CMake target 和
`asharia.package.json` 明确 package 边界，并以 RenderGraph、Vulkan RHI、renderer、asset、
scene、editor 和 Studio host 的可验证小闭环逐步扩展。

## 快速开始

首次构建、清理 `build/` 后，或 Conan profile/依赖发生变化后，先生成 Conan toolchain：

```powershell
.\scripts\bootstrap-conan.ps1
```

日常 MSVC Debug 构建：

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

提交前 ClangCL Debug 构建：

```powershell
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
```

运行 sample viewer 或 native editor：

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe
build\cmake\msvc-debug\apps\editor\asharia-editor.exe
```

完整 smoke、test preset 和审查命令只维护在
[docs/workflow/review.md](docs/workflow/review.md)，构建故障与目录约定见
[docs/workflow/build.md](docs/workflow/build.md)。

## 仓库结构

```text
apps/       host applications：sample viewer、native editor、Studio
engine/     最小稳定基础：core、platform，以及规划中的 host/package runtime
packages/   可独立组合的 runtime、system、feature 和 integration targets
tools/      asset processor 与仓库维护工具
docs/       唯一工程文档源
cmake/      package-first CMake helpers
scripts/    bootstrap 与工作流脚本
profiles/   Conan profiles
```

## 文档入口

从 [docs/README.md](docs/README.md) 开始。主要事实来源：

- [docs/architecture/flow.md](docs/architecture/flow.md)：当前 target graph、启动和 frame flow。
- [docs/architecture/overview.md](docs/architecture/overview.md)：当前模块边界与所有权。
- [docs/architecture/package-first.md](docs/architecture/package-first.md)：package 与 CMake 规则。
- [docs/planning/system-architecture-roadmap.md](docs/planning/system-architecture-roadmap.md)：目标系统框架和迁移门禁。
- [docs/planning/project-management.md](docs/planning/project-management.md)：GitHub Epic、Slice、Project 和 Done evidence。

`docs/` 是唯一文档正文源。文档站只同步和发布这里的内容，不维护第二份翻译或改写后的工程事实。

## 维护约定

- C/C++ 源文件使用 UTF-8 with BOM；其他文本使用 UTF-8 without BOM。
- 修改 package 依赖、frame flow、RenderGraph/RHI 语义、smoke 或工具链时，同步对应事实文档。
- 当前进度、阻塞与完成证据维护在 GitHub Issues / Project，不写进长期文档。
- 构建目录、Conan 输出、生成的 toolchain/preset 和 shader products 不进入源码管理。
