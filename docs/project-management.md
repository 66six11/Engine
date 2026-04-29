# 项目管理

## 仓库布局

当前仓库布局：

```text
VkEngine/
  CMakeLists.txt
  CMakePresets.json
  conanfile.py
  profiles/
    windows-msvc-debug
    windows-msvc-release
  docs/
  apps/
    sample-viewer/
  engine/
    core/
    platform/
  packages/
    window-glfw/
    rhi-vulkan/
    rendergraph/
    renderer-basic/
    shader-slang/
  tools/
```

后续可能新增 `apps/editor/`、`tests/`、`package-registry/`、`packages/asset-core/`、
`packages/editor-core/` 等目录，但新增前应先明确 package 边界和 CMake target 关系。

## 变更策略

- 每个变更绑定一个里程碑。
- 首帧跑通前避免大规模重构。
- 优先维护 package 边界，避免把 runtime、editor、renderer 全塞进一个 app。
- 构建文件、依赖文件和 renderer 代码如果互相影响，应一起审核。
- 构建目录、Conan 输出目录、生成的 toolchain/preset 不提交。

## 里程碑门禁

- Gate 0 Scope：目标平台、Vulkan 版本、依赖和 validation 方案明确。
- Gate 1 Research：版本敏感结论有一手资料支撑。
- Gate 2 Design：生命周期、同步、内存分配、错误路径明确。
- Gate 3 Implementation：改动窄，符合 C++23/Vulkan 规范。
- Gate 4 Validation：build、shader validation、Vulkan validation 已运行或明确阻塞原因。
- Gate 5 Review：记录 findings、风险和后续任务。

## 初始任务拆分

1. 创建 CMake、Conan、preset、dependency profile 骨架。[x]
2. 建立 `apps/`、`engine/`、`packages/` 的 package-first 目录和 CMake target 边界。[x]
3. 添加 `engine/core` 日志、错误、断言工具。[x]
4. 添加 `packages/window-glfw` 的 GLFW window 和 Vulkan surface。[x]
5. 添加 `packages/rhi-vulkan` 的 Vulkan instance/device/queue/allocator。[x]
6. 添加 swapchain 和 frame pacing。[x]
7. 添加 `packages/rendergraph` builder/compiler 骨架。[x]
8. 添加 clear pass。[x]
9. 添加 `packages/shader-slang` shader build 路径和 triangle pass。[x]
10. 添加基础 resize/recreate 路径和专项 smoke。[x]
11. 添加 validation checklist 和 smoke test 文档。[x]

## 第一版引擎 Definition Of Done

- 开发者能根据文档 configure、build、run。
- 窗口能通过 render graph execution 呈现 clear color 或 triangle。
- validation layer 在普通 startup-frame-shutdown 路径没有已知 error。
- resource lifetime 和 synchronization 决策在代码和日志中可见。
- shader 语言决策已确认，或已改为可工作的编译链路。

## 风险表

- Slang 工具链集成：Slang 支持 Vulkan SPIR-V，但 compiler 获取、版本 pin 和 Conan/CMake
  集成需要明确。
  缓解：固定 `slangc` 版本，记录命令行，shader 输出统一走 `spirv-val`。
- Vulkan 1.4 可用性：本机驱动可能不暴露 Vulkan 1.4。
  缓解：请求 1.4，输出能力报告，再决定 fail fast 或支持 1.3 fallback。
- 同步复杂度：render graph barrier 很容易细节错误。
  缓解：首版单 queue、只用 synchronization2、开启详细 debug log 和 sync validation。
- Swapchain resize：自动 smoke 已覆盖 zero extent 和主动 recreate；真实交互 resize/minimize
  仍可能出现连续 out-of-date surface。
  缓解：保留 `--smoke-resize`，后续接入窗口 resize 事件和交互验证记录。
- 依赖漂移：未 pin 的 Conan 依赖可能改变行为。
  缓解：首次 bootstrap 成功后生成 lockfile。
- 包边界膨胀：为了快速跑通，代码容易滑向 monolithic app。
  缓解：从第一版 CMake target 开始按 `apps/engine/packages` 分层，app 只组合 package。
