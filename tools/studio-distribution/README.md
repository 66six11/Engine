# Studio Distribution input producers

`Asharia.Studio.Distribution` is the .NET build/release boundary for two independent, closed inputs intended for canonical Engine Distribution assembly:

- an Editor Image built from an isolated `dotnet publish` directory for the fixed Studio plus an exact hostfxr/runtime selection; the pinned SDK apphost template is a build-time qualification input only;
- the repo-owned production Windows Editor Host Profile, emitted as exact canonical bytes.

The current v1 Editor Image is explicitly **Windows x64** because Studio publishes a `win-x64` apphost. It requires the real top-level `Editor.exe`, `Editor.dll`, `Editor.deps.json`, `Editor.runtimeconfig.json`, `Asharia.Studio.Application.dll`, `Asharia.Runtime.Contracts.dll`, `Asharia.Studio.EngineBridge.dll`, and `Asharia.Studio.Presentation.Avalonia.dll`. It also requires the exact project, editor-content, scene, and viewport native DLLs, validates their production exports (including the exact `asharia_editor_content_query` editor-content entry point), and admits exactly the 22 declared renderer-basic shader/reflection files below `shaders/renderer-basic`. On every normalized publish path, the producer still rejects the retired `Asharia.Editor`, development host/protocol, and `slang` artifact stems (including sidecars and case variants), unexpected editor-content or `editor_native` sidecars or locations, extra renderer-basic shaders, reserved `managed`/`metadata`/`sdk`/`packs` build-environment directories, `dotnet.exe`, and `managed-build-environment.json`.

The producer copies only selected trees, returns the complete `path + role + mediaType + size + sha256` binding set on stdout, and commits a fresh output root with one no-overwrite directory move. It does not emit the retired ProjectCode managed-build-environment projection, run Conan/CMake, discover a “latest” version, generate an `EngineGenerationId`, combine inputs into a Distribution generation, or invoke Python.

Python is repository-only development, validation, and CI tooling; it is not an Editor capability or product dependency. The producer fails closed if a selected publish or .NET tree contains conventional Python source/bytecode, wheels or extension modules, virtual-environment/package directories, or interpreter/runtime artifacts. A successful Editor Image receipt cannot bind those payloads.

The same logical-path policy is independently enforced by Package Artifact verification/publication, canonical Distribution assembly, and installed-generation health verification. A self-consistent legacy manifest or receipt is not an exemption. Downstream Python implementations are repository reference oracles only; formal Editor, Launcher, Installer, and Repair executables must consume portable contracts from C#/.NET or native code.

This is a build/release tool, not an untrusted-filesystem sandbox. Input roots and the pre-existing output parent must be release-orchestrator-owned and not attacker-writable. The producer rejects reparse points and verifies every copied byte. Logical image paths use the portable ASCII v1 subset, including Windows reserved-name and trailing-dot/space rejection. A successful receipt makes the root eligible for downstream assembly; it does not prove complete Distribution health.

## Static qualification boundary

The caller explicitly supplies the SDK apphost-template version, hostfxr version, and host-runtime version. The producer statically verifies:

- the selected SDK apphost template and the fixed `Editor.dll` with AppRelative `../managed/dotnet` binding;
- required Studio managed identities plus deps/runtimeconfig evidence;
- project/editor-content/scene/viewport native PE identities and required exports, plus the exact viewport shader bundle;
- required `hostfxr.dll` direct exports;
- selected hostfxr/runtime assembly and product-version anchors.

It copies the complete selected `host/fxr/<version>` and `shared/Microsoft.NETCore.App/<version>` trees and binds every staged byte in the receipt. `dotnet.exe`, `sdk/`, and `packs/` are not part of the AppRelative launch closure and are not staged. This qualifies the requested local selection; it does not authenticate Microsoft provenance, start `Editor.exe`, load a DLL, call hostfxr, or prove launch/ABI/runtime health.

The apphost comparison allows only the two official binding slots, GUI subsystem field, and exact fields written by the .NET resource updater. The reconstructed `.rsrc` section follows the .NET 10 HostModel writer layout and rejects extra executable or resource payload.

## Build and test

```powershell
dotnet build tools\studio-distribution\Asharia.Studio.Distribution.csproj -c Release
dotnet test tools\studio-distribution.Tests\Asharia.Studio.Distribution.Tests.csproj -c Release
```

## Produce an Editor Image input

Use a fresh, release-orchestrator-owned publish directory. `dotnet publish` does not clean a reused `PublishDir`, so reuse is outside this contract. `EditorImage.pubxml` configures the apphost to search the sibling bundled runtime at `../managed/dotnet`. Conan plus the selected Release CMake preset must already have produced the four native adapters and renderer-basic shader bundle copied by `Editor.csproj`.

```powershell
$releaseRoot = 'D:\Build\Asharia'
New-Item -ItemType Directory -Force -Path $releaseRoot | Out-Null
$publishRoot = Join-Path $releaseRoot ("studio-publish-" + [guid]::NewGuid().ToString('N'))
$sdkVersion = '10.0.302'

if ((dotnet --version).Trim() -ne $sdkVersion) {
  throw "Repository global.json did not select the required .NET SDK $sdkVersion."
}

dotnet publish apps\studio\Editor.csproj `
  -c Release `
  -p:PublishProfile=EditorImage `
  -p:PublishDir="$publishRoot\"
```

Stage a fresh Editor Image with exact versions. The output root must not exist.

```powershell
$hostFxrVersion = '10.0.10'
$hostRuntimeVersion = '10.0.10'

dotnet run --project tools\studio-distribution\Asharia.Studio.Distribution.csproj `
  -c Release -- `
  stage-editor-image `
  --publish-root $publishRoot `
  --entry-point Editor.exe `
  --dotnet-root "C:\Program Files\dotnet" `
  --sdk-version $sdkVersion `
  --hostfxr-version $hostFxrVersion `
  --host-runtime-version $hostRuntimeVersion `
  --output-root D:\Build\Asharia\editor-image
```

The result is an input to Engine Distribution assembly, not a complete generation. Real installable package artifacts, canonical assembly, installed-generation byte health, and launcher-owned current selection remain downstream work.

## Produce the Editor Host Profile input

```powershell
dotnet run --project tools\studio-distribution\Asharia.Studio.Distribution.csproj `
  -c Release -- `
  stage-editor-host-profile `
  --output-root D:\Build\Asharia\editor-host-profile
```

This output is also only an assembler input.
