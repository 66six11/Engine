# 构建流程

本项目使用 Conan 2 + CMake Presets。日常开发与 hosted CI 以 MSVC 预设为主；ClangCL 保留为本地按需的第二编译器。四个入口全部使用 Ninja 生成器。

仓库 Python 工具依赖单独安装：

```powershell
python -m pip install -r tools\requirements.txt
```

## 预设约定

- `msvc-debug`：日常 Debug 构建。
- `msvc-release`：日常 Release 构建。
- `clangcl-debug`：Debug 第二编译器构建，生成 compilation database。
- `clangcl-release`：Release 第二编译器构建，生成 compilation database。

Visual Studio 中直接选择这四个项目级 preset 即可。`msvc-*` 负责常规编译、IntelliSense、hosted CI 和默认验证；
`clangcl-*` 只负责显式的本地第二编译器验证。`clang-tidy` 不挂在编译动作上，而是读取 MSVC test preset 生成的
`compile_commands.json` 独立运行。

## 目录约定

- `build/conan/*`：Conan 生成的 toolchain、依赖配置和环境脚本。
- `build/cmake/*`：CMake/Visual Studio/Ninja 的实际构建目录。

这两个目录必须分开。Visual Studio 删除 CMake cache 时可能会清理 `build/cmake/*`，但不应该删除 `build/conan/*`，否则会再次出现 `Could not find toolchain file`。

## 生成 Conan 文件

首次构建、清理 `build` 目录后，或者依赖 profile 改动后，先生成 Conan toolchain 和依赖配置：

```powershell
.\scripts\bootstrap-conan.ps1
```

该命令默认准备四个仓库 profile。Hosted CI 只执行 MSVC Debug，因此使用
`powershell -ExecutionPolicy Bypass -File scripts\bootstrap-conan.ps1 -Profiles windows-msvc-debug`，不安装或构建 ClangCL profile 的依赖。

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

### Studio native-first 构建与本地 ABI admission

`dotnet build apps\studio\Editor.csproj` 不运行 Conan 或 CMake，也不隐式构建 native target。它只按
`StudioNativeBuildPreset` 从已存在的 `build/cmake/<preset>` 消费 Project、Scene、editor-content 与 Viewport
native artifacts。因此首次构建、切换分支，或修改 `apps/editor`、Viewport ABI、renderer shader closure 后，必须先重建
`editor-native`，再构建 Studio：

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build build\cmake\msvc-debug --target editor-native"
dotnet build apps\studio\Editor.csproj -c Debug -p:StudioNativeBuildPreset=msvc-debug
```

Release 使用对应 preset，不得让 Release managed output 借用 Debug DLL：

```powershell
cmd /c "build\conan\msvc-release\Release\generators\conanbuild.bat && cmake --preset msvc-release && cmake --build build\cmake\msvc-release --target editor-native"
dotnet build apps\studio\Editor.csproj -c Release -p:StudioNativeBuildPreset=msvc-release
```

`Editor.csproj` 对 `editor_native.dll` 使用 `CopyToOutputDirectory="Always"` 与
`CopyToPublishDirectory="Always"`，所以每次 managed build 都从所选 preset 覆盖最终 sibling，而不按目标目录中的旧时间戳保留
陈旧 DLL。复制完成后，`ValidateStudioViewportNativeRuntimeContract` 在非 design-time `Build` 的
`AfterTargets` 阶段从 `$(TargetDir)` 执行 `Editor.exe --verify-native-contract`。该入口通过
`ViewportNativeRuntimeContract` 加载最终 `$(TargetDir)\editor_native.dll`，要求完整 V10 production export set，且拒绝固定的
legacy V1--V9 entry-point set；缺失 DLL、错误架构、缺失 V10 export 或仍存在 legacy export 都使 build 以非零状态失败。

显式 `msvc-debug-tests` preset 可以额外携带 `editor_viewport_open_stream_v10_for_test`，供 GPU/fault-injection 验收使用；它不属于
managed production imports。普通本地 admission 允许该 test-only 扩展，正式 Editor Image 的静态 PE gate 则拒绝它。

这个 admission 只验证普通 Studio build 最终会实际加载的 sibling，不会重建 native，也不提供 V1--V9 fallback。若失败，修复方式是
重建所选 preset 的 `editor-native` 后重新运行 managed build；不要手工从历史 `bin/`、测试输出或其他 preset 复制 DLL。
正式发行仍由 `StudioEditorImageProducer` 的静态 PE/DLL identity 与 export inspector 对全新 publish tree 独立复验，不能用本地
`--verify-native-contract` 成功替代 Editor Image qualification。

## Native Test Presets

CMake 之前必须先 bootstrap Conan。`msvc-debug-tests` 和 `clangcl-debug-tests` 分别使用
`build/cmake/msvc-debug-tests` 和 `build/cmake/clangcl-debug-tests`，并设置 `ASHARIA_BUILD_TESTS=ON`。

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug-tests && cmake --build --preset msvc-debug-tests && ctest --preset msvc-debug-tests --output-on-failure"
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug-tests && cmake --build --preset clangcl-debug-tests && ctest --preset clangcl-debug-tests --output-on-failure"
```

## 独立 clang-tidy 入口

MSVC test preset 配置完成后，只检查当前改动直接命中的 translation units：

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && python tools\run_clang_tidy.py --build-dir build\cmake\msvc-debug-tests --changed --include-untracked"
```

该入口只消费 compilation database 中属于当前 source root、且不位于 `build/` 的 translation units。
changed `.cc/.cpp/.cxx` 必须精确存在于 database，否则 fail closed。C 源文件继续由编译 gate 检查；
头文件、`.clang-tidy`、CMake、Conan 或 profile 不是 translation unit，不会扩大 tidy 选择；只修改这些文件时 tidy 成功跳过，
由 MSVC build/CTest 和仓库合同检查提供覆盖。无 `--changed` 的全量模式保留为显式诊断入口，但不属于默认提交或 CI 门禁。
本地默认最多并行八个 tidy 进程，可通过 `--jobs` 显式调整；CI 固定为两个并发以限制 hosted runner 的内存峰值。
`.clang-tidy` 继续把所有 diagnostics 作为 errors。

这一拆分采用 LLVM 官方 `run-clang-tidy` + compilation database 路径：

- CMake 的 [`CMAKE_EXPORT_COMPILE_COMMANDS`](https://cmake.org/cmake/help/latest/variable/CMAKE_EXPORT_COMPILE_COMMANDS.html)
  为 Ninja build 生成每个 translation unit 的精确编译命令；
- LLVM 的 [clang-tidy automation](https://clang.llvm.org/extra/clang-tidy/index.html#clang-tidy-automation)
  明确支持在 build graph 外并行分析 compilation database；
- 不采用 `clang-tidy-diff` 作为提速路径，因为 LLVM 明确说明它仍分析整个文件、只过滤最终诊断，且可能漏掉落在未改行上的影响。

`.github/workflows/native-code-quality.yml` 在 pull request、push to `main` 和 manual dispatch 时运行。
Windows hosted job 固定使用包含 Visual Studio 2022 的 `windows-2022` runner；仓库 Conan profiles
要求 Visual Studio 17，因此不得依赖会迁移到更新 Visual Studio 主版本的 `windows-latest`。Job 先安装锁定版本的 Conan/Vulkan SDK、只 bootstrap `windows-msvc-debug`，再运行 encoding、diff
whitespace、asset boundary、MSVC build 和 CTest；随后 CI 从同一个 MSVC test compilation database 中，以两个并发进程只分析
相对 PR base、push range 或手动触发时最后一个提交发生变化的 translation units。CI 不构建 ClangCL，编译失败和静态检查失败仍可分别归因。Hosted CI 不运行 GPU/window smokes；相关本地
pre-commit smoke gate 以 `docs/workflow/review.md` 为准。

也可以从 “Developer PowerShell for VS 2022” 进入项目目录后运行 `cmake --preset ...` 和 `cmake --build --preset ...`。

## 日常建议

- 平时开发优先使用 `msvc-debug`。
- 提交前运行 MSVC test gate 和 changed tidy；仅在高风险编译器兼容性、frame/swapchain/render-graph 或专项验证中按需运行 `clangcl-debug`。
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

### Studio Distribution 输入物化

固定 Studio 的发行输入不再由测试手写 metadata，也不由 Python 构建路径生成。当前 v1 是 Windows x64
release contract。使用标准 `dotnet publish` 与 `EditorImage.pubxml` 生成 release-orchestrator-owned、全新且不复用的
Studio publish 目录；`dotnet publish` 不负责清理旧 `PublishDir`。当前 Studio 的 Project、SceneDocument 与 Scene View
都有 native consumer，因此必须先按本文规则运行 Conan，并构建所选 `msvc-release` preset。publish 精确复制
`asharia_project_native.dll`、`asharia_scene_native.dll`、`editor_native.dll` 与 22 个 renderer-basic shader/reflection 文件；
仍不复制 `slang.dll`、Vulkan SDK 或 validation layer。Scene schema v2、Document ABI v3 与 Viewport V10 是 hard-cut
native consumer contract；发行验证必须拒绝遗留 v1/v2 document 与 v1--v9 viewport exports。shader/reflection closure 必须仍为
精确 22 个文件，不能因 Scene mesh / Frame Debug / selection outline 改动少复制、重复复制或以旧 shader 代替。
完整可复制命令、required file set、参数、输出布局、receipt 与失败恢复见
`tools/studio-distribution/README.md`。

```powershell
$releaseRoot = 'D:\Build\Asharia'
New-Item -ItemType Directory -Force -Path $releaseRoot | Out-Null
$publishRoot = Join-Path $releaseRoot ("studio-publish-" + [guid]::NewGuid().ToString('N'))
$sdkVersion = '10.0.302'
$hostFxrVersion = '10.0.10'
$hostRuntimeVersion = '10.0.10'

if ((dotnet --version).Trim() -ne $sdkVersion) {
  throw "Repository global.json did not select the required .NET SDK $sdkVersion."
}

dotnet publish apps\studio\Editor.csproj `
  -c Release `
  -p:PublishProfile=EditorImage `
  -p:PublishDir="$publishRoot\"

dotnet run --project tools\studio-distribution\Asharia.Studio.Distribution.csproj `
  -c Release -- `
  stage-editor-image `
  --publish-root $publishRoot `
  --entry-point Editor.exe `
  --dotnet-root "C:\Program Files\dotnet" `
  --sdk-version $sdkVersion `
  --hostfxr-version $hostFxrVersion `
  --host-runtime-version $hostRuntimeVersion `
  --output-root D:\Build\Asharia\editor-image

dotnet run --project tools\studio-distribution\Asharia.Studio.Distribution.csproj `
  -c Release -- `
  stage-editor-host-profile `
  --output-root D:\Build\Asharia\editor-host-profile
```

focused functional tests 为：

```powershell
dotnet test tools\studio-distribution.Tests\Asharia.Studio.Distribution.Tests.csproj -c Release
python -m unittest tools.tests.test_host_profile_contracts
```

Editor Image receipt 与 Host Profile receipt 都只是 canonical Distribution assembler 的 typed inputs。
Editor Image producer 要求 release orchestration 显式钉住 SDK apphost template、hostfxr 与 host runtime；SDK template
只用于构建期 apphost byte qualification，不进入产品树。最终 bundled runtime 只有完整的
`host/fxr/<version>` 与 `shared/Microsoft.NETCore.App/<version>`，不携带无 runtime reader 的 `dotnet.exe`、SDK 或 reference pack。
producer 静态核对 apphost binding、managed identity/runtime evidence、`hostfxr.dll` direct exports 与关键 runtime version evidence；
apphost 的 `.rsrc` 必须是 fixed `Editor.dll` 资源经 .NET 10 HostModel 规则重建出的 exact canonical bytes。
通过资格检查后再复制并逐字节绑定所选树。该过程不执行候选 EXE、不加载 DLL、不调用 hostfxr，也不证明 ABI 或 runtime health。
Python 只属于仓库内开发、验证与 CI 工具层；正式 Studio、Editor Image、Host Profile、package artifact、Engine Distribution、Launcher、Installer、Repair 与用户运行时均不得依赖或携带 Python。Editor Image producer 会拒绝所选 publish/.NET 树中的常规 Python 源码、字节码、wheel/extension、虚拟环境/package 目录与解释器/runtime artifact；Package Artifact、canonical assembly 与 installed health/repair 三个下游边界会独立重复同一逻辑路径政策。旧 manifest/receipt 的 schema、hash 与 generation ID 即使自洽也不能取得产品资格。这里运行的 Python 命令只是仓库 reference-oracle/CI 门禁，不是正式发行流程的运行依赖。
两份 receipt 都不选择 installable packages，不生成或暗示 `EngineGenerationId`，也不证明完整 Distribution health。
真实 installable package artifacts、canonical assembly invocation、installed-generation byte-health handoff 与
launcher-owned current selection 仍是 downstream work。

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
  installable v2、Feature Set v2、Project Manifest v2、Package Lockfile v2、Host Profile v1、Package Source Build v1、
  Package Product Declaration v1、Package Artifact Manifest v1、Engine Distribution Manifest v1、Source Topology Snapshot v1、
  CMake Codemodel Snapshot v1 与 Source Build Plan v1 contracts；也可以显式传入一个或多个 fixture/manifest 路径。
- `tools/tests/test_package_topology.py` 覆盖正常 inventory、missing dependency、cycle、duplicate identity、
  catalog 泄漏和未声明 CMake target 等负向路径。
- `tools/tests/test_package_contracts.py` 覆盖 portable v2 system/integration、封闭 schema、引用、module cycle、
  catalog policy 和 deterministic diagnostics。
- `tools/tests/test_package_project_contracts.py` 覆盖 Project Manifest、Feature Set、dispatcher isolation、selected graph cycle
  与 normalized writer determinism/encoding。
- `tools/tests/test_engine_distribution_contracts.py` 覆盖 closed Engine Distribution schema、内容派生
  `EngineGenerationId`、Editor/package/artifact/profile invariants、portable paths、discovery 与 canonical writer。
- `tools/tests/test_engine_distribution_assembly.py` 覆盖 #282 assembler 的显式隔离输入、staged-byte inventory、
  receipt 深度复验、大文件流式复制、source/staging drift、single-rename publication、确定性复用、失败清理与 corrupt existing
  generation no-overwrite。assembler 不执行 CMake/Conan，不实现 installed Repair/Launcher/Activation。
- `tools/tests/test_engine_distribution_repair_verifier.py` 覆盖 #283 的外部 expected generation trust anchor、
  canonical Distribution Manifest bootstrap、disk-only artifact generation reconstruction、Editor/package/artifact/profile/closed-tree
  故障注入、稳定多 finding、bounded streaming，以及成功/失败路径只读保证。verifier 不执行 repair、active selection、
  Bootstrap/Session integration 或 Activation。
- `tools/tests/test_engine_distribution_package_catalog.py` 覆盖 #301 verified handoff 捕获、bundled inventory 排列确定性、
  duplicate identity/root、strict-loader source failure/mutation、exact candidate evidence mismatch、snapshot 隔离，以及
  `catalog -> resolver -> canonical Lock v2 -> locked verify/reuse` 的无 existing Lock headless 链。catalog 不持久化第二份 inventory，
  不实现 Project/local index、Lock update/apply 或 UI。
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
