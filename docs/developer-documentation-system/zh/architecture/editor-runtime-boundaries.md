# 架构：Editor And Runtime Boundaries

## 目的

本文说明当前 C++ runtime、native ImGui editor host、shared native bridge、Avalonia Studio shell 之间的边界。它回答谁拥有 editor state、谁拥有 GPU/runtime state、managed UI code 如何跨入 native rendering。

本文不描述 panel layout 细节或单个 feature 的实现计划。未来工作只用 `future` 标注。

## 分层

| 层 | 当前路径 | 当前职责 |
|---|---|---|
| Runtime packages | `engine/*`、`packages/*` | Engine data structures、rendering backend、asset/model packages、scene/resource state。 |
| Native editor executable | `apps/editor/src` | ImGui host、panel registration、actions、shortcuts、asset browser、frame debugger、viewport runtime、editor smokes。 |
| Native bridge library | `apps/editor/src/native_bridge`、`editor-native` target | native runtime 导出给 Studio viewport 和 frame debugger interop 的 C ABI。 |
| Managed Studio shell | `apps/studio/Core`、`apps/studio/Shell`、`apps/studio/Features` | Avalonia shell、command/dock/panel models、viewport composition models、managed interop adapters。 |
| Managed tests | `apps/studio/Tests` | Studio architecture、model、interop、feature、XAML tests。 |

## 状态 Owner

| 状态 | Owner | 访问方向 |
|---|---|---|
| Vulkan context、frame loop、shared image handles、present packets | `apps/editor` native runtime 和 `packages/rhi-vulkan` | Studio 通过 C ABI 请求 snapshots 或 packets；它不拥有 native GPU objects。 |
| Native viewport producer 和 packet lifetime | `editor-native` implementation | Studio 必须通过 bridge 释放 acquire 得到的 present packets。 |
| Frame debugger capture state 和 JSON snapshot buffer | `apps/editor` native frame debugger bridge | Studio 复制 UTF-8 JSON payload，然后释放 native buffers。 |
| ImGui panels、native menu/actions/tools、native editor smokes | `apps/editor` | Avalonia Studio 不直接修改该状态。 |
| Dock workspace、command palette、contribution descriptors、lifecycle events | `apps/studio/Shell` 和 `apps/studio/Core` | Native runtime 不是 managed shell dependency。 |
| Scene View scheduling plans 和 viewport status snapshots | `apps/studio/Core/Models/Viewports`、`apps/studio/Core/Services` 和 Scene View feature code | `ViewportScheduler` 是 pure planner；live Scene View cadence 还经过 panel frame scheduling、lifecycle、native present presenter 和 present drain。 |
| Studio feature panel view models | `apps/studio/Features/*` | Feature code 消费 shell/core abstractions，不消费 native C++ package internals。 |

## 依赖方向

- Runtime packages 不依赖 `apps/editor` 或 `apps/studio`。
- `apps/editor` 可以聚合 runtime packages，因为它是 host application。
- `editor-native` 从 native side 导出 C ABI。它链接选定 runtime packages，但不向 Studio 暴露 C++ classes。
- `apps/studio` 通过 `IViewportNativeApi`、`ViewportNativeBridge`、`IFrameDebuggerNativeApi`、`FrameDebuggerNativeBridge` 与 native code 通信。
- Studio managed models 存储 snapshots 和 statuses，不把 native pointers 当作短生命周期 interop struct 之外的状态保存。
- C++ editor panels 可以通过 native services inspect runtime state。它们不应成为 public package APIs。

## Native Bridge Flow

Viewport composition flow：

1. Studio 收集 supported handle types 和 device identity 等 composition capabilities。
2. `ViewportNativeBridge.QueryCompositionCompatibility()` 构建 `EditorViewportNativeCompatibilityRequest`。
3. `editor_viewport_query_composition_compatibility()` 验证 ABI version、struct size、handle type、device compatibility。
4. Studio 复制返回的 UTF-8 message，把 native status 映射到 `ViewportNativePresentStatus`，需要时释放 compatibility result。
5. `ViewportNativeBridge.AcquirePresentPacket()` 为 viewport extent 请求 packet。
6. Native code 返回 image/semaphore handles、format、memory size、frame index、status。
7. Studio 把 packet 转换成 managed snapshot，然后调用 `editor_viewport_release_present_packet()`。

Frame Debugger v0 在默认 Workbench wiring 中是 fixture-backed 的只读面板。Native bridge 作为可注入路径存在：

1. Studio 调用 `FrameDebuggerNativeBridge.RequestCapture()` 或 `RequestResume()`。
2. Studio 需要当前数据时调用 `TryAcquireSnapshot()`。
3. Native 返回带 `JsonUtf8` payload 的 `EditorFrameDebuggerNativeSnapshotBuffer`。
4. Studio 把 bytes 复制到 `NativeFrameDebuggerSnapshotPayload`，然后释放 native buffer。
5. 选择 execution event 时，把 UTF-8 string view 传入 native code。

## Render Flow

- Native editor viewport rendering 拥有 Vulkan resources 和 frame-level synchronization。
- `apps/studio` 只拥有 managed viewport request/scheduling state 和 composition host/presenter state。
- Present packets 是短生命周期对象。Managed layer 必须在 snapshot conversion 后释放 packet，避免泄漏 native handles。
- Device mismatch、unsupported composition interop、unsupported handle type、device lost、render failure、internal error 都以 native status code 表示，并映射到 managed statuses。

## 生命周期

1. Studio 启动并构建 shell/core/feature services。
2. Scene View 读取 composition capabilities，并询问 native code 是否支持 shared composition。
3. 每次 render request，Studio 按具体 viewport extent 请求 present packet。
4. Studio 复制 present metadata，并释放 native packet。
5. Frame debugger snapshots 按相同 ownership rule acquire、copy、release。
6. Studio shutdown 或 native backend 不再可用时，`ViewportNativeBridge.Shutdown()` 只调用一次 native shutdown，之后的调用按 unavailable 处理。
7. Native runtime 按 RHI/editor ownership order 销毁 Vulkan resources。

## 错误处理

| 失败 | Owner | 预期行为 |
|---|---|---|
| Native DLL missing、entry point missing、bad image | Studio interop adapter | 捕获 binding exception，并返回 unavailable managed status。 |
| Frame debugger native DLL binding failure | Studio frame debugger bridge | 该 bridge 当前不像 `ViewportNativeBridge` 那样包装 DLL 或 entry-point binding exceptions；调用方应保留 fixture/read-only fallback path。 |
| Unsupported ABI 或 struct size | Native bridge 和 managed model validation | 返回 unsupported ABI status；不解引用不兼容 buffer。 |
| Unsupported composition handle type | Native bridge | 返回 unsupported composition 或 unsupported handle type status。 |
| Device mismatch | Native bridge | 返回 device mismatch；Studio 保持 managed state 可检查。 |
| Render failure 或 device lost | Native runtime | 返回 status 和 message；Studio 映射到 render failed/device lost。 |
| Snapshot buffer format mismatch | Studio frame debugger bridge | 拒绝 snapshot，并在需要时释放 buffer。 |

## 未来工作

`future`: C structs 改为由单一 schema 生成后，Studio 可以拥有 generated native ABI compatibility report。当前事实源仍是 C header、C# mirror structs 和 tests。

`future`: 如果 Studio 增加多个 native viewport backend，backend selection 必须留在 `IViewportNativeApi` 之下；feature view models 继续消费 snapshots 和 statuses。

## 验证方式

运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
powershell -ExecutionPolicy Bypass -File tools\pre-pr.ps1 -IncludeUntracked -RunCheapGates
dotnet build apps\studio\Editor.csproj -c Release
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "SceneView|ViewportNative|Composition|FrameDebuggerNative"
```

C++ editor 或 bridge 行为改变时运行 native editor smoke gates：

```powershell
foreach ($preset in @("clangcl-debug", "msvc-debug")) {
    $exe = "build\cmake\$preset\apps\editor\asharia-editor.exe"
    & $exe --smoke-editor-shell
    & $exe --smoke-editor-viewport-native
    & $exe --smoke-editor-frame-debugger
}
```

检查点：

- Studio 在 release 前复制 native buffers，且不把 native packet pointer 存为 durable state。
- Native ABI structs 保留显式 `abiVersion` 和 `structSize` checks。
- Runtime packages 保持独立，不依赖 `apps/studio`。
- Native bridge errors 以 managed statuses 和 messages 暴露，不能吞成成功的 empty frame。
