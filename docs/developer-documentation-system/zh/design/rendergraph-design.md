# 详细设计：RenderGraph 声明、编译与执行

## 背景

当前 renderer 需要在不泄露 Vulkan 细节的情况下声明 pass、资源和命令摘要，再由 backend-specific 层完成 barrier/layout/stage 映射和 command recording。`packages/rendergraph` 提供这个中间层。

## 目标

- 让调用方声明 images、buffers、passes、slots、params 和 command summaries。
- 在 compile 阶段验证 resource access 和 command 合法性；schema checks 只在 `compile(schemaRegistry)` 时执行。
- 生成 pass order、transitions、dependencies、culled passes 和 transient resource plan。
- 允许通过 callbacks 或 executor registry 执行。
- 保持 public API backend-agnostic。

## 非目标

- 不创建 Vulkan image/buffer。
- 不录制 Vulkan command buffer。
- 不管理 swapchain。
- 不决定 shader module、pipeline layout、descriptor set lifetime。
- 不把 editor UI state 放进 RenderGraph。

## 当前约束

- `RenderGraphImageState` 和 `RenderGraphBufferState` 是抽象状态，不是 Vulkan layout/access flags。
- `RenderGraphShaderStage` 当前只有 `None`、`Fragment`、`Compute`，描述资源访问所需 stage，不是完整 pipeline shader-stage 列表。
- pass params 必须 trivially copyable；`PassBuilder::setParams()` 复制为 byte vector。
- `RenderGraphSchemaRegistry` 决定每个 pass type 允许哪些 resource slots 和 command kinds。
- `hasSideEffects` 防止有副作用 pass 被错误 culling。

## 总体方案

调用方先构建 graph declaration。compile 阶段读取 declarations 和可选 schema registry：

1. 验证 resource descriptions。
2. 验证 pass slots 和 commands；schema validation 只在传入 registry 时执行。
3. 根据 resource read/write 建 dependency。
4. 对允许 culling 且无 reachable output/side effects 的 pass 做 culling。
5. 重排 transient producer 到 reader 前。
6. 为每个 compiled pass 计算 `transitionsBefore` 和 `bufferTransitionsBefore`。
7. 为 imported images/buffers 计算 `finalTransitions` 和 `finalBufferTransitions`。
8. 调用方可用 `diagnosticsSnapshot(compiled)` 和 `formatDebugTables(compiled)` 派生诊断视图。

执行阶段：

- `RenderGraphPassCallback`：pass 直接绑定 callback。
- `RenderGraphExecutorRegistry`：`execute(executorRegistry)` 先用 pass-bound callback，再按 pass type 查 executor。

## 模块划分

| 模块/文件 | 职责 |
|---|---|
| `render_graph.hpp` | aggregate include |
| `render_graph_builder.hpp` | `RenderGraph` 和 `PassBuilder` public declaration API |
| `render_graph_types.hpp` | handles、states、descs、slot schema、command data |
| `render_graph_command_list.hpp` | command summary builder |
| `render_graph_compile.hpp` | compile result public data |
| `render_graph_execution.hpp` | schema registry and executor registry |
| `render_graph_diagnostics.hpp` | diagnostics snapshot data |
| `src/render_graph_*validation*` | resource/pass/slot/command validation |
| `src/render_graph_dependency_*` | dependency collection、culling、sorting |
| `src/render_graph_debug_*` | debug names 和 table formatting |

## 数据结构

| 数据 | 关键字段 | 说明 |
|---|---|---|
| `RenderGraphImageDesc` | `name`、`format`、`extent`、`initialState`、`initialShaderStage`、`finalState`、`finalShaderStage`、`lifetime` | image declaration |
| `RenderGraphBufferDesc` | `name`、`byteSize`、`initialState`、`initialShaderStage`、`finalState`、`finalShaderStage`、`lifetime` | buffer declaration |
| `RenderGraphPassSchema` | `type`、`paramsType`、`resourceSlots`、`allowedCommands`、`allowCulling`、`hasSideEffects` | pass contract |
| `RenderGraphCommand` | `kind`、`name`、`secondaryName`、`floatValues`、`intValue`、`uintValues` | backend-readable command summary |
| `RenderGraphCompileResult` | passes、transitions、dependencies、culled passes、transient resources | compiled execution plan |

## API 设计

主要 public API 见 [../api/rendergraph-api.md](../api/rendergraph-api.md)。

示例：

```cpp
asharia::RenderGraph graph;
auto backbuffer = graph.importImage({
    .name = "Backbuffer",
    .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
    .extent = {.width = 1280, .height = 720},
    .initialState = asharia::RenderGraphImageState::Undefined,
    .finalState = asharia::RenderGraphImageState::Present,
});

graph.addPass("Clear", asharia::kBasicTransferClearPassType)
    .writeTransfer("target", backbuffer)
    .setParams(asharia::kBasicTransferClearParamsType,
               asharia::BasicTransferClearParams{.color = {0.0F, 0.0F, 0.0F, 1.0F}})
    .recordCommands([](asharia::RenderGraphCommandList& commands) {
        commands.clearColor("target", {0.0F, 0.0F, 0.0F, 1.0F});
    });

auto schemas = asharia::basicRenderGraphSchemaRegistry();
auto compiled = graph.compile(schemas);
```

## 关键流程

### 正常流程

1. 注册 schemas。
2. import external resources 并创建 transient resources。
3. 添加 pass declarations。
4. compile graph。
5. 需要时检查 diagnostics。
6. 通过 callbacks 或 backend executors 执行。

### 失败流程

- invalid resource desc：compile 返回 `Result<RenderGraphCompileResult>` error。
- missing schema：`compile(schemaRegistry)` 返回 error。
- slot access mismatch：`compile(schemaRegistry)` 返回 error。
- command kind 不被 schema 允许：`compile(schemaRegistry)` 返回 error。
- executor 在执行时缺失：execute 返回 `Result<void>` error。

### 边界流程

- transient resource producer 可以重排到 reader 前。
- 没有 output 但有 side effect 的 pass 在 `hasSideEffects` 为 true 时不能被 cull。
- optional slots 只有在 schema 标记 optional 时可以缺失。

## 生命周期

`RenderGraph` 通过内部 `Impl` 拥有 declarations。它可 copy 和 move。compile result 是返回给调用方的 value。Backend execution 不能以改变已返回 compile result 语义的方式修改 graph declarations。

## 错误处理

RenderGraph errors 使用 `asharia::Error` 和 `Result<T>`。RenderGraph errors 应使用 `ErrorDomain::RenderGraph`，并包含：

- 可用时包含 pass name 或 resource name。
- 相关时包含 schema type。
- validation 失败时包含 command kind 或 slot name。

## 测试方案

Core tests：

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages/rendergraph -B build/cmake/package-rendergraph-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build/cmake/package-rendergraph-tests-msvc-debug && ctest --test-dir build/cmake/package-rendergraph-tests-msvc-debug --output-on-failure"
```

Runtime smoke：

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-rendergraph
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --bench-rendergraph --warmup 10 --frames 100 --output build\rendergraph-bench.json
```

## 风险

- Schema names 是 strings，拼写错误会变成 missing schema 或 slot mismatch。Mitigation：使用 `kBasicTransferClearPassType` 等 constants。
- Params 是 raw bytes，`paramsType` 与 struct layout 不一致可能通过 compile 但被 backend 误解。Mitigation：在同一个 package 中配对 constants 和 typed structs。
- backend-specific behavior 可能向上泄漏，如果 Vulkan concepts 被加入 `render_graph_types.hpp`。Mitigation：只在 `rhi_vulkan_rendergraph` 下添加 mappings。
