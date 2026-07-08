# ADR 0002：独立可部署文档根目录

## 状态

Accepted.

## Context

仓库已有旧 `docs/` 历史资料。新的开发者技术文档体系需要清晰 taxonomy，并且不能通过复制旧文档作为事实来源。部署也需要稳定源目录，避免把 legacy planning 或历史记录直接作为新站点入口。

## Decision

新的可部署文档根目录是：

```text
docs/developer-documentation-system
```

该目录包含语言选择入口、`zh/` 中文版本和 `en/` 英文版本。docs-site sync workflow 发布这个目录，而不是整个 `docs/` tree。

Dispatch payload 包含：

```json
{
  "engine_docs_dir": "docs/developer-documentation-system"
}
```

## Alternatives

| Alternative | Rejected because |
|---|---|
| 原地替换所有旧 `docs/` | 风险大，且会丢失历史 planning material |
| 顶层新增 `developer-docs/` | 会绕过现有 `docs/` CODEOWNERS/doc sync 习惯 |
| 继续部署全部 `docs/` | 会把混合 taxonomy 和历史资料暴露为新公开入口 |
| 只在 external docs-site 维护内容 | 工程事实会离开代码仓库并漂移 |

## Consequences

收益：新文档有单一部署根；旧 docs 可后续显式迁移或删除；docs-site 可以同步可预测目录。

成本：docs-site 必须使用 `engine_docs_dir`；旧 docs 到新 docs 的链接不会自动迁移。

## 验证方式

```powershell
Get-Content -Raw .github\workflows\docs-site-sync.yml
Test-Path docs\developer-documentation-system\README.md
Test-Path docs\developer-documentation-system\zh\README.md
Test-Path docs\developer-documentation-system\en\README.md
```
