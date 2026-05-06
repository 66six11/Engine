# 性能诊断与编辑器性能面板技术细节

研究日期：2026-05-06

适用范围：Windows 桌面端、Vulkan 1.4、C++23、single graphics queue、dynamic rendering、synchronization2、VMA、RenderGraph 每帧 record/compile 模型。

本文只定义性能诊断底座和未来编辑器性能面板的技术约束。编辑器产品功能尚未纳入当前开发计划，因此本文件不把 Editor UI、面板交互、asset database 或完整 Play Mode 工作流列为近期里程碑。

## 与现有计划的顺序关系

性能工作不是替代 RenderGraph 主线，而是插入到 compiler diagnostics 和 backend lifetime/cache 之间，作为后续优化的观测底座。

推荐顺序：

1. 先完成 `rendergraph-development-roadmap.md` 的 P3 compiler diagnostics v2。
2. 插入 P3.5 performance profiling substrate：CPU scope、frame profile 数据结构、benchmark CLI、RenderGraph compile counters。
3. 进入 P4 backend lifetime and caches：deferred deletion、descriptor allocator、transient resource pool、pipeline/layout cache，同时把 cache hit/miss、allocation/reuse 和 delayed destruction counter 接入 profiling。
4. 在 P4 生命周期稳定后接 Vulkan GPU timestamp 和 debug labels，避免 query readback、fence epoch 和 resize/recreate 生命周期与 MVP 资源路径互相打架。
5. P5/P6 继续扩展 buffer/storage/MRT、asset/material 时，只补对应 counters 和 pass labels，不改变 profiling 数据模型。
6. P7 multi-view prep 出现 `RenderView` 后，再把 profile target 拆分为 Game、Scene、Preview 和 Bench。
7. 编辑器性能面板只在未来 editor/PlaySession 被正式纳入计划后接入；当前只保留技术接口和数据分层约束。

## 防过度设计边界

性能系统第一版只回答“时间花在哪”和“资源是否复用”。它不是调试平台、可视化平台或编辑器功能的前置实现。

当前只允许进入主线：

- CPU scope。
- frame ring buffer。
- benchmark CLI。
- JSONL/CSV 输出。
- RenderGraph compile counters。
- P4 cache/lifetime counters。

当前不进入主线：

- editor performance panel。
- timeline UI。
- graph viewer UI。
- GPU timestamp query pool。
- RenderDoc/Nsight capture orchestration。
- 跨进程 profiler server。
- 跨线程采集聚合。
- 自动性能回归数据库。

这些能力可以保留接口余量，但不能创建必须维护的 runtime 路径。只有当 benchmark 数据证明 CPU-only profile 不足以定位问题，或者 P4/P5 的真实 GPU pass 成为瓶颈后，才进入 GPU timestamp 实现。

## 目标

第一阶段目标是回答这些问题：

- 一帧 CPU 时间花在 acquire、record graph、compile graph、prepare backend、record commands、submit/present 的哪一段。
- RenderGraph compile 是否随着 pass/resource 数量线性增长，是否有异常图导致诊断或排序成本暴涨。
- 后端缓存接入后，pipeline/layout/descriptor/transient resource 是否真的复用，是否还在每帧创建长期对象。
- fullscreen、depth、draw-list、future postprocess 等 pass 在 RenderDoc/Nsight 中能否用同一套 pass 名称定位。

非目标：

- 不用 `--smoke-*` 作为性能基准。
- 不在第一阶段做 editor performance panel。
- 不引入完整 Tracy/Remotery/ImGui profiler UI 作为前置条件。
- 不为了读取 profiler 数据调用 `vkDeviceWaitIdle`。
- 不让 `rendergraph` 依赖 Vulkan 或平台 profiler。

## 数据模型

新增轻量 profiling 包时建议保持零后端依赖：

```cpp
enum class ProfileTarget {
    Game,
    Scene,
    Preview,
    EditorHost,
    Bench,
};

struct FrameProfileInfo {
    std::uint64_t frameIndex;
    ProfileTarget target;
    std::string_view viewName;
};

struct CpuScopeSample {
    std::string_view name;
    std::uint64_t beginNanoseconds;
    std::uint64_t endNanoseconds;
};

struct GpuScopeSample {
    std::string_view name;
    double beginMilliseconds;
    double endMilliseconds;
};

struct CounterSample {
    std::string_view name;
    std::uint64_t value;
};
```

第一版只要求 CPU scope 和 counter 可用。GPU scope 可以先保留结构，等 Vulkan timestamp 接入后填充。

建议包边界：

| 位置 | 职责 | 禁止事项 |
| --- | --- | --- |
| `packages/profiling` | CPU scope、frame ring buffer、counter、JSONL/CSV writer | 依赖 Vulkan、RenderGraph 或 window |
| `packages/rendergraph` | 输出 compile counters 和 pass/debug label 字符串 | 持有 profiler 全局状态、引用 Vulkan |
| `packages/rhi-vulkan` | query pool、timestamp readback、debug utils object/command labels | 依赖 RenderGraph |
| `packages/rhi-vulkan_rendergraph` | 把 compiled graph label 映射到 Vulkan marker 名称 | 改变 RenderGraph 抽象状态 |
| `apps/sample-viewer` | `--bench-*` CLI、输出文件路径、汇总 p50/p95/max | 改变 `--smoke-*` 语义 |

## 第一阶段实现

建议先提交一个 CPU-only 版本：

- 新增 `packages/profiling`。
- 新增 `FrameProfiler`、`CpuProfileScope`、固定容量 frame ring buffer。
- 新增 JSONL 或 CSV 输出，默认写入 `build/perf/`。
- 新增 `--bench-rendergraph`，专门测 RenderGraph record/compile。
- 保持所有 `--smoke-*` 命令输出和语义不变。

第一版避免新增抽象层：

- 不创建 profiler plugin system。
- 不设计 editor-facing API。
- 不把 profiler singleton 注入 RenderGraph。
- 不改变 frame loop 的 submit/present 语义。
- 不为未来 UI 保存复杂层级树；先用 flat scope/counter 记录。

`--bench-rendergraph` 最低参数：

```text
--bench-rendergraph --warmup 60 --frames 600 --output build/perf/rendergraph.jsonl
```

输出至少包含：

- warmup frames
- measured frames
- average milliseconds
- p50 milliseconds
- p95 milliseconds
- max milliseconds
- pass count
- image count
- dependency edge count
- transition count
- culled pass count
- transient image count

## Vulkan timestamp 技术细节

GPU timestamp 放到第二阶段或 P4 生命周期稳定之后。

实现约束：

- 每个 in-flight frame 使用独立 `VkQueryPool` 或独立 query range。
- 每个 GPU scope 写 begin/end timestamp。
- 优先使用 synchronization2 路径下的 `vkCmdWriteTimestamp2`。
- 只读取已经由 fence 确认完成的历史帧 query。
- 用 physical device `timestampPeriod` 把 timestamp tick 转换为纳秒或毫秒。
- 设备不支持有效 timestamp 时降级为 CPU-only profile。
- swapchain recreate 或 frame resource rebuild 时，query pool 生命周期必须和 frame resource 一起闭合。
- 不允许为了 profiler 当前帧数据调用 `vkDeviceWaitIdle`、`vkQueueWaitIdle` 或同步 map/read 阻塞渲染主路径。

推荐 GPU scope：

- `Frame`
- `AcquireWait` 不放 GPU timestamp，只保留 CPU scope。
- `RenderGraphTransition`
- `Pass/<pass.name>`
- `Present` 不放 GPU timestamp，只保留 CPU scope。

## Debug label 技术细节

`VK_EXT_debug_utils` 用于外部工具可读性，不作为性能数据源本身。

建议接入：

- `vkSetDebugUtilsObjectNameEXT`：image、image view、buffer、pipeline、descriptor set layout。
- `vkCmdBeginDebugUtilsLabelEXT` / `vkCmdEndDebugUtilsLabelEXT`：compiled pass 和 backend transition group。
- label 名称从 RenderGraph pass name/type、image name 和 view name 派生。

label 示例：

```text
Game/MainView/Pass/SceneColorClear
Game/MainView/Pass/FullscreenComposite
Game/MainView/Barrier/SceneColor ShaderRead(fragment)
```

## 后端缓存指标

P4 接入 caches 时，同步增加 counters，避免优化无法量化：

| 模块 | Counter |
| --- | --- |
| Deferred deletion | pending objects、retired objects、oldest fence epoch |
| Descriptor allocator | sets allocated、pool count、pool reset count |
| Pipeline layout cache | lookup count、hit count、miss count、create count |
| Pipeline cache | lookup count、hit count、miss count、create milliseconds |
| Transient resource pool | image request count、reuse count、create count、live bytes estimate |
| RenderGraph compiler | pass/resource/dependency/transition/culled counts、compile milliseconds |

这些 counter 先进入 benchmark 输出，未来再进入 editor 面板。

## 编辑器性能面板技术细节

编辑器尚未纳入当前开发计划，因此这里只定义未来接入条件。

接入前置条件：

- 已有 `RenderView` 或等价 view 描述。
- 已能区分 Game View、Scene View、Preview View 和 Bench。
- 已有 PlaySession 或明确的 game runtime 边界。
- profiling 数据通过 snapshot/ring buffer API 暴露，而不是 UI 直接读取 renderer 内部对象。

面板范围：

- 默认只分析 Game/Play target。
- Scene/Preview 只能作为额外 target 选择。
- EditorHost CPU、dock/layout、asset import、inspector 等编辑器自身性能不放入 Game Performance panel。
- 未来如需要分析编辑器整体性能，单独做 Editor Diagnostics 面板。

第一版面板只消费已有数据：

- frame time p50/p95/max
- CPU timeline
- GPU timeline
- RenderGraph pass list
- slowest pass
- compile graph cost
- transient resource 和 cache counters
- 一键导出当前 ring buffer JSONL
- 一键触发外部 capture 的启动脚本，具体工具不成为 runtime 依赖

## 外部工具策略

外部工具用于定位问题，不作为引擎内置数据模型的替代品。

建议分工：

- Vulkan validation / synchronization validation：验证同步、layout、access 和 lifetime。
- RenderDoc：检查 frame capture、resource history、debug labels 和 pipeline state。
- Nsight Graphics：检查 GPU workload、barrier、draw/dispatch 和 GPU trace。
- Windows Performance Recorder / Windows Performance Analyzer：检查 CPU 调度、线程等待、驱动调用和系统层开销。
- `nvidia-smi` 或厂商监控工具：只做粗粒度 GPU 占用、显存和温度观察。

## 资料依据

- Unity Profiler window：https://docs.unity3d.com/Manual/ProfilerWindow.html
- Unity profiling applications：https://docs.unity3d.com/Manual/profiler-profiling-applications.html
- Vulkan `vkCmdWriteTimestamp2`：https://registry.khronos.org/vulkan/specs/latest/man/html/vkCmdWriteTimestamp2.html
- Vulkan timestamp queries：https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#queries-timestamps
- Khronos `VK_EXT_debug_utils` guide：https://docs.vulkan.org/guide/latest/extensions/VK_EXT_debug_utils.html
- RenderDoc documentation：https://renderdoc.org/docs/
- NVIDIA Nsight Graphics documentation：https://docs.nvidia.com/nsight-graphics/
- Windows Performance Recorder：https://learn.microsoft.com/windows-hardware/test/wpt/windows-performance-recorder
- Windows Performance Analyzer：https://learn.microsoft.com/windows-hardware/test/wpt/windows-performance-analyzer

## Definition of Done

P3.5 完成标准：

- `--bench-rendergraph` 可在 Release preset 下运行，并输出稳定 JSONL/CSV。
- benchmark 支持 warmup 和 measured frame count。
- `--smoke-*` 行为不变。
- `rendergraph` 不依赖 Vulkan、window 或 platform profiler。
- 文档记录 CPU/GPU/editor 面板分层边界。
- 没有新增 editor UI、GPU query pool 或外部 capture runtime 依赖。

P4 profiling 扩展完成标准：

- deferred deletion、descriptor allocator、pipeline cache、transient pool 均输出基础 counters。
- GPU timestamp readback 不阻塞当前帧。
- RenderDoc/Nsight capture 中能看到 RenderGraph pass label。
- resize/recreate 后 query pool、debug names 和 frame resources 生命周期无 validation warning。
