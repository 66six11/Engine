# ADR 0001: Package-First Boundaries

## Status

Accepted.

## Context

The codebase is organized as CMake packages under `engine/`, `packages/`, `apps/`, and `tools/`. Each reusable C++ unit defines its own target and alias such as `asharia::rendergraph` or `asharia::rhi_vulkan`.

The engine needs standalone package builds, package-local tests, and clear dependency direction between RenderGraph, RHI, renderer, asset pipeline, and editor hosts.

## Decision

Use package-first boundaries:

- Each package owns its `CMakeLists.txt`, public `include/`, private `src/`, optional `tests/`, and `asharia.package.json`.
- Cross-package use goes through CMake targets and public headers.
- `src/` directories are private.
- App targets may aggregate packages; reusable packages may not depend on apps.
- Backend-specific integration uses separate targets when it would otherwise pollute a backend-agnostic API.

## Alternatives

| Alternative | Rejected because |
|---|---|
| One monolithic engine library | It would hide dependency direction and make package-local validation weak |
| Header-only packages for all modules | It would expose implementation details and slow rebuilds |
| App-owned reusable modules | Runtime packages would start depending on editor/sample concerns |
| RenderGraph directly includes Vulkan | Backend-independent compile/diagnostics would no longer be portable |

## Consequences

Benefits:

- Package-local tests can validate APIs without building every app.
- Public/private API boundaries are visible in the filesystem.
- RHI/backend adapters can be reasoned about as explicit targets.

Costs:

- New packages require more CMake and manifest boilerplate.
- Moving code across boundaries requires updating target links and docs.

## Validation

```powershell
rg -n "#include .*src" engine packages apps tools
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

Check target links in package `CMakeLists.txt` when adding or moving APIs.
