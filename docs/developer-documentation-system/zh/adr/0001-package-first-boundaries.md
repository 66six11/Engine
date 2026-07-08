# ADR 0001：Package-first 边界

## 状态

Accepted.

## Context

当前代码以 CMake package 组织在 `engine/`、`packages/`、`apps/`、`tools/` 下。每个 reusable C++ unit 定义自己的 target 和 alias，例如 `asharia::rendergraph`、`asharia::rhi_vulkan`。

Engine 需要 standalone package builds、package-local tests，以及 RenderGraph、RHI、renderer、asset pipeline、editor hosts 之间清晰的依赖方向。

## Decision

采用 package-first boundaries：

- 每个 package 拥有自己的 `CMakeLists.txt`、public `include/`、private `src/`、可选 `tests/`、`asharia.package.json`。
- 跨 package 使用 CMake target 和 public headers。
- `src/` 目录私有。
- App targets 可以聚合 packages；reusable packages 不能依赖 apps。
- Backend-specific integration 使用单独 target，避免污染 backend-agnostic API。

## Alternatives

| Alternative | Rejected because |
|---|---|
| 一个 monolithic engine library | 会隐藏依赖方向，削弱 package-local validation |
| 所有模块 header-only | 会暴露实现细节并拖慢 rebuild |
| app-owned reusable modules | runtime packages 会依赖 editor/sample concerns |
| RenderGraph 直接 include Vulkan | backend-independent compile/diagnostics 不再成立 |

## Consequences

收益：package-local tests 可验证 API；public/private 边界在文件系统中可见；RHI/backend adapters 是显式 target。

成本：新增 package 需要 CMake 和 manifest boilerplate；移动代码要同步 target links 和 docs。

## 验证方式

```powershell
rg -n "#include .*src" engine packages apps tools
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```
