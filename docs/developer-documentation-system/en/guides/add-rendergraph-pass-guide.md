# Guide: Add A RenderGraph Pass

## Scope

Use this guide when a rendering feature needs to enter the RenderGraph compile/diagnostics/execution path. A direct Vulkan helper, one-off smoke draw, or pure UI operation does not require a new pass type.

## Steps

### 1. Choose Owner

| Pass type | Owner |
|---|---|
| backend-agnostic builtin renderer pass | `packages/renderer-basic/include/asharia/renderer_basic/` |
| Vulkan-only execution detail | `packages/renderer-basic/src/basic_renderers/` or `packages/rhi-vulkan/` |
| test-only schema | package test or sample smoke source |

### 2. Define Pass Type And Params Type

```cpp
inline constexpr char kExamplePassType[] = "builtin.example";
inline constexpr char kExampleParamsType[] = "builtin.example.params";

struct ExampleParams {
    std::uint32_t value{};
};
```

Params used with `PassBuilder::setParams()` must be trivially copyable.

### 3. Register Schema

```cpp
inline void registerExampleSchema(asharia::RenderGraphSchemaRegistry& schemas) {
    schemas.registerSchema(asharia::RenderGraphPassSchema{
        .type = kExamplePassType,
        .paramsType = kExampleParamsType,
        .resourceSlots =
            {
                asharia::RenderGraphResourceSlotSchema{
                    .name = "target",
                    .access = asharia::RenderGraphSlotAccess::ColorWrite,
                    .shaderStage = asharia::RenderGraphShaderStage::None,
                    .optional = false,
                },
            },
        .allowedCommands = {asharia::RenderGraphCommandKind::ClearColor},
    });
}
```

### 4. Record Graph Declaration

```cpp
graph.addPass("Example", kExamplePassType)
    .writeColor("target", target)
    .setParams(kExampleParamsType, ExampleParams{.value = 1})
    .recordCommands([](asharia::RenderGraphCommandList& commands) {
        commands.clearColor("target", {0.1F, 0.2F, 0.3F, 1.0F});
    });
```

### 5. Add Backend Execution

If the pass needs Vulkan work, add recorder/executor code in the backend owner. Do not add Vulkan types to `packages/rendergraph/include/`.

Backend code must validate:

- slot exists and has expected image/buffer resource,
- target format and extent are supported,
- params byte size matches expected struct,
- shaders/pipelines/descriptors are created or reused with explicit lifetime.

### 6. Add Smoke/Test Coverage

At minimum:

- positive compile path,
- invalid command kind rejected by schema,
- missing required slot rejected,
- backend smoke if Vulkan recording is added.

## Validation

RenderGraph package test:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages/rendergraph -B build/cmake/package-rendergraph-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build/cmake/package-rendergraph-tests-msvc-debug && ctest --test-dir build/cmake/package-rendergraph-tests-msvc-debug --output-on-failure"
```

Runtime smoke:

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-rendergraph
```

If Vulkan recording changed, also run the closest sample smoke such as:

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-triangle
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-compute-dispatch
```

Failure examples to test:

- schema says `ClearColor` only, command list records `DrawFullscreenTriangle`;
- pass declares `writeColor("target")` but schema requires `source` texture;
- params type string does not match schema params type.
