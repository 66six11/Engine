# Guide：新增 RenderGraph Pass

## 适用范围

当渲染功能需要进入 RenderGraph compile/diagnostics/execution 路径时，使用本指南。直接 Vulkan helper、一次性 smoke 绘制、纯 UI 操作不需要新增 pass type。

## 步骤

### 1. 选择 Owner

| Pass 类型 | Owner |
|---|---|
| backend-agnostic builtin renderer pass | `packages/renderer-basic/include/asharia/renderer_basic/` |
| Vulkan-only execution detail | `packages/renderer-basic/src/basic_renderers/` 或 `packages/rhi-vulkan/` |
| test-only schema | package test 或 sample smoke source |

### 2. 定义 Pass Type 和 Params Type

```cpp
inline constexpr char kExamplePassType[] = "builtin.example";
inline constexpr char kExampleParamsType[] = "builtin.example.params";

struct ExampleParams {
    std::uint32_t value{};
};
```

`PassBuilder::setParams()` 使用的 params 必须 trivially copyable。

### 3. Register Schema

```cpp
inline void registerExampleSchema(asharia::RenderGraphSchemaRegistry& schemas) {
    schemas.registerSchema(asharia::RenderGraphPassSchema{
        .type = kExamplePassType,
        .paramsType = kExampleParamsType,
        .resourceSlots = {asharia::RenderGraphResourceSlotSchema{
            .name = "target",
            .access = asharia::RenderGraphSlotAccess::ColorWrite,
            .shaderStage = asharia::RenderGraphShaderStage::None,
            .optional = false,
        }},
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

如果 pass 需要 Vulkan work，backend owner 必须验证 slot、format/extent、params byte size、shader/pipeline/descriptor lifetime。不要把 Vulkan types 加到 `packages/rendergraph/include/`。

### 6. Add Smoke/Test Coverage

至少覆盖 positive compile、invalid command kind、missing required slot、backend smoke。

## 验证方式

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages/rendergraph -B build/cmake/package-rendergraph-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build/cmake/package-rendergraph-tests-msvc-debug && ctest --test-dir build/cmake/package-rendergraph-tests-msvc-debug --output-on-failure"
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-rendergraph
```
