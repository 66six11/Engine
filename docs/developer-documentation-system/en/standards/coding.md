# Standards: Coding And Architecture Constraints

## Language And Build

- C++ code uses C++23.
- CMake minimum is 3.28.
- Reusable C++ code lives in package targets with `asharia::<name>` aliases.
- Conan must run before CMake so presets can find generated toolchains.

## Naming

| Item | Rule |
|---|---|
| Types | `PascalCase` |
| Functions | `camelBack` |
| Variables | `camelBack` |
| Constants | `kPascalCase` |
| Private members | trailing underscore, such as `device_` |

## Error Handling

- Failable APIs return `Result<T>` or `VoidResult`.
- Do not ignore `VkResult`.
- Convert subsystem failures to `asharia::Error` with domain, code, and message.
- Constructors should stay lightweight; Vulkan/OS/IO-failable work belongs in `create()` or explicit factory functions.

## Ownership

- No bare owning pointers.
- No bare `new` or `delete`.
- Vulkan GPU allocations go through VMA or explicit Vulkan wrapper ownership.
- RAII wrappers own Vulkan handles and destroy them in reverse lifetime order.
- Deferred GPU deletion must be used when resource lifetime crosses submitted frames.

## RenderGraph And RHI

- `packages/rendergraph` must stay backend-agnostic.
- Vulkan state mapping belongs to `asharia::rhi_vulkan_rendergraph`.
- Vulkan command recording belongs to backend implementation packages.
- `asharia::renderer_basic` must not require Vulkan command buffer types.
- `asharia::renderer_basic_vulkan` may depend on Vulkan RHI and RenderGraph adapter.

## Include Order

Use project include ordering:

1. `<vulkan/...>`
2. other system or third-party `<...>`
3. `"asharia/..."`
4. package-local headers

The formatter enforces much of this; still keep dependency direction readable.

## `vkDeviceWaitIdle`

Do not call `vkDeviceWaitIdle` in render loops. It is allowed in shutdown, early MVP simplify paths, or debug probes when commented with the reason.

## Validation

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
```

Review checks:

- New public headers do not include another package's `src/`.
- New Vulkan code checks every `VkResult`.
- New failable API exposes `Result<T>` or `VoidResult`.
- New render graph API does not expose Vulkan types.
