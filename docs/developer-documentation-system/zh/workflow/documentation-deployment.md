# Workflow：文档站部署

## 当前部署源目录

```text
docs/developer-documentation-system
```

这个目录包含语言选择入口、`zh/` 中文文档和 `en/` 英文文档。旧 `docs/` 下其他文件不是部署根。

## Engine Repository Workflow

`.github/workflows/docs-site-sync.yml` 对文档改动执行编码和空白检查；在非 PR 事件中，如果配置了 `DOCS_SITE_DISPATCH_TOKEN`，它会通知 docs-site repository。

当前行为：

- Pull request 只验证 `docs/developer-documentation-system/**` 和 workflow 文件。
- push 到 `main` 验证同一目录。
- 没有 `DOCS_SITE_DISPATCH_TOKEN` 时，workflow 打印 notice 后跳过 repository dispatch。
- dispatch payload 包含 `engine_repository`、`engine_ref`、`engine_docs_dir`、`source_sha`、`source_run_id`。
- `VERCEL_DEPLOY_HOOK_URL` 配置时，会额外触发 Vercel deploy hook。

## Required Settings

| Name | Type | Required | Purpose |
|---|---|---|---|
| `DOCS_SITE_DISPATCH_TOKEN` | secret | 自动 dispatch 需要 | 调用 docs-site repository dispatch |
| `DOCS_SITE_REPOSITORY` | variable | 否 | 默认 `66six11/VkEngine-docs-site` |
| `VERCEL_DEPLOY_HOOK_URL` | secret | 否 | 可选 Vercel deploy hook |

## 出站契约

本仓库只定义发送给文档站的 payload，不描述外部仓库的内部实现。

当前 payload 字段：

- `engine_repository`
- `engine_ref`
- `engine_docs_dir`
- `source_sha`
- `source_run_id`

## 本地检查

Engine repo 本地只验证源文件和 workflow；不记录外部文档站的内部 build 细节。

```powershell
Get-Content -Raw .github\workflows\docs-site-sync.yml
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```

检查点：

- path filters 包含 `docs/developer-documentation-system/**`。
- dispatch payload 包含 `engine_docs_dir`。
- `docs/developer-documentation-system/README.md`、`zh/README.md`、`en/README.md` 存在。
