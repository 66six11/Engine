# Standards：文档写作

## 文档类型

| 类型 | 目录 | 必须回答 |
|---|---|---|
| 文档入口 | `README.md` | 项目是什么，文档怎么读 |
| 架构文档 | `architecture/` | 系统怎么分层，谁拥有状态，谁能依赖谁 |
| 详细设计 | `design/` | 功能怎么落地，模块、数据结构、流程、错误处理和测试 |
| API Reference | `api/` | 接口怎么调用，参数是什么，返回什么，失败时是什么 |
| Guide | `guides/` | 开发者怎么完成一个具体任务 |
| Workflow | `workflow/` | 构建、测试、发布、代码评审怎么执行 |
| ADR | `adr/` | 为什么选这个方案，拒绝了哪些方案 |
| Standards | `standards/` | 编码、命名、文本、文档规范 |

## 语言策略

- `zh/` 中正文使用简体中文。
- `en/` 中正文使用英文。
- 两个语言目录保持同构文件名。
- 代码标识符、路径、target names、API names、commands、固定产品名保持原样。

## 通用规则

- 先写当前事实，再写未来计划。
- 计划必须标 `planned`、`proposal` 或 `future`。
- 少写口号，多写约束。
- 每篇文档必须有验证方式。
- 引用真实文件、target、类名、函数名和命令。
- 不把旧文档当成事实来源；事实来源优先是代码、构建文件、public headers、tests 和 tools。
- 架构事实变化时，同步入口文档和相关 architecture/design/API/guide/workflow 文档。

## 详细设计模板

```markdown
# 详细设计：功能名称

## 背景
为什么要做。

## 目标
这次要达成什么。

## 非目标
这次明确不做什么。

## 当前约束
已有系统、性能、兼容性、平台、依赖限制。

## 总体方案
核心实现思路。

## 模块划分
新增/修改哪些模块、类、文件。

## 数据结构
关键结构体、字段、状态。

## API 设计
公开接口、参数、返回值、错误。

## 关键流程
正常流程、失败流程、边界流程。

## 生命周期
创建、更新、释放、异常清理。

## 错误处理
错误类型、恢复策略、日志。

## 测试方案
单元测试、集成测试、手动验证。

## 风险
可能出问题的地方和备选方案。
```

## API 文档模板

````markdown
# API Reference：模块名称

## 类型 / 类名

### `functionName(args)`

说明这个接口做什么。

参数：

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|

返回值：

| 类型 | 说明 |
|---|---|

错误：

| 错误 | 触发条件 |
|---|---|

示例：

```cpp
// example
```
````

## 验证方式

文档类 PR 至少运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1 -IncludeUntracked
```

人工检查清单：

- 文档点名具体文件或 target。
- 当前事实和 `future`、`proposal` 分开。
- 包含可运行命令、检查点、示例或失败场景。
- 成为稳定入口后，链接到相关索引。
