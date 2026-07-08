# Standards：编码与架构约束

## Language And Build

- C++ code 使用 C++23。
- CMake minimum 是 3.28。
- Reusable C++ code 通过 package targets 和 `asharia::<name>` aliases 暴露。
- Conan 必须先于 CMake 运行，确保 presets 能找到 generated toolchains。

## Naming

| Item | Rule |
|---|---|
| Types | `PascalCase` |
| Functions | `camelBack` |
| Variables | `camelBack` |
| Constants | `kPascalCase` |
| Private members | trailing underscore，如 `device_` |

## Error Handling

- Failable APIs 返回 `Result<T>` 或 `VoidResult`。
- 不忽略 `VkResult`。
- subsystem failure 离开 subsystem 前转换为 `asharia::Error`，保留 domain、code、message。
- Constructors 保持轻量；Vulkan/OS/IO-failable work 放进 `create()` 或 explicit factory。

## Ownership

- 不使用 bare owning pointers。
- 不使用 bare `new` / `delete`。
- Vulkan GPU allocations 通过 VMA 或显式 Vulkan wrapper ownership。
- RAII wrappers 拥有 Vulkan handles，并按反向 lifetime 销毁。
- submitted frames 之后才能释放的资源使用 deferred deletion。

## RenderGraph And RHI

- `packages/rendergraph` 保持 backend-agnostic。
- Vulkan state mapping 属于 `asharia::rhi_vulkan_rendergraph`。
- Vulkan command recording 属于 backend implementation packages。
- `asharia::renderer_basic` 不要求 Vulkan command buffer types。
- `asharia::renderer_basic_vulkan` 可以依赖 Vulkan RHI 和 RenderGraph adapter。

## Include Order

1. `<vulkan/...>`
2. 其他 system/third-party `<...>`
3. `"asharia/..."`
4. package-local headers

## `vkDeviceWaitIdle`

不要在 render loops 中调用 `vkDeviceWaitIdle`。shutdown、early MVP simplify paths 或 debug probes 可以使用，但必须注释原因。

## 验证方式

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
```
