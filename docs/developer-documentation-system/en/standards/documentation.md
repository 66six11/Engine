# Standards: Documentation Writing

## Document Types

| Type | Directory | Must answer |
|---|---|---|
| Documentation entry | `README.md` | What the project is and how to read the docs |
| Architecture document | `architecture/` | How the system is layered, who owns state, and who may depend on whom |
| Detailed design | `design/` | How a feature is implemented, including modules, data structures, flows, error handling, and tests |
| API Reference | `api/` | How to call interfaces, which parameters they take, what they return, and how they fail |
| Guide | `guides/` | How a developer completes a concrete task |
| Workflow | `workflow/` | How build, test, release, and code review are executed |
| ADR | `adr/` | Why a decision was made and which alternatives were rejected |
| Standards | `standards/` | Coding, naming, text, and documentation rules |

## Language Strategy

- Body text under `zh/` uses Simplified Chinese.
- Body text under `en/` uses English.
- The two language directories keep mirrored file names.
- Code identifiers, paths, target names, API names, commands, and fixed product names stay unchanged.

## General Rules

- Write current facts before future plans.
- Plans must be labeled `planned`, `proposal`, or `future`.
- Prefer constraints over slogans.
- Every document must include validation.
- Reference real files, targets, class names, function names, and commands.
- Do not treat old documents as the source of truth. Prefer code, build files, public headers, tests, and tools as evidence.
- When architecture facts change, sync the entry point and related architecture/design/API/guide/workflow documents.

## Detailed Design Template

```markdown
# Detailed Design: Feature Name

## Context
Why this work exists.

## Goals
What this change must achieve.

## Non-Goals
What this change explicitly does not do.

## Current Constraints
Existing system, performance, compatibility, platform, and dependency limits.

## Overall Design
Core implementation approach.

## Module Breakdown
Which modules, classes, and files are added or modified.

## Data Structures
Key structs, fields, and states.

## API Design
Public interfaces, parameters, return values, and errors.

## Key Flows
Normal flow, failure flow, and boundary flow.

## Lifetime
Creation, update, release, and failure cleanup.

## Error Handling
Error types, recovery strategy, and logging.

## Test Plan
Unit tests, integration tests, and manual validation.

## Risks
Likely failure points and fallback options.
```

## API Document Template

````markdown
# API Reference: Module Name

## Type / Class Name

### `functionName(args)`

Explain what this interface does.

Parameters:

| Parameter | Type | Required | Description |
|---|---|---|---|

Return values:

| Type | Description |
|---|---|

Errors:

| Error | Trigger |
|---|---|

Example:

```cpp
// example
```
````

## Validation

For documentation-only PRs:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1 -IncludeUntracked
```

Manual review checklist:

- The doc names concrete files or targets.
- It separates current facts from `future` or `proposal`.
- It contains a runnable command, a check, an example, or a failure mode.
- It links to the relevant index when it becomes a stable entry.
