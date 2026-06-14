# Shader / Material Authoring V2 MVP Plan

更新日期：2026-06-14

状态：V2 近期执行计划。旧 V1 milestone、`*-v1.md` spec 拆分和 graph-first 主路径已弃用。

本文只放从设计冻结到 code-first preview 的近期路线。Minimal `.agraph` IR、Hybrid node discovery 和完整
Material Editor 是后续路线，不进入 MVP critical path。

## MVP 验收标准

给定：

```text
Assets/Shaders/Unlit/Unlit.ashader
Assets/Shaders/Unlit/Unlit.slang
Assets/Materials/Red.amat
```

系统能够：

```text
1. parse .ashader
2. read .amat
3. generate Slang prelude
4. compile to SPIR-V
5. validate SPIR-V
6. generate reflection
7. map reflection to MaterialResourceSignature
8. produce deterministic pipeline key
9. create material binding packet
10. render preview or smoke scene
11. report diagnostics with source location
```

MVP 不要求：

```text
Graph Editor
node preview
Hybrid node discovery
live autocomplete
complex variants
bindless
beautiful material UI
```

## Milestone 0：冻结 V2 设计合同

目标：把格式边界、依赖方向和非目标写清楚，并彻底弃用 V1 文档入口。

交付物：

```text
docs/systems/shader-material-authoring.md
docs/specs/ashader-v2.md
docs/specs/material-runtime-products-v2.md
docs/planning/shader-material-mvp-plan.md
```

验收：

```text
明确 .ashader / .agraph / .amat / product 的职责
明确第一阶段不做 graph editor
明确 runtime 不读 graph
明确 material-core 不依赖 Slang
明确 renderer 不读 authoring 格式
明确 V1 spec 文件不再作为入口
```

## Milestone 1：Reflection Adapter

目标：把 `shader-slang` reflection 映射成 `MaterialResourceSignature`。

Status: Done via #143 / PR #144. The remaining MVP path starts at Milestone 2.

交付物：

```text
packages/shader-material-adapter/
  ReflectionToMaterialSignature.hpp
  ReflectionToMaterialSignature.cpp
  ReflectionDiagnostics.hpp
```

输入：

```text
shader reflection JSON / reflection model
```

输出：

```text
MaterialResourceSignature
signature hash
compatibility diagnostics
```

验收：

```text
descriptor binding 映射正确
constant buffer 映射正确
texture/sampler/buffer 映射正确
stage visibility 映射正确
pipeline key hash 稳定
golden tests 通过
```

## Milestone 2：`.ashader` Parser + Document Model

目标：能读 `.ashader`，生成 document model，并产生基础 diagnostics。

Status: Done via #146 / PR #147. This milestone is intentionally CPU-only and stops before
generated Slang, `.amat`, asset cook, or editor UI.

交付物：

```text
packages/shader-authoring/
  AshaderLexer
  AshaderParser
  AshaderDocument
  AshaderDiagnostics
  AshaderFormatterBasic
```

V2 支持：

```text
schema 2
shader
properties
pass
render state
slang "file.slang"
slang { raw block }
graph "file.agraph" 只作为引用，不实现 graph lowering
```

验收：

```text
能解析 unlit.ashader
重复 property 报错
未知类型报错
非法默认值报错
缺少 pass entry 报错
raw slang block span 正确
#line 映射准备就绪
```

## Milestone 3：Generated Slang Skeleton

目标：根据 `.ashader` 生成可编译 Slang。

Status: Done through #152 / PR #153. #148 / PR #149 completed deterministic Slang skeleton text
and line mapping data. #150 / PR #151 validates the generated source through Slang compile,
`spirv-val`, reflection JSON, and reflection-to-material-signature smoke coverage. #152 records a
deterministic entry manifest for pass-declared source entries and generated wrapper hooks, while
still stopping before `.amat`, asset cook, product cache, renderer/RHI, or editor UI.

交付物：

```text
GeneratedSlangBuilder
MaterialParameterBlockGenerator
BindingLayoutGenerator
LineMappingTable
```

生成内容：

```text
material params struct
constant buffer binding
texture/sampler declarations
Material.* access shim
pass entry wrapper
entry manifest
#line mapping
user raw slang / external slang include
```

验收：

```text
generated .slang 可读
shader-slang 能编译
spirv-val 通过
reflection.json 生成
entry manifest 驱动 compile/reflection entry 选择
diagnostics 能回到 .ashader 行号
```

## Milestone 4：`.amat` Minimal IO

目标：材质实例能引用 `.ashader` 并覆盖参数。

Status: Done via #154 / PR #155. This slice added CPU-only `packages/material-instance` `.amat`
read/write and override validation against `.ashader` document facts, while still stopping before
asset import/cook, product cache, renderer/RHI, editor UI, or final material binding packets.

交付物：

```text
packages/material-instance/
  AmatDocument
  AmatReader
  AmatWriter
  AmatResolver
  MaterialOverrideSet
```

V2 参数类型：

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

验收：

```text
.amat 能读取
能解析 material type asset GUID
能检查 property 是否存在
能检查 property type 是否匹配
能检测 stale signature hash
能输出 override diff
```

## Milestone 5：Asset Pipeline Import / Cook

目标：`.ashader` 和 `.amat` 纳入 asset pipeline。

Status: In progress via #158. #156 completed the first slice by cooking `.amat` material instance
source bytes into deterministic `material-instance-product.v1` blobs through `asset-pipeline`.
#158 continues the same milestone by cooking `.ashader` source bytes into deterministic
`shader-authoring-product.v1` generated Slang blobs, reusing `shader-authoring` parser/builder
privately and stopping before `slangc`, SPIR-V, reflection/signature products, cross-asset
dependency invalidation, renderer/RHI, editor UI, or final binding packets.

交付物：

```text
AshaderImporter
AmatImporter
MaterialProductManifest
DependencyTracker
ShaderCookCache
MaterialDiagnosticsWriter
```

验收：

```text
修改 .ashader 会重新 cook shader product
修改 .slang 会重新 cook shader product
修改 .amat 只重新 resolve material instance
删除 texture asset 能产生 diagnostics
property rename 能产生 stale/incompatible diagnostics
cache 命中稳定
```

## Milestone 6：Renderer Binding 闭环

目标：renderer 能消费 material product 和 `.amat` 实例。

交付物：

```text
MaterialBindingPacket
MaterialParameterUploader
MaterialTextureBindingResolver
PipelineKeyBuilderIntegration
DrawPacketMaterialBinding
```

验收：

```text
用 .ashader + .amat 绘制一个 unlit triangle/mesh
修改 baseColor 后画面变化
修改 texture 后 binding 更新
pipeline key 稳定
signature mismatch 时不崩溃，有 diagnostics
```

## Milestone 7：Code-first Preview

目标：editor 里能预览 code-first 材质。

交付物：

```text
MaterialPreviewRequest
MaterialPreviewScene
MaterialPreviewRendererPath
PreviewDiagnosticsPanel
LastGoodPreviewCache
```

验收：

```text
打开 .ashader 能显示 preview
修改 property 能刷新 preview
shader 编译失败时保留上一帧成功画面
diagnostics 定位到 .ashader / .slang
不复制独立 renderer 路径
```

## 后续路线

MVP 之后按顺序推进：

```text
8. minimal .agraph IR + lowering
9. Hybrid Slang function node discovery
10. full Material Editor
```

Graph、Hybrid 和完整 Material Editor 必须复用同一套 `.ashader` contract、generated Slang、reflection adapter、
material product、renderer binding 和 preview diagnostics。

## 测试策略

单元测试：

```text
AshaderLexerTests
AshaderParserTests
AshaderDiagnosticsTests
GeneratedSlangTests
BindingLayoutTests
AmatReaderWriterTests
ReflectionAdapterTests
MaterialSignatureCompatibilityTests
PipelineKeyHashStabilityTests
```

集成测试：

```text
.ashader -> generated Slang -> SPIR-V -> reflection -> signature
.ashader + .amat -> material binding packet
.ashader 修改 -> product invalidation
.slang 修改 -> product invalidation
.amat 修改 -> instance update only
texture 删除 -> diagnostics
signature breaking change -> stale .amat warning/error
```

Editor smoke：

```text
打开 .ashader
打开 .amat
修改 baseColor
触发 preview
制造 shader error
diagnostics 定位
恢复 shader
preview 恢复
```

文档变更验证：

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```
