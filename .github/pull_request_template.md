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
