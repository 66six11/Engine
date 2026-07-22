# Studio Distribution input producers

`Asharia.Studio.Distribution` is the .NET build/release boundary for two independent, closed inputs intended for canonical Engine Distribution assembly:

- an Editor Image built from an isolated `dotnet publish` directory for the fixed Studio plus one exact .NET installation selection: host, SDK, hostfxr, runtime, and reference pack;
- the repo-owned production Windows Editor Host Profile, emitted as exact canonical bytes.

The current v1 release contract is explicitly **Windows x64** because the repository's production native presets and Studio native publish wiring are Windows-only today. It requires the real top-level `Editor.exe`, `Editor.dll`, `Editor.deps.json`, `Editor.runtimeconfig.json`, `Asharia.Runtime.Contracts.dll`, `Asharia.Editor.dll`, `editor_native.dll`, and `slang.dll`; native host files must be native x64 PE images rather than renamed managed assemblies.

It copies only the selected trees, writes UTF-8/LF `metadata/managed-build-environment.json`, returns the complete `path + role + mediaType + size + sha256` binding set on stdout, and commits a fresh output root with a no-overwrite directory move. It does not run Conan/CMake, choose “latest” versions, generate an `EngineGenerationId`, combine these inputs with package artifacts into a Distribution generation, or invoke Python.

Python is repository-only development, validation, and CI tooling; it is not an Editor capability or product dependency. The producer therefore fails closed if a selected publish or .NET tree contains conventional Python source/bytecode, wheels or extension modules, virtual-environment/package directories, or interpreter/runtime artifacts. A successful Editor Image receipt cannot bind those payloads.

This is a build/release tool, not an untrusted-filesystem sandbox. The publish root, .NET root, and pre-existing output parent must be release-orchestrator-owned and not attacker-writable. The producer rejects reparse points and verifies every copied byte, but its Windows containment checks intentionally rely on canonical release inputs rather than defending against device aliases or 8.3 aliases supplied by an adversary.
Logical image paths are intentionally restricted to portable ASCII in v1, including Windows reserved-name and trailing-dot/space rejection; project asset naming is not constrained by this release-image rule.
Only a successful receipt makes the root eligible to be submitted to downstream assembly; the assembler still owns its own preflight and staged-byte revalidation. If verification detects drift after the atomic directory move, the producer deliberately leaves that output path in place rather than recursively deleting a path another actor could have replaced; release orchestration must quarantine or remove the failed root explicitly and must never treat directory existence as success.

## Static qualification boundary

An exact .NET selection means the caller explicitly supplies the SDK, hostfxr, host runtime, and reference-pack version directories. The producer does not discover a newest version. It statically checks the selected SDK apphost template and the fixed `Editor.dll` plus AppRelative `../managed/dotnet` bindings; managed assembly identities and Editor deps/runtimeconfig evidence; required direct exports and internal names for `editor_native.dll`, `slang.dll`, and `hostfxr.dll`; and the selected SDK/runtime/reference assembly and product-version anchors. SDK bundled-version evidence must be one closed XML document without explicit or SDK imports, so an external evaluation cannot override the inspected values. It then copies the complete selected trees and binds every staged byte in the receipt. This qualifies the requested local selection; it does not authenticate Microsoft provenance or define an official full-file inventory.

For the apphost, every byte covered by the selected template must match except the two official binding slots, the GUI subsystem field, and the exact fields written by the .NET resource updater. The one appended, non-executable `.rsrc` section must use canonical geometry and zero raw padding; arbitrary header/data-directory changes and payloads disguised as an enlarged resource section are rejected. Its bytes are reconstructed from the bounded type/name/language tree and payloads in the fixed `Editor.dll` using the .NET 10 HostModel writer order, alignment, directory metadata, and data-entry layout. This intentionally does not require the source DLL and generated apphost to have equal aligned `.rsrc` raw sizes. The reconstruction models HostModel's CodePage normalization from compiler value `0` to updater value `1252` and rejects any other source CodePage.

These checks do not start `Editor.exe` or `dotnet.exe`, load managed/native DLLs, call hostfxr, or exercise an ABI. Loadability, dependency closure, launch behavior, native ABI behavior, and runtime health remain downstream validation boundaries.

## Build and test

```powershell
dotnet build tools\studio-distribution\Asharia.Studio.Distribution.csproj -c Release
dotnet test tools\studio-distribution.Tests\Asharia.Studio.Distribution.Tests.csproj -c Release
```

## Produce an Editor Image input

First build the `msvc-release` native target, then create the Studio publish input in a newly chosen, release-orchestrator-owned directory. `dotnet publish` does not clean a reused `PublishDir`, so reusing an old directory is outside this contract. `EditorImage.pubxml` keeps the normal developer build unchanged, binds the Release native preset, publishes the two required native DLLs, and configures the apphost to search the sibling bundled runtime at `../managed/dotnet`.

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

Then stage a fresh Editor Image root with exact versions. The two contract paths must belong to the publish root, and the output root must not already exist.

```powershell
$hostFxrVersion = '10.0.10'
$hostRuntimeVersion = '10.0.10'
$referencePackVersion = '10.0.10'

dotnet run --project tools\studio-distribution\Asharia.Studio.Distribution.csproj `
  -c Release -- `
  stage-editor-image `
  --publish-root $publishRoot `
  --entry-point Editor.exe `
  --dotnet-root "C:\Program Files\dotnet" `
  --sdk-version $sdkVersion `
  --hostfxr-version $hostFxrVersion `
  --host-runtime-version $hostRuntimeVersion `
  --reference-pack-version $referencePackVersion `
  --runtime-contract (Join-Path $publishRoot 'Asharia.Runtime.Contracts.dll') `
  --editor-contract (Join-Path $publishRoot 'Asharia.Editor.dll') `
  --output-root D:\Build\Asharia\editor-image
```

The result is an input to Engine Distribution assembly, not a complete Distribution generation. A complete generation still needs the independently produced Editor Host Profile input, real installable package artifacts, canonical Distribution assembly, installed-generation byte health, and launcher-owned current selection.

## Produce the Editor Host Profile input

Stage the repo-owned production policy into another fresh root. The command embeds and validates the canonical profile, writes only `profiles/editor/asharia.host-profile.json`, returns its host/platform/size/SHA-256 binding, and never overwrites an existing root.

```powershell
dotnet run --project tools\studio-distribution\Asharia.Studio.Distribution.csproj `
  -c Release -- `
  stage-editor-host-profile `
  --output-root D:\Build\Asharia\editor-host-profile
```

This output is also only an assembler input. The complete generation still needs real installable package artifacts, canonical Distribution assembly, installed-generation byte health, and launcher-owned current selection.
