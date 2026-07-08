# ADR 0002: Independent Deployable Documentation Root

## Status

Accepted.

## Context

The repository already contains historical documentation under `docs/`. The new developer documentation system needs a clean taxonomy and must not be rewritten by copying old documents. Deployment also needs a stable source directory that docs-site can sync without picking up legacy planning or historical notes.

## Decision

Create the new documentation system under:

```text
docs/developer-documentation-system
```

The docs-site sync workflow publishes this directory, not the whole `docs/` tree.

The workflow dispatch payload includes:

```json
{
  "engine_docs_dir": "docs/developer-documentation-system"
}
```

## Alternatives

| Alternative | Rejected because |
|---|---|
| Replace all old `docs/` content in place | It risks losing historical planning material and makes review noisy |
| Create a second top-level `developer-docs/` directory | It would bypass existing CODEOWNERS/doc sync expectations that watch `docs/` |
| Keep deploying all of `docs/` | It would expose old planning and mixed taxonomy as the new public docs root |
| Put deployable docs in external docs-site only | Engine facts would drift from code because the source would leave the code repo |

## Consequences

Benefits:

- New docs have one deployable root.
- Old docs can be migrated or deleted later through explicit PRs.
- Docs-site can sync a smaller, predictable source directory.

Costs:

- Links from old docs to new docs are not automatically migrated.
- Docs-site must honor `engine_docs_dir`; if it hardcodes `docs/`, it needs an update.

## Validation

```powershell
Get-Content -Raw .github\workflows\docs-site-sync.yml
Test-Path docs\developer-documentation-system\README.md
```

Checkpoints:

- Workflow path filters target `docs/developer-documentation-system/**`.
- Dispatch payload contains `engine_docs_dir`.
