# Workflow: Documentation Site Deployment

## Current Deployment Source Directory

The deployable documentation root is:

```text
docs/developer-documentation-system
```

This directory is the source sent to the external docs-site repository. Older files under `docs/` are not the deployment root.

## Engine Repository Workflow

`.github/workflows/docs-site-sync.yml` validates docs changes and notifies the docs-site repository when the required token is configured.

Current behavior:

- Pull requests validate changes under `docs/developer-documentation-system/**`.
- Pushes to `main` validate the same directory.
- On non-PR runs, the workflow dispatches the docs-site workflow only when `DOCS_SITE_DISPATCH_TOKEN` is configured; otherwise it exits after a notice.
- The dispatch payload includes:
  - `engine_repository`,
  - `engine_ref`,
  - `engine_docs_dir`,
  - `source_sha`,
  - `source_run_id`.
- Optional Vercel deploy hook is triggered when `VERCEL_DEPLOY_HOOK_URL` is configured.

## Required Repository Settings

| Name | Type | Required | Purpose |
|---|---|---|---|
| `DOCS_SITE_DISPATCH_TOKEN` | secret | No, but needed for automatic dispatch | Token that can call repository dispatch on docs-site |
| `DOCS_SITE_REPOSITORY` | variable | No | Defaults to `66six11/VkEngine-docs-site` |
| `VERCEL_DEPLOY_HOOK_URL` | secret | No | Optional direct Vercel deploy hook |

## Outbound Contract

This repository only defines the payload it sends to the documentation site. It does not document the external repository's internal implementation.

Current payload fields:

- `engine_repository`
- `engine_ref`
- `engine_docs_dir`
- `source_sha`
- `source_run_id`

## Local Deployment Check

This repository does not document the external docs-site build internals. The Engine workflow validates text encoding and whitespace only. Local validation here is limited to source and workflow checks:

```powershell
Get-Content -Raw .github\workflows\docs-site-sync.yml
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```

Checkpoints:

- Workflow path filters include `docs/developer-documentation-system/**`.
- Dispatch payload includes `engine_docs_dir`.
- `docs/developer-documentation-system/README.md`, `docs/developer-documentation-system/zh/README.md`, and `docs/developer-documentation-system/en/README.md` exist.
