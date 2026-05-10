# Asharia 命名约定

决策日期：2026-05-10

本文记录引擎品牌、持久化 schema、文件后缀和代码命名的过渡约定。当前仓库目录和已有 C++ API 仍保留
`VkEngine` / `vke`，避免在 Reflection/Serialization 起步前进行大规模 rename；新设计文档和持久化数据约定
从现在开始使用 Asharia。

## 品牌命名

| 项 | 名称 |
| --- | --- |
| 英文名 | Asharia Engine |
| 中文名 | 灰咏引擎 |
| 编辑器 | Asharia Editor |
| 稳定 schema 前缀 | `com.asharia` |

`Asharia` 来自 `Ash + Aria`，保留“灰之魔女”的灰色意象，并用 `Aria` 表达咏唱、魔法和幻想感。
这里的 `Ash` 表示银灰、魔女斗篷和旅途尘色，不强调灰烬毁灭感。

## 持久化命名

持久化数据使用 Asharia 前缀。它比 C++ namespace 更稳定，后续即使内部代码重命名，用户工程文件也不应随意改变。

示例：

```text
com.asharia.scene.TransformComponent
com.asharia.asset.MaterialAsset
com.asharia.render.BasicDrawItem
com.asharia.editor.EditorCameraSettings
```

字段稳定名：

```text
com.asharia.scene.TransformComponent.position
com.asharia.scene.TransformComponent.rotation
com.asharia.scene.TransformComponent.scale
```

文件 schema：

```json
{
  "schema": "com.asharia.scene",
  "schemaVersion": 1
}
```

## 文件后缀

文件后缀使用短前缀 `a`，不把完整 `ash` 塞进后缀。完整品牌名写在 schema 中。

| 文件类型 | 后缀 |
| --- | --- |
| Scene | `.ascene` |
| Prefab | `.aprefab` |
| Asset metadata | `.ameta` |
| Material | `.amat` |
| RenderGraph / tool graph | `.agraph` |
| World | `.aworld` |

后缀职责是表达文件类型并减少冲突；品牌识别由 `Asharia Engine`、`Asharia Editor` 和 `com.asharia.*`
schema 承担。

## 当前代码过渡规则

当前代码仍处于 `VkEngine` / `vke` 命名阶段：

```cpp
namespace vke {
template <typename T>
using Result = std::expected<T, Error>;
}
```

Reflection/Serialization 第一版实现时，建议暂时沿用现有工程命名：

```text
packages/reflection
packages/serialization
vke-reflection
vke-serialization
vke::reflection
vke::serialization
```

原因：

- 当前 `engine/core` 已提供 `vke::Result`、`vke::Error` 和日志基础。
- 局部改成 `asharia::reflection` 会造成 namespace 混用。
- 全仓库 rename 应作为独立工程任务处理，包含目录、CMake target、alias、namespace、文档和文件头。

因此第一版规则是：

```text
C++ / CMake implementation name: 暂时使用 vke
Persistent schema / user project data: 使用 com.asharia
User-facing brand: Asharia Engine / 灰咏引擎
```

## 全仓库 Rename 前置条件

后续真正把 `vke` 改成 `asharia` 前，至少需要单独计划：

- C++ namespace 迁移：`vke` -> `asharia`。
- CMake target 迁移：`vke-reflection` -> `asharia-reflection`，alias `vke::reflection` -> `asharia::reflection`。
- include path 迁移：`include/vke/...` -> `include/asharia/...`。
- package manifest 和文档同步。
- smoke 和 clangcl/msvc 双构建验证。
- 对旧用户数据保持 `com.asharia.*` 不变，不做二次迁移。
