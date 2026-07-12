# ScriptLab Debug Host

`asharia-sample-viewer` can act as the native C++ CLR host used by ScriptLab debug attach tests.
This is intentionally an application-level smoke path, not the final gameplay scripting package.

## Command

```powershell
asharia-sample-viewer.exe `
  --scriptlab-host `
  --scriptlab-bridge-manifest D:\Game\apps\sample-viewer\scriptlab.bridge.json `
  --scriptlab-ready-file D:\Game\apps\sample-viewer\scriptlab.ready `
  --scriptlab-go-file D:\Game\apps\sample-viewer\scriptlab.go
```

The host:

- reads `scriptlab.bridge.json`
- loads `hostfxr`
- initializes CoreCLR from the bridge runtimeconfig
- loads the managed bridge assembly
- calls the configured `Prepare` method
- writes the ready file
- waits for the go file
- calls the configured `Entry` method

Use the ScriptLab runner after the host reaches the ready barrier:

```powershell
scripts\Invoke-ScriptLabEngineHostDebug.ps1 `
  -BridgeManifestPath D:\Game\apps\sample-viewer\scriptlab.bridge.json `
  -EnginePackageRoot D:\TechArt\VkEngine `
  -ScriptPath D:\TechArt\Asharia.ScriptLab\ScriptLab\Samples\PlayerMove.ash.cs `
  -OutputDirectory D:\TechArt\Asharia.ScriptLab\ScriptLab\bin\ScriptDebug\VkEngineSampleViewer `
  -GoFilePath D:\Game\apps\sample-viewer\scriptlab.go
```

The ready/go barrier lets ScriptLab attach through `netcoredbg` before the managed script entry point runs.
