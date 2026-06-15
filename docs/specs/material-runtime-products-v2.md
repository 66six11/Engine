# Material Runtime Products V2

更新日期：2026-06-14

状态：计划中的 V2 runtime product 合同。V1 `.amat`、product manifest 和 binding layout 文档已弃用，不再新增
`amat-v1.md`、`material-product-v1.md` 或 `binding-layout-v1.md`。

本文定义 `.amat` 材质实例、binding layout、generated product manifest、diagnostics 和 renderer 消费边界。
`.ashader` / `.agraph` authoring contract 见 [`ashader-v2.md`](ashader-v2.md)。

## `.amat` V2

`.amat` 是材质实例，不是 shader 文件，也不是 GPU 状态文件。

`.amat` 保存：

```text
material type asset GUID
stable shader type id
property stable id
property value
texture/sampler/buffer asset handle
variant/static switch authoring 值
override diff
last cooked signature hash
schema version
```

`.amat` 不保存：

```text
Vulkan descriptor set
VkImageView
VkSampler
pipeline object
source absolute path
editor panel layout
preview camera
graph nodes
shader source copy
```

## `.amat` Schema

```json
{
  "schemaVersion": 2,
  "materialType": {
    "assetGuid": "asset-guid-of-unlit-ashader",
    "stableTypeId": "asharia.material.unlit",
    "expectedTypeHash": "..."
  },
  "variant": {
    "staticSwitches": {}
  },
  "properties": {
    "baseColor": {
      "propertyId": "baseColor",
      "type": "color",
      "value": [1.0, 0.0, 0.0, 1.0]
    },
    "albedoMap": {
      "propertyId": "albedoMap",
      "type": "texture2D",
      "assetGuid": "texture-guid"
    }
  },
  "import": {
    "lastCookedSignatureHash": "...",
    "lastCookedAt": "..."
  }
}
```

`.amat` resolver 必须能检查：

- material type asset GUID 是否存在。
- stable shader type id 是否匹配。
- property 是否存在。
- property type 是否匹配。
- texture/sampler/buffer asset handle 是否可解析。
- `expectedTypeHash` 或 `lastCookedSignatureHash` 是否 stale。

## Binding Layout V2

V2 冻结第一条 material binding policy：

```text
set 0: frame / view / scene globals
set 1: material
set 2: object / instance
set 3: reserved / future extension
```

material set 内部：

```text
binding 0: material parameter constant buffer
binding 1..N: textures
binding N+1..M: samplers
binding M+1..K: buffers
```

规则：

- 用户 shader 不手写 `[[vk::binding]]`。
- Tool 生成 binding 并验证 Slang reflection。
- 内置 engine shader 可以手写 binding，但必须有 tests 和 reflection contract 覆盖。
- Product manifest 必须记录 binding layout version。
- Pipeline key 输入必须显式包含 binding layout version、signature hash、render state 和 shader product identity。

## Product Outputs

每次 import/cook 生成：

```text
.asharia/cache/shaders/<name>.generated.slang
.asharia/cache/shaders/<name>.spv
.asharia/cache/shaders/<name>.reflection.json
.asharia/cache/shaders/<name>.signature.json
.asharia/cache/shaders/<name>.product.json
```

`.ashader` import 输出：

```text
generated.slang
spv
reflection.json
signature.json
product.json
diagnostics.json
```

当前 #158 的最小 `.ashader` product blob 已生成 `shader-authoring-product.v1` generated Slang
payload，并记录 source/importer/product key/hash、shader stable type id、property/pass summary facts、
generated binding facts 和 entry manifest facts。它不调用 `slangc`，不生成 SPIR-V、reflection JSON、
signature JSON 或 final shader product manifest，也不做 `.ashader` / `.slang` cross-asset dependency
invalidation。#163 是后续 compile/reflection product 层，目标是从该 generated Slang payload 和 entry
manifest facts 产出 deterministic SPIR-V/reflection facts，但仍不生成 material signature product 或 renderer
binding packet。

截至 2026-06-14，#163 已在 `asset-pipeline` execution request 中显式接收上游 product bytes
（relative product path + product hash + bytes），并在执行前校验路径、重复项和 hash 漂移；Slang
compile/reflection cook 消费该输入契约，而不是从 cache 路径或 `dependencyHash` 反推上游 product
内容。

同日的第二步新增 `com.asharia.importer.shader-compile-reflection` / `shader-compile-reflection-product.v1`：
compile request 通过 `shader.authoringProductPath` 指向上游 `shader-authoring-product.v1`，读取其 generated
Slang payload 和 entry manifest facts，逐 entry 调用 `slangc -reflection-json` 生成 SPIR-V 与 reflection
JSON，并用 `spirv-val` 验证 SPIR-V。产品 blob 记录 profile/target、上游 authoring product path/hash、
generated Slang hash、entry manifest、SPIR-V bytes/hash/size 和 reflection JSON/hash/size；reader 会复核
payload size/hash。该层仍不生成 material signature product、cross-asset dependency invalidation、renderer
binding packet 或 editor UI。

同日第三步把 compiler/validator diagnostics 纳入 `shader-compile-reflection-product.v1` entry facts：
每个 entry 记录 `slangc` exit code、diagnostic text size/hash/payload，以及 `spirv-val` exit code、
diagnostic text size/hash/payload；reader 会像校验 SPIR-V/reflection payload 一样复核 diagnostic payload
size/hash。Slang 官方命令行资料列出更结构化的诊断选项，但当前 Vulkan SDK 1.4.321.1 附带的
`slangc` 尚不支持这些 flags，因此当前 product cook 只依赖 stdout/stderr 捕获，并在写入诊断前归一化
CRLF 和临时 work path。坏 generated Slang source 会产生 deterministic `ShaderCompileReflectionImportFailed`
diagnostic，不写成功 product。

同日第四步补齐 compile/reflection determinism evidence：asset-pipeline smoke 现在对同一个 compile request
连续执行两次，并比较 product manifest text、compiled product hash、读回 payload、SPIR-V bytes/hash、
reflection JSON/hash 和 diagnostic facts。该证据覆盖 #163 的 deterministic compile/reflection product
验收点，但仍不把该 Slice 扩展到 material signature product 或 renderer/RHI 消费。

同日第五步补齐 manifest-selected entry 的 negative evidence：asset-pipeline smoke 会构造一个 hash
一致但 entry stage 不受支持的 `shader-authoring-product.v1` dependency bytes，并验证 compile/reflection
层在调用 shader tool 前产生 deterministic `ShaderCompileReflectionImportFailed` diagnostic 且不写 product
manifest。该证据限定在 generated product contract validation，不把 stage 纠正、material signature 或
renderer/RHI fallback 纳入 #163。

`.amat` import 输出：

```text
resolved material instance
validated property overrides
material-instance-product.v1 blob
diagnostics
```

当前 #156 的最小 product blob 只规范化 `.amat` material instance source bytes，并记录 material type
asset GUID、stable type id、expected type hash、last cooked signature hash、product key/hash 和 canonical
`.amat` payload。它不解析 `.ashader`，不生成 shader product，也不做 cross-asset dependency invalidation。

## Product Manifest Schema

```json
{
  "schemaVersion": 2,
  "bindingLayoutVersion": 2,
  "source": {
    "ashader": "Assets/Shaders/Unlit/Unlit.ashader",
    "slang": ["Assets/Shaders/Unlit/Unlit.slang"],
    "agraph": null
  },
  "hashes": {
    "ashader": "...",
    "slang": "...",
    "generatedSlang": "...",
    "reflection": "...",
    "signature": "...",
    "pipelineKeyInputs": "..."
  },
  "entries": [
    {
      "pass": "Forward",
      "tag": "SceneForward",
      "vertex": "vertexMain",
      "fragment": "fragmentMain"
    }
  ],
  "products": {
    "generatedSlang": "...",
    "spirv": "...",
    "reflection": "...",
    "signature": "..."
  },
  "diagnostics": [],
  "toolVersions": {
    "ashaderImporter": "2",
    "shaderSlang": "..."
  }
}
```

## Runtime Consumption

Runtime 只读 product，不读 authoring graph。

Editor 可以读：

```text
.ashader
.agraph
.amat
product diagnostics
preview product
```

Renderer 只读：

```text
SPIR-V
MaterialResourceSignature
pipeline key inputs
material binding packet
draw packet
```

`renderer_basic` 必须保持 backend-agnostic。Vulkan command recording 和 descriptor update 进入
`renderer_basic_vulkan` 或更窄的 Vulkan adapter target，不进入 `renderer_basic`。

## Diagnostics Model

```cpp
enum class DiagnosticSource {
    Ashader,
    Slang,
    Graph,
    Amat,
    Reflection,
    MaterialSignature,
    AssetPipeline,
    Renderer
};

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error
};

struct MaterialDiagnostic {
    DiagnosticSeverity severity;
    DiagnosticSource source;
    std::string code;
    std::string message;
    SourceSpan span;
    std::optional<std::string> targetPropertyId;
    std::optional<std::string> targetPassName;
    std::optional<GraphNodeId> targetNode;
    std::optional<GraphPinId> targetPin;
    std::vector<RelatedNote> notes;
};
```

Diagnostics 必须支持定位到：

```text
.ashader property
.ashader pass
raw Slang line
external .slang line
.amat property override
graph node
graph pin
reflection binding
renderer binding error
```

Preview 失败时保留上一次成功画面，diagnostics 附着到 node、pin、property 或 code line，并复用 RenderView kind
`Preview`。

## Import / Cook Rules

- 修改 `.ashader` 会重新 cook shader product。
- 修改 `.slang` 会重新 cook shader product。
- 修改 `.agraph` 会重新 lower 并 cook shader product。
- 修改 `.amat` 只重新 resolve material instance。
- 删除 texture asset 必须产生 diagnostics。
- property rename 必须产生 stale/incompatible diagnostics。
- Cache hit 必须稳定，不能依赖绝对 source path。

## Golden Tests

每个示例 shader 固定输出：

```text
generated.slang.golden
reflection.json.golden
signature.json.golden
product.json.golden
diagnostics.golden
pipeline-key.golden
```

示例集合：

```text
UnlitColor
UnlitTexture
AlphaBlend
DepthOnly
VertexColor
NormalMappedStub
ComputeMinimal
BrokenMissingEntry
BrokenPropertyTypeMismatch
BrokenBindingConflict
```
