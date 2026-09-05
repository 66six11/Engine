# `.shader` / `.agraph` V2 Authoring Contract

更新日期：2026-09-06

状态：计划中的 V2 格式合同。V1 `.shader` / `.agraph` 规格已弃用，不再新增 `shader-v1.md`
或把 V1 schema 作为兼容目标。

本文定义材质类型 authoring contract 和后续 graph authoring IR。Runtime product、`.mat`、binding layout
和 diagnostics schema 见 [`material-runtime-products-v2.md`](material-runtime-products-v2.md)。

## 目标

V2 第一阶段只要求 code-first 材质类型可导入、可生成 Slang、可编译、可反射并能被 `.mat` 实例绑定。

`.shader` 负责：

- 声明 shader/material type 的稳定名称和 schema version。
- 声明 properties、pass、render state、entry point 和 code/graph 引用。
- 为 generated Slang、reflection、diagnostics 和 editor inspector 提供同一份 contract。

`.shader` 不负责：

- 保存材质实例值。
- 保存 Vulkan descriptor、pipeline object、GPU handle 或 runtime resource。
- 替代 Slang 作为 shader language。
- 保存 editor panel layout、preview camera 或 selection state。

## V2 `.shader` 范围

V2 MVP 支持：

```text
schema version
shader stable type id
properties
passes
render state
pass tag
external slang file reference
raw slang block
optional graph reference
diagnostics source spans
```

属性类型先限制为：

```text
float
float2
float3
float4
color
int
uint
bool
texture2D
sampler
```

pass V2 支持：

```text
name
tag
vertex entry
fragment entry
compute entry
cull
depthTest
depthWrite
blend
slang reference
graph reference
```

V2 不支持：

```text
include system
conditional DSL
cross-file inheritance
template/generic material
Slang AST rewrite
Slang -> Graph 反编译
复杂 variant matrix
bindless
runtime graph interpreter
```

## 示例

```text
schema 2

shader "asharia.material.unlit" {
  properties {
    color baseColor = [1, 1, 1, 1]
    texture2D albedoMap
  }

  pass "Forward" {
    tag "SceneForward"
    vertex vertexMain
    fragment fragmentMain
    cull back
    depthTest lessEqual
    depthWrite true
    slang "Unlit.slang"
  }
}
```

外部 Slang 文件写实际 GPU 逻辑：

```hlsl
float4 shadeMaterial() {
    return Material.baseColor;
}
```

导入时工具生成 prelude、binding、material parameter block 和 pass wrapper，再拼接用户代码。

## Raw Slang Block

`.shader` 可以包含 raw `slang { ... }` block，但 parser 不解析 Slang AST，只做：

```text
brace balancing
source span 记录
#line 映射
generated prelude 拼接
错误定位回原文件
```

示例：

```text
schema 2

shader "asharia.material.debug_color" {
  properties {
    color tint = [1, 0, 1, 1]
  }

  pass "Forward" {
    tag "SceneForward"
    vertex vertexMain
    fragment fragmentMain
  }

  slang {
    float4 shadeMaterial() {
      return Material.tint;
    }
  }
}
```

## Document Model

Parser 产出的 CPU document model 应保持接近 source contract：

```cpp
struct ShaderDocument {
    std::uint32_t schemaVersion;
    StableShaderTypeId typeId;
    std::string name;
    std::vector<ShaderPropertyDecl> properties;
    std::vector<ShaderPassDecl> passes;
    std::vector<SourceReference> slangFiles;
    std::optional<RawSlangBlock> rawSlang;
    std::optional<GraphReference> graph;
    SourceSpan fullSpan;
};
```

建议首个 V2 parser 使用 handwritten recursive descent。这个 parser 属于 `shader-authoring`，不进入
`material-core`，也不把 Slang compiler 依赖塞进 `material-core`。

## Generated Slang

Generated Slang 必须可读、可 golden-test，并使用 `#line` 把 diagnostics 映射回 `.shader` 或外部 `.slang`。

示例：

```hlsl
struct __AshariaMaterialParams {
    float4 baseColor;
    float roughness;
};

[[vk::binding(0, 1)]]
ConstantBuffer<__AshariaMaterialParams> __ashariaMaterial;

#define Material __ashariaMaterial

#line 22 "Assets/Shaders/Unlit/Unlit.shader"
float4 shadeMaterial() {
    return Material.baseColor;
}
```

用户侧不手写 `[[vk::binding]]`。工具负责生成和验证 binding；内置 engine shader 可以手写 binding，但必须由
tests 和 reflection contract 覆盖。

Current MVP entry strategy:

- Generated output records an entry manifest for each pass-declared `vertex`, `fragment`, and
  `compute` entry.
- `compileEntryName` is currently the source entry declared in `.shader`; Slang compile/reflection
  receives that entry name plus the explicit stage.
- Generated wrapper names are recorded as stable shim hooks, but they are not the final compile ABI
  until a later slice defines return/parameter adaptation for arbitrary user Slang entry shapes.

## `.agraph` V2

`.agraph` 是 graph authoring 数据，不是 runtime format。V2 MVP 不实现完整 Material Graph，只为后续保留
minimal IR：

```text
nodes
edges
pin values
layout
exposed properties
output binding to .shader pass/property contract
```

第一批 builtin node 建议限制为：

```text
Constant
Add
Multiply
Lerp
SampleTexture2D
Normalize
Dot
MakeFloat3
MakeFloat4
OutputBaseColor
```

`.agraph` lowering 输出普通 Slang function。Runtime 永远不读取 `.agraph`，renderer 只消费 generated product。

## Hybrid Slang Function Nodes

目标扩展为纯代码、混合、纯图三种平等创作方式与公共函数自动注册，详见
[`shader-material-authoring.md`](../systems/shader-material-authoring.md#三种创作方式与公共函数自动注册已确认目标尚未实现)。
当前尚无函数发现或 graph lowering 实现。公共库的公开、可节点化函数应自动成为 typed node；
显示元数据可选，不要求为每个函数添加节点注册标记或重复声明端口。函数签名示意：

```hlsl
public float3 blendColors(float3 a, float3 b, float factor) {
    return lerp(a, b, saturate(factor));
}
```

以上是 Slang 函数形态，不是对当前 discovery API 的声明。模块发现范围、可见性提取、稳定函数身份与可选元数据
须经本地 Slang 工具链验证。Graph 调用 handwritten Slang function，不要求把任意 handwritten Slang 反编译回 graph。

## Diagnostics

`.shader` / `.agraph` diagnostics 必须至少能定位到：

```text
.shader property
.shader pass
raw Slang line
external .slang line
graph node
graph pin
reflection binding
```

V2 MVP 必须覆盖这些错误：

- schema version 缺失或不支持。
- 重复 property。
- 未知 property type。
- 非法默认值。
- 缺少 pass entry。
- `slang` 引用缺失。
- raw Slang block brace 不平衡。
- generated binding 与 reflection 不一致。

## 验证

- `ShaderLexerTests`
- `ShaderParserTests`
- `ShaderDiagnosticsTests`
- `GeneratedSlangTests`
- `BindingLayoutTests`
- generated `.slang` golden tests
- `.shader -> generated Slang -> SPIR-V -> reflection -> signature` integration test

## 源格式命名迁移（#434）

源文件后缀统一为 `.shader`，C++ `Shader*`、`parseShaderDocument` 与 `shader_*` 文件同步命名。
旧 `.ashader` 文件需要重命名；保留其 `.ameta` GUID 并更新外部路径引用，不生成新资产身份。
不保留旧后缀或旧符号别名。`shader-authoring-product.v2` 使用 `shader.schemaVersion` 字段；
authoring importer version 升为 2，旧产物必须重新生成，依赖的 compile/reflection product 随输入 hash 失效。
这是命名及产物字段迁移，不更改 source schema 2 或 Slang 语义；`.agraph` 不在本次变更范围。
