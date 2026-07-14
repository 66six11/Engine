# 构建流程

本项目使用 Conan 2 + CMake Presets。日常开发以 MSVC 预设为主，代码检查以 ClangCL 预设为主；四个入口全部使用 Ninja 生成器。

仓库 Python 工具依赖单独安装：

```powershell
python -m pip install -r tools\requirements.txt
```

## 预设约定

- `msvc-debug`：日常 Debug 构建。
- `msvc-release`：日常 Release 构建。
- `clangcl-debug`：Debug 代码检查构建，启用 `clang-tidy`。
- `clangcl-release`：Release 代码检查构建，启用 `clang-tidy`。

Visual Studio 中直接选择这四个项目级 preset 即可。`msvc-*` 负责常规编译、IntelliSense 和构建；`clangcl-*` 用作第二编译器验证和 `clang-tidy` 检查。

## 目录约定

- `build/conan/*`：Conan 生成的 toolchain、依赖配置和环境脚本。
- `build/cmake/*`：CMake/Visual Studio/Ninja 的实际构建目录。

这两个目录必须分开。Visual Studio 删除 CMake cache 时可能会清理 `build/cmake/*`，但不应该删除 `build/conan/*`，否则会再次出现 `Could not find toolchain file`。

## 生成 Conan 文件

首次构建、清理 `build` 目录后，或者依赖 profile 改动后，先生成 Conan toolchain 和依赖配置：

```powershell
.\scripts\bootstrap-conan.ps1
```

这些命令会生成本地文件，例如：

- `build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake`
- `build/conan/msvc-release/Release/generators/conan_toolchain.cmake`
- `build/conan/clangcl-debug/Debug/generators/conan_toolchain.cmake`
- `build/conan/clangcl-release/Release/generators/conan_toolchain.cmake`
- `ConanPresets.json`

这些都是本地生成物，不提交到版本库，也不要手动编辑。

仓库提交 `conan.lock` 作为依赖 recipe revision 锁定文件。`bootstrap-conan.ps1` 在检测到
`conan.lock` 时会自动把它传给 `conan install`，避免 `glfw`、`glm`、`vulkan-headers`、
`vulkan-memory-allocator` 等依赖漂移。任一 profile 的 `conan install` 失败时，bootstrap 会立即
停止并返回同一个非零退出码，不会继续运行后续 profile 或报告 toolchain 已就绪。

## 构建命令

在 Visual Studio 的 CMake 集成中，选择对应 preset 后配置和构建即可。

如果在普通 PowerShell 中构建 Ninja + MSVC/ClangCL，需要先加载 Conan 生成的 VS 编译环境：

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
cmd /c "build\conan\msvc-release\Release\generators\conanbuild.bat && cmake --preset msvc-release && cmake --build --preset msvc-release"
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\clangcl-release\Release\generators\conanbuild.bat && cmake --preset clangcl-release && cmake --build --preset clangcl-release"
```

## Native Test Presets

CMake 之前必须先 bootstrap Conan。`msvc-debug-tests` 和 `clangcl-debug-tests` 分别使用
`build/cmake/msvc-debug-tests` 和 `build/cmake/clangcl-debug-tests`，并设置 `ASHARIA_BUILD_TESTS=ON`。

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug-tests && cmake --build --preset msvc-debug-tests && ctest --preset msvc-debug-tests --output-on-failure"
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug-tests && cmake --build --preset clangcl-debug-tests && ctest --preset clangcl-debug-tests --output-on-failure"
```

ClangCL test preset 对 production 和 test translation units 启用 clang-tidy，且所有 clang-tidy
diagnostics 都作为 errors 处理。

`.github/workflows/native-code-quality.yml` 在 pull request、push to `main` 和 manual dispatch 时运行。
Windows hosted job 固定使用包含 Visual Studio 2022 的 `windows-2022` runner；仓库 Conan profiles
要求 Visual Studio 17，因此不得依赖会迁移到更新 Visual Studio 主版本的 `windows-latest`。Job 先安装锁定版本的 Conan/Vulkan SDK、bootstrap Conan，再运行 encoding、diff
whitespace、asset boundary、两编译器 build 和 CTest；ClangCL hosted build 使用 `--parallel 2`，限制并发 clang-tidy 的内存峰值。Hosted CI 不运行 GPU/window smokes；相关本地
pre-commit smoke gate 以 `docs/workflow/review.md` 为准。

也可以从 “Developer PowerShell for VS 2022” 进入项目目录后运行 `cmake --preset ...` 和 `cmake --build --preset ...`。

## 日常建议

- 平时开发优先使用 `msvc-debug`。
- 提交前至少跑一次 `clangcl-debug`，让 ClangCL 和 `clang-tidy` 帮我们抓 MSVC 不容易暴露的问题。
- 做发布或性能验证时使用 `msvc-release`。
- 需要更严格检查发布配置时再跑 `clangcl-release`。

## 运行方式

无参数启动 sample viewer 会进入正常交互式运行状态，打开窗口并持续渲染 triangle：

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe
```

需要自动验证时使用 `--smoke-*` 入口。完整提交前 smoke 清单见 `docs/workflow/review.md`。

根构建也会生成开发期工具。当前 `tools/asset-processor` 提供 read-only dry-run 和受控 product
execution baseline；`execute` 可为 PNG Texture2D request 写 deterministic texture product blob/manifest，
其他 product request 仍走 placeholder blob baseline。它不接 watcher、dependency invalidation、GPU upload 或
editor UI：

```powershell
build\cmake\msvc-debug\tools\asset-processor\asharia-asset-processor.exe --smoke-dry-run
build\cmake\msvc-debug\tools\asset-processor\asharia-asset-processor.exe --smoke-product-execution
build\cmake\msvc-debug\tools\asset-processor\asharia-asset-processor.exe dry-run --source-root Content --source-path-prefix Content --target-profile windows-msvc-debug
build\cmake\msvc-debug\tools\asset-processor\asharia-asset-processor.exe dry-run --project asharia.project.json --target-profile windows-msvc-debug
build\cmake\msvc-debug\tools\asset-processor\asharia-asset-processor.exe execute --source-root Content --source-path-prefix Content --target-profile windows-msvc-debug --output-root build\asset-cache
```

## 仓库维护工具

这些脚本不替代构建门禁，但用于本地自检和变更审查：

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1
powershell -ExecutionPolicy Bypass -File tools\check-asset-boundaries.ps1
python tools\check_package_topology.py
python tools\check_package_contracts.py
python -m unittest discover -s tools\tests -p "test_*.py"
powershell -ExecutionPolicy Bypass -File tools\count-code-lines.ps1
```

- `check-text-encoding.ps1` 验证 C/C++ 源码 UTF-8 with BOM、其他文本 UTF-8 without BOM。
- `check-doc-sync.ps1` 在 code/build/tooling 变更缺少文档同步时失败；临时验证未跟踪文件时可加 `-IncludeUntracked`。
- `check-asset-boundaries.ps1` 验证 `asset-core` 没有重新引入 texture profile/importer 解释或 `asset-pipeline`
  依赖。
- `check_package_topology.py` 验证全部 source-boundary manifests 的 identity、dependency DAG、target owner/role、
  target dependency keys 和直接 CMake target 声明；需要机器快照时使用
  `--output build/package-topology.json`，不要提交该生成文件。
- `check_package_contracts.py` 使用 Draft 2020-12 schema、显式 discriminator dispatcher 和跨字段 semantic rules 验证
  installable v2、Feature Set v2、Project Manifest v1、Package Lockfile v1、Host Profile v1、Package Source Build v1、
  Package Product Declaration v1、Package Artifact Manifest v1、Source Topology Snapshot v1、CMake Codemodel Snapshot v1 与
  Source Build Plan v1 contracts；也可以显式传入一个或多个 fixture/manifest 路径。
- `tools/tests/test_package_topology.py` 覆盖正常 inventory、missing dependency、cycle、duplicate identity、
  catalog 泄漏和未声明 CMake target 等负向路径。
- `tools/tests/test_package_contracts.py` 覆盖 portable v2 system/integration、封闭 schema、引用、module cycle、
  catalog policy 和 deterministic diagnostics。
- `tools/tests/test_package_project_contracts.py` 覆盖 Project Manifest、Feature Set、dispatcher isolation、selected graph cycle
  与 normalized writer determinism/encoding。
- `tools/tests/test_package_lock_contracts.py` 覆盖 exact graph closure、source/integrity、cross-document selected-result validation、
  package tree digest 与 normalized lock writer determinism。
- `tools/tests/test_package_resolver.py` 覆盖纯内存 candidate validation、最高兼容版本、稳定回溯、嵌套 Feature Set、
  prerelease/engine API、requirement chain、source ambiguity、cycle、输入不变性与 canonical lock byte determinism。
- `tools/tests/test_package_candidate_discovery.py` 覆盖三类显式来源、containment、source/physical alias、原子失败、
  payload tree 限制、TOCTOU 与 resolver/lock validator 合成交接。
- `tools/tests/test_package_lock_verification.py` 覆盖 existing lock 成功复用、stale inputs、exact source binding、
  cross-document drift、selected payload 重哈希、原子失败、排列确定性以及 no-resolver/no-write 边界。
- `tools/tests/test_host_profile_contracts.py` 覆盖五个固定 Host policies、normalized writer、module/contribution filtering、
  capability grants 与 platform/role/shipping closure rejection。
- `tools/tests/test_package_product_contracts.py` 覆盖 Product Declaration exact binding、closed fields、module/product uniqueness、
  canonical normalization、Artifact Manifest portable paths 与 candidate/locked snapshot drift。
- `tools/tests/test_package_artifact_evidence.py` 覆盖 pure per-package verifier 的 coverage、portable path/size/SHA-256、
  stale provenance、determinism、immutability、no-IO 与 Discovery → Source Build Plan synthetic handoff；同时覆盖 #278
  collector 的大文件分块 copy/rehash、closed roots、link/reparse、source drift、single-rename publication、失败清理、
  content-addressed generation 复用与 corrupt existing generation 拒绝。collector 是 build/install/cache evidence 边界，
  不执行 CMake/Conan，也不参与 Editor Bootstrap 或每次源码编辑。
- `count-code-lines.ps1` 只统计 Git tracked 文本文件，默认排除 Markdown；需要把文档纳入统计时加 `-IncludeDocs`。
