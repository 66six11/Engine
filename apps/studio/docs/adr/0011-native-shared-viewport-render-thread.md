# ADR-0011：Studio shared viewport 的唯一 RenderThread 与 V5 stream scheduler

状态：Accepted / Implemented
日期：2026-08-08

## 决策摘要

`editor_native.dll` 的 `EditorSharedViewportRuntime` 是进程内唯一 shared viewport Vulkan owner。Scene、Game 和 Preview
stream 共享同一 context、producer、graphics queue、renderer cache、frame epoch tracker、单调 frame clock 和 retirement。Avalonia control、
`ViewportSession`、EngineBridge caller 与每个 stream 都不是 renderer owner。

V5 把旧同步 request/wait facade 硬切为：非阻塞 latest submit、非阻塞 ready take、显式 frame completion、显式 import release
和异步 close/poll/destroy。V4 frame symbols 不导出。

## Owner boundary

```text
Avalonia UI
  publishes immutable ViewportRenderRequest
        |
EngineBridge V5
  copies blittable request into native owning packet
        |
per-stream mailbox
  pending-latest(1) / ready(1) / slots(3)
        |
EditorSharedViewportRuntime RenderThread
  Vulkan context + monotonic frame clock + producer + record + submit + retirement + shutdown
        |
GPU / Avalonia compositor
```

跨线程 packet 只包含 owning `std::string`、`std::vector` 和纯值 snapshot。不得保存 managed pointer、Avalonia object、
`SceneDocument`/World pointer、`std::span` 或 `std::string_view` 到 dispatch 之后。

## 调度规则

### Stream lane

每个 stream：

- 最多一个 executing render；
- 最多一个 pending latest，重复 submit 原位覆盖；
- 最多一个 ready frame；
- 最多三个 persistent slots。

RenderThread 的 control/release queue 仍优先于 render work；stream scheduler 每次 owner loop 至多推进一个状态转换，
避免单个 viewport 独占 owner。GPU fence 未完成时 owner 使用有界 1 ms retirement poll，不阻塞 UI caller。

### Slot ownership

slot state 和全部 Vulkan object 只在 RenderThread 创建、reset、submit、poll、retire 和析构。managed 只持有不透明 stream/slot
identity 以及专供 compositor import 的 duplicate external handles。

首次 render 创建完整 slot；复用 render 重置该 slot 自己的 fence/command pool，并在 submit 中等待上一次 compositor 的
consumer-available semaphore。ready take 只改变 stream 状态，不触碰 Vulkan。

### Completion

`complete_frame_v5` 是非阻塞所有权消息：

- `NotSubmittedToConsumer`：这次 surface update 没有开始，slot 下一次 submit 不等待 consumer signal；
- `ConsumerAccessed`：surface update 成功，下一次复用必须 GPU-wait consumer-available semaphore。

unknown completion kind 返回 `InvalidArgument`，不消费 Presented 所有权。重复/foreign completion 同样拒绝。

### Close

`close_stream_v5` 停止 admission、丢弃 pending latest，并把未 take 的 ready frame按 NotSubmitted 回收。Presented slot 必须先有
completion；曾暴露给 managed 的 slot 还必须收到 `release_slot_import_v5`。满足后 owner 调用现有 consumer-safe
`releasePresentPacketOnRenderThread`：

- last completion 为 NotSubmitted：只等 producer fence；
- last completion 为 ConsumerAccessed：提交 wait-only consumer wait 和独立 fence；
- fence 查询/submit 失败：quarantine，不提前析构。

stream slots 转入 retirement 后即可报告 Closed；runtime context 在 packet retirement 仍未完成时继续存活。caller 只在 Closed
后调用 destroy，删除 stream registry identity。

## 帧身份与单调时间

Runtime 在 producer 创建时重置 `std::chrono::steady_clock` epoch，并在 RenderThread 真正准备 record 时生成 immutable
`BasicRenderViewFrameParams`。record/submit 成功后才推进 last-render sample：

```text
frameIndex   = process-level render-attempt identity; failed attempts may leave gaps
timeSeconds  = monotonic elapsed since shared producer epoch
deltaSeconds = elapsed since the previous successful render from any shared stream; first frame is 0
```

`frameIndex`、managed `RequestSequence`、SceneDocument `TargetRevision` 和时间是四个正交概念。frame index 在调用 producer 前分配，
用于标识一次实际 render attempt；失败不回收 identity，也不推进成功时间 sample。刷新率只改变时间采样密度，
不能改变材质/preview 时间速度；OnDemand 空闲后恢复时 absolute time 跳到当前 elapsed，delta 记录真实 render 间隔。
禁止恢复 `frameIndex / 60` 或固定 `1 / 60`，也禁止把 Avalonia compositor cadence、GPU timeline semaphore 或物理 present
反向用作 host/editor 时间源。当前 delta 是 shared viewport render delta，不是 World simulation fixed-step delta。

## Runtime shutdown

managed caller 不在 Avalonia dispatcher 上执行每毫秒 native poll。ready-frame 与 close-state 等待在 UI context 外轮询；只有 ready
结果、错误投影和 compositor import/commit 回到 UI thread。这个边界不改变 native 单一 owner thread，也不把 Vulkan ownership
移到 managed worker。

```text
stop managed admission
-> drain composition updates/import cleanup
-> close and destroy streams
-> request runtime shutdown
-> drain release/control/render lanes
-> poll packet retirement
-> shutdown-only context cleanup
-> join RenderThread
```

没有 `vkDeviceWaitIdle` 进入 render/resize loop。无法证明 consumer/GPU completion 的资源保留到 process exit；shutdown 可以
进入 Faulted，而不能从 caller thread 强制析构。

## 采用与拒绝

采用：Unreal 式 immutable render snapshot/owner thread、Avalonia sample 的 persistent image+semaphore pair、bounded mailbox。
拒绝：每 panel 一条 render thread、caller 直接 Vulkan、unbounded FIFO resize commands、固定 UI render timer、
producer fence 代替 consumer completion、独立 renderer process（当前故障隔离/跨进程共享需求不足以支付复杂度）。

## 当前限制

- 多 stream 共用单 graphics queue；consumer wait 延迟可能造成 head-of-line blocking；
- scheduler 当前是 owner-loop 扫描，不提供优先级权重；多 viewport 公平性需要后续压力数据；
- full slot 的 imported wrapper 由 Avalonia adapter 管理，native 不接触 Avalonia API；
- V5 目前只支持 `DocumentScene` target kind 和 Vulkan opaque NT handles。

## 验证

- MSVC native V5 smoke 验证 34 次 submit 只产生 4 个 render、coalesced counter、三个 distinct slots 和 reuse；
- smoke 验证 close 前 import release、Closed 后 destroy、runtime shutdown/join；
- managed tests 验证 exact-once completion、ABI sizes、stream lifecycle 和 failure mapping；
- CPU clock smoke 以注入 time point 验证首帧 0、5 ms 连续帧、失败 attempt 不推进成功 sample、2 s dirty-only idle gap、
  reset 建立新 epoch，以及时间与 attempt identity 解耦；
- DLL export audit 验证 V5 frame exports 存在、V4 create/release packet exports 不存在。

## 资料

- Unreal threaded rendering: https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine
- Vulkan queue external synchronization: https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueSubmit2.html
- Vulkan threading: https://docs.vulkan.org/guide/latest/threading.html
- Avalonia 12.1.0 Vulkan sample: https://github.com/AvaloniaUI/Avalonia/blob/12.1.0/samples/GpuInterop/VulkanDemo/VulkanSwapchain.cs
