# API Reference：RenderGraph

## `RenderGraph`

拥有 graph declaration，并把它编译成 execution plan。

### `importImage(RenderGraphImageDesc desc)`

注册 externally owned image。

参数：

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `desc` | `RenderGraphImageDesc` | 是 | name、format、extent、initial/final state、lifetime |

返回值：

| 类型 | 说明 |
|---|---|
| `RenderGraphImageHandle` | pass slot 使用的 image handle |

错误：

| 错误 | 触发条件 |
|---|---|
| 无直接返回错误 | Resource description 在 `compile()` 阶段验证 |

### `createTransientImage(RenderGraphImageDesc desc)`

注册 graph-owned transient image declaration。

返回的 handle 只对同一个 graph 的 declaration 有效。实际 allocation 由 backend 拥有。

### `importBuffer(RenderGraphBufferDesc desc)`

注册 externally owned buffer。

### `createTransientBuffer(RenderGraphBufferDesc desc)`

注册 graph-owned transient buffer declaration。

### `addPass(std::string name)`

创建没有 type 的 pass builder。需要 schema validation 时，应使用带 type 的 overload。

### `addPass(std::string name, std::string type)`

创建带 schema type 的 pass builder。

返回值：

| 类型 | 说明 |
|---|---|
| `RenderGraph::PassBuilder` | 用于 slots、params、commands、callbacks 的 fluent builder |

### `compile()`

不传显式 schema registry 的 compile。resource 和 dependency validation 仍会执行；missing schema、params type、schema slot、schema command checks 只适用于 `compile(schemaRegistry)`。

### `compile(const RenderGraphSchemaRegistry& schemaRegistry)`

根据 registered schemas 验证并编译。

返回值：

| 类型 | 说明 |
|---|---|
| `Result<RenderGraphCompileResult>` | 成功时包含 ordered passes、transitions、dependencies、culled passes、transient resources |

错误：

| 错误 | 触发条件 |
|---|---|
| `ErrorDomain::RenderGraph` | invalid resource、dependency/cycle failure，或使用 `compile(schemaRegistry)` 时的 schema failures |

### `execute()`

执行 pass 直接绑定的 callbacks。

### `execute(const RenderGraphExecutorRegistry& executorRegistry)`

先执行 pass-bound callbacks；没有 callback 时按 pass type 从 registry 查 executor。

### `diagnosticsSnapshot(const RenderGraphCompileResult& compiled)`

为 UI 或调试构建 diagnostics snapshot。

### `formatDebugTables(const RenderGraphCompileResult& compiled)`

格式化 debug tables，供 smoke output 或人工检查使用。

## `RenderGraph::PassBuilder`

### Resource Slots

| Function | Access |
|---|---|
| `writeColor(image)` | color attachment write |
| `readTexture(slotName, image, shaderStage)` | shader read |
| `readDepth(slotName, image)` | depth read |
| `writeDepth(slotName, image)` | depth write |
| `readDepthTexture(slotName, image, shaderStage)` | depth sampled read |
| `readTransfer(slotName, image)` | transfer read |
| `writeTransfer(slotName, image)` | transfer write |
| `readBuffer(slotName, buffer, shaderStage)` | buffer shader read |
| `readTransferBuffer(slotName, buffer)` | buffer transfer read |
| `writeBuffer(slotName, buffer)` | buffer transfer write |
| `readWriteStorageBuffer(slotName, buffer, shaderStage)` | storage read/write |

### Params And Commands

| Function | 说明 |
|---|---|
| `setParamsType(std::string)` | 设置 params type id |
| `setParamsData(std::vector<std::byte>)` | 设置 raw params bytes |
| `setParams<Params>(paramsType, params)` | 复制 trivially copyable params 到 bytes |
| `execute(RenderGraphPassCallback)` | 绑定 callback |
| `setCommands(RenderGraphCommandList)` | 绑定 command summary list |
| `recordCommands(recorder)` | 通过 lambda 构建 command list |

## `RenderGraphCommandList`

Command summary builder：

| Function | 说明 |
|---|---|
| `setShader(shaderAsset, shaderPass)` | 选择 shader identity |
| `setTexture(bindingName, slotName)` | 按名称绑定 texture slot |
| `setFloat(bindingName, value)` | 设置 float |
| `setInt(bindingName, value)` | 设置 int |
| `setVec4(bindingName, value)` | 设置 vec4 |
| `drawFullscreenTriangle()` | 请求 fullscreen triangle draw |
| `clearColor(slotName, color)` | clear image slot |
| `fillBuffer(slotName, value)` | fill buffer |
| `copyImage(sourceSlotName, targetSlotName)` | image copy |
| `copyBuffer(sourceSlotName, targetSlotName)` | buffer copy |
| `copyBufferToImage(sourceSlotName, targetSlotName)` | buffer to image copy |
| `copyImageToBuffer(sourceSlotName, targetSlotName)` | image to buffer copy |
| `dispatch(x, y, z)` | compute dispatch |

## `RenderGraphSchemaRegistry`

注册 pass schemas。schema 定义 pass type、params type、resource slots、allowed command kinds、culling 和 side effects。

## 示例

```cpp
auto schemas = asharia::basicRenderGraphSchemaRegistry();

asharia::RenderGraph graph;
auto target = graph.importImage({
    .name = "Target",
    .format = asharia::RenderGraphImageFormat::B8G8R8A8Srgb,
    .extent = {.width = 64, .height = 64},
    .initialState = asharia::RenderGraphImageState::Undefined,
    .finalState = asharia::RenderGraphImageState::Present,
});

graph.addPass("ClearTarget", asharia::kBasicTransferClearPassType)
    .writeTransfer("target", target)
    .recordCommands([](asharia::RenderGraphCommandList& commands) {
        commands.clearColor("target", {0.02F, 0.04F, 0.08F, 1.0F});
    });

auto compiled = graph.compile(schemas);
if (!compiled) {
    return compiled.error();
}
```

## 验证方式

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-rendergraph
```

预期 smoke 检查：

- invalid command kind 会被 schema validation 拒绝。
- transient producer 排在 reader 之前。
- diagnostics snapshot 包含 pass/resource/edge data。
- executor callbacks 会为 compiled passes 执行。
