## Project 跟踪

- 关联 Issue: #
- [ ] 关联 Issue 已加入 Engine Project。
- [ ] PR 粒度工作已有一个 primary `[Slice]` issue；独立 XS 维护工作已说明 standalone 原因。
- [ ] 跨系统影响使用 Related Epics 或原生 dependency，没有把一个 Slice 挂到多个 parents。
- [ ] Project `Status`、`Priority`、`Size` 已同步。
- [ ] 验证完成后，本 PR 会关闭或更新关联 Issue。

## 完成证据

- [ ] Closing PR / commit 已写入关联 Issue。
- [ ] 验证门禁结果已写入 PR 或关联 Issue。
- [ ] 文档更新已完成，或无需更新的原因已说明。
- [ ] Follow-up / blocking issue 已创建；没有扩大原 Issue scope。

## 变更范围

- [ ] Vulkan / RHI / frame loop / swapchain
- [ ] RenderGraph / renderer / shader pipeline
- [ ] package / CMake / Conan / build scripts
- [ ] docs / workflow / tooling only

## 文档同步

- [ ] 已同步相关文档。
- [ ] 本次无需文档更新，原因：

需要优先检查的文档：

- `docs/architecture/flow.md`
- `docs/planning/next-development-plan.md`
- `docs/rendergraph/mvp.md`
- `docs/rendergraph/rhi-boundary.md`
- `docs/architecture/package-first.md`
- `docs/workflow/review.md`

## 验证

- [ ] `powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1`
- [ ] `powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1`
- [ ] `git diff --check`
- [ ] `clangcl-debug` 构建
- [ ] `msvc-debug` 构建
- [ ] 相关 smoke 命令

## 设计审查

- [ ] RenderGraph 仍保持后端无关。
- [ ] Vulkan layout / stage / access / barrier 没有扩散到非 Vulkan 层。
- [ ] `renderer-basic` 与 `renderer-basic-vulkan` 分层仍清楚。
- [ ] CMake target 依赖、package 边界和 include 可见性一致。
