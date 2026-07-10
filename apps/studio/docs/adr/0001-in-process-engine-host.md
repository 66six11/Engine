# ADR-0001：采用同进程模块化 EngineHost

状态：Accepted

日期：2026-07-11

## Context

Studio 需要正式连接 native engine/runtime/renderer，支持低延迟 authoring、多 World 和多 Viewport。当前直接由 View 创建 bridge、App 调用静态 shutdown 的方式缺乏统一所有权。

可选进程模型包括：

- UI 与 engine 直接同进程耦合；
- 一开始就把 engine/renderer 放到独立进程；
- 同进程运行，但通过明确的 EngineHost、Application ports 和 transport-neutral contract 隔离。

## Decision

采用第三种方案。

- 近期 native engine 与 Avalonia Studio 在同一进程运行。
- `EngineHost` 是 runtime/device/world/viewport native connection 的唯一 managed owner。
- Application 只依赖 engine/world/viewport ports，不依赖 P/Invoke。
- `Asharia.Studio.EngineBridge` 实现 `InProcessEngineTransport`。
- 上层合同不暴露 native pointer，因此未来可以增加 `IpcEngineTransport`。
- Standalone Game 本身仍是独立 game process，不受本 ADR 限制。

## Alternatives

### Direct in-process calls

Rejected。虽然实现最少，但会继续传播静态 global、View ownership、shutdown race 和不可测试的 P/Invoke 调用。

### Renderer/engine separate process immediately

Deferred。它增加 IPC、protocol version、process restart、project state sync 和跨进程 GPU handle transfer，而当前 session/domain contract 尚未稳定。

## Consequences

Positive：

- authoring 和 embedded viewport 延迟较低；
- GPU device/资源共享路径直接；
- Application 和 Presentation 可测试，不需要加载 native runtime；
- 未来 IPC migration 有明确接缝。

Negative：

- native crash 会终止 Studio；
- native library packaging 是 Studio 启动依赖；
- shutdown/device lost 必须在同一进程内严格协调；
- 不能把同进程便利变成 public API 假设。

## Follow-ups

- 建立异步 `StudioSession` 和 `EngineHost` state machine。
- 移除 static native shutdown 和 View-owned bridge。
- 定义 ABI negotiation、opaque session handle 和 structured result。
- 当 crash isolation 成为实际需求时，用相同 Application ports 评估 IPC transport。

## Validation

- Application tests 使用 fake transport 完成 startup/world/viewport/stop。
- Native bridge contract tests 验证 ABI mismatch 和 unavailable runtime。
- Shutdown tests 证明所有 world/viewport lease 在 EngineHost stop 前结束。
