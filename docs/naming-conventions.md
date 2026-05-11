# Asharia 命名约定

决策日期：2026-05-10

本文记录引擎品牌、持久化 schema、文件后缀和代码命名约定。仓库已经完成代码侧 rename：
用户可见品牌使用 `Asharia Engine` / `灰咏引擎`，C++ namespace、CMake target、include path
和 package manifest 使用小写实现名前缀 `asharia`，持久化数据继续使用稳定前缀 `com.asharia`。

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

## 当前代码命名规则

当前代码使用小写 namespace：

```cpp
namespace asharia {
template <typename T>
using Result = std::expected<T, Error>;
}
```

Reflection/Serialization 等 package 使用同一套实现名前缀：

```text
packages/reflection
packages/serialization
asharia-reflection
asharia-serialization
asharia::reflection
asharia::serialization
```

规则：

- C++ namespace 使用 `asharia`。
- CMake target 使用 `asharia-<name>`，alias 使用 `asharia::<name>`。
- public include path 使用 `include/asharia/...`。
- package manifest 文件名使用 `asharia.package.json`。
- CMake options、cache variables 和 compile definitions 使用 `ASHARIA_*`。

当前规则是：

```text
C++ / CMake implementation name: asharia
Persistent schema / user project data: 使用 com.asharia
User-facing brand: Asharia Engine / 灰咏引擎
```

## 全仓库 Rename 完成状态

本次 rename 已覆盖：

- C++ namespace：`asharia`。
- CMake target：`asharia-reflection`、`asharia-rendergraph` 等；alias：`asharia::reflection`、`asharia::rendergraph` 等。
- include path：`include/asharia/...`。
- package manifest：`asharia.package.json` 和 `com.asharia.*` package names。
- 文档、README、构建选项、smoke 命令和资源文件同步。
- 对旧用户数据保持 `com.asharia.*` 不变，不做二次迁移。

仓库根目录名可以按本地工作区需要保留或改成 `AshariaEngine/`；构建脚本和文档命令不依赖固定绝对路径。
