# 审查流程规范

本文档定义每次代码审查、修复和提交前必须执行的验证门禁。架构准入、Owner Card、最早/最迟接入窗口和
Integration Gates 由 [`architecture-health.md`](architecture-health.md) 统一定义；本文不再用零散专项检查替代架构准入。
目标是让代码正确性、内部代码设计、Vulkan 同步安全、包边界、文档同步和下一步开发判断保持一致。

## 适用范围

- 用户要求“审查代码”“审查并提交”“再次审查”时，必须执行本文档。
- 用户要求“架构审查”“代码架构审查”或“内部设计审查”时，内部代码设计审查是必选项，不能只检查 package、target、include 或 Vulkan/RHI 边界。
- 用户给出 review findings 时，先判断每条 finding 是否仍适用，再修复。
- 涉及 Vulkan、RenderGraph、renderer、shader、构建脚本或包依赖的改动，必须增加设计审查门禁；涉及 editor、renderer、runtime、RenderGraph 或 RHI 的改动，还必须执行内部代码设计审查门禁。

## 审查输出顺序

审查回复必须先列 findings，再列验证与总结。架构或代码审查回复还必须显式写出：

```text
设计审查：通过 / 未通过 / 不适用
内部设计审查：通过 / 未通过 / 不适用
参考资料：...
```

若内部设计审查为“不适用”，必须说明原因；只检查边界而没有检查内部对象职责、数据合同、生命周期和状态模型，不允许标为通过。

若发现问题：

1. 按 P1/P2/P3 标注优先级。
2. 给出文件和行号。
3. 说明风险、触发条件和建议修法。
4. 若用户要求修复，则修复后重新跑完整门禁。

若无问题：

1. 明确写“未发现新的阻塞问题”。
2. 列出已跑命令和结果。
3. 若提交成功，说明 commit hash。

架构、系统、public contract、owner/lifecycle/thread、持久化格式、package/target、Studio/native 边界或并发变化，
在开始编码前还必须记录：

```text
Current evidence: ...
Owner / lifetime / thread: ...
Data / error / budget / diagnostics: ...
Foundation prerequisite: ...
Integration Gate: I0-I6
Earliest safe / latest required: ...
Non-goals / exit evidence: ...
```

缺少这些事实时先缩小 Slice 或补审查，不用未来扩展需求猜测抽象。

## 固定门禁

每次提交前必须执行：

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1
python -m unittest discover -s tools\tests -p "test_*.py"
python tools\check_package_topology.py
python tools\check_package_contracts.py
git diff --check
python tools\review-vulkan-cpp.py . --exclude apps/studio --exclude apps/editor/src/native_bridge --exclude-glob "apps/editor/src/editor_shared_viewport*" --fail-on warning
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug-tests && cmake --build --preset msvc-debug-tests && ctest --preset msvc-debug-tests --output-on-failure"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && python tools\run_clang_tidy.py --build-dir build\cmake\msvc-debug-tests --changed --include-untracked"
```

完整 native test gate 必须先 bootstrap Conan，然后运行 MSVC test tree 与 changed tidy：

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug-tests && cmake --build --preset msvc-debug-tests && ctest --preset msvc-debug-tests --output-on-failure"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && python tools\run_clang_tidy.py --build-dir build\cmake\msvc-debug-tests --changed --include-untracked"
```

MSVC build/test 与 clang-tidy 是两个独立 gate。后者读取 MSVC test preset 的 compilation database，只选择发生变化且存在于 database 的
`.cc/.cpp/.cxx`，并将所选 translation units 的所有 clang-tidy diagnostics 作为 error。头文件和构建输入不会触发全量 tidy；
只修改这些文件时由 MSVC build/CTest 与仓库合同检查覆盖。ClangCL 保留为下方高风险专项验证的本地按需 gate，不在 hosted CI 默认构建。

修改任意 `asharia.package.json`、`CMakeLists.txt`、`cmake/` helper 或 target graph 时，还必须在 configure 前准备由仓库拥有的
File API query，并用包含全部 test targets 的 configured graph 对证 manifest direct dependencies：

```powershell
python tools\check_target_dependency_truth.py --root . --prepare-query build\cmake\msvc-debug-tests
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug-tests"
$replyIndex = Get-ChildItem build\cmake\msvc-debug-tests\.cmake\api\v1\reply\index-*.json |
    Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName
python tools\check_target_dependency_truth.py --root . --reply-index $replyIndex --configuration Debug
```

该审计是 CMake 4.2+ / codemodel 2.9+ 的架构门禁，不改变仓库日常构建的 CMake 3.28 兼容下限。它精确对证 manifest
`targetDependencies`，只认六类 configured direct relation，并过滤 imported/generator targets；不得用旧 codemodel 的
transitive `dependencies` 字段替代，也不得把它误报成 package-level dependency 或 shipping closure gate。
`.github/workflows/native-code-quality.yml` 固定在包含 Visual Studio 2022 的 `windows-2022` hosted runner 上。所有变更先运行
encoding、diff whitespace、package topology、package/factory/product/artifact contracts 和 asset boundary；只有改动命中原生源码或
原生构建输入时，才安装 Conan/Vulkan SDK、只 bootstrap `windows-msvc-debug`，并运行 Vulkan package boundary/safety heuristic review、MSVC build 和 CTest。
原生构建输入包括 `engine/`、`packages/`、`apps/editor/`、`apps/sample-viewer/`、`tools/asset-processor/`、`shaders/`，
CMake/Conan/profile/bootstrap 配置、`.clang-tidy` 和该 workflow 本身；这些目录下的 Markdown、reStructuredText 与嵌套
`docs/` 仅视作文档，不触发编译。`workflow_dispatch` 始终执行完整原生构建。Package topology 从 source-boundary
manifests 生成 inventory 并对证直接 CMake target；Vulkan review 脚本只产生需要人工确认的保守提示；CI 以
`--fail-on warning` 阻止 warning/error，info 不阻塞。独立 tidy step 读取 MSVC test compilation database，并以两个并发进程
只分析相对事件基线变化的 translation units，避免静态分析超过 runner 内存。Hosted CI 不构建 ClangCL，也不运行 GPU/window smokes；
下方相关 smoke matrix 仍是 local high-risk gate，并且在其明确要求时使用两个 standard debug presets 运行。

涉及 Project Manifest/Lock、Engine Distribution、Host Profile、Effective Session、Host Composition、Source Build 或 package artifact
handoff 时，除全量 Python tests 外，开发中至少先运行以下 focused chain；提交前仍执行上面的完整门禁：

```powershell
dotnet test tools\studio-distribution.Tests\Asharia.Studio.Distribution.Tests.csproj -c Release
python -m unittest tools.tests.test_package_factory_contracts tools.tests.test_package_static_factory_bindings tools.tests.test_effective_session tools.tests.test_host_package_composition tools.tests.test_package_source_build_plan tools.tests.test_package_artifact_evidence tools.tests.test_engine_distribution_assembly tools.tests.test_engine_distribution_repair_verifier tools.tests.test_engine_distribution_package_catalog tools.tests.test_host_activation_blueprint tools.tests.test_static_composition_root
python -m unittest tools.tests.test_host_build_request tools.tests.test_host_cmake_target tools.tests.test_host_build_adapter tools.tests.test_host_executable_template tools.tests.test_host_generation_compatibility tools.tests.test_host_registration_snapshot tools.tests.test_host_registration_cross_verifier tools.tests.test_host_registration_verification tools.tests.test_host_binding_inputs tools.tests.test_host_executable_binding tools.tests.test_host_binding_assembly tools.tests.test_host_binding_publication
python -m unittest tools.tests.test_bootstrap_session tools.tests.test_bootstrap_project_inspection tools.tests.test_bootstrap_current_host tools.tests.test_bootstrap_project_host tools.tests.test_bootstrap_host_session
```

四个产品边界必须共同消费 `tools/tests/fixtures/product-boundaries/python-product-payload-v1.json`：Editor Image、Package
Artifact、Distribution Assembly 与 Installed Repair 对每个 forbidden path 都 fail closed；合法 Studio/.NET/native/package
control 的 policy match 数必须为零。旧的自洽 manifest/receipt 不是豁免，失败路径不得发布 partial generation、覆盖既有
generation 或修改 installed tree。`tools/*.py` 在这些命令中只作为仓库验证/reference oracle，不得被正式产品启动或携带。

Effective Session v1 只能产生 `Ready`、`UpgradeRequired`、`RepairRequired` 或 `SafeMode`；没有 artifact freshness 或
current-process generation evidence 的改动不得让 composer 猜测 `PendingBuild` / `PendingRestart`。#298 的
[Bootstrap Project-Open Session v1](../architecture/adr-bootstrap-project-open-session-v1.md) 在 composer 之外使用 verified
published Host/C6 与 path/type/size evidence 产生 `PendingBuild`；它只从一个 canonical project root 读取 Manifest/Lock、按 Lock
执行 fresh candidate discovery，不得 resolve、写 lock 或读取另一个 root。matching Host 必须由 binding 指向的 immutable publication
执行，normal-open 不重算 executable hash；`PendingRestart` 仍不可产生。
Package Factory Declaration v1 只声明 logical factory、owner scope、required factory 与 contribution ownership；不得加入
CMake target、artifact path、DLL symbol、作者自定义 phase/lifetime，或把 module/JSON 顺序解释为 activation order。
Host Activation Blueprint v1 只能从 Ready Session、匹配的 Host Composition 和 exact factory snapshots 派生固定的
scope/factory/contribution 顺序；不得加入 artifact path、DLL symbol、build command 或进程加载状态。
active Static Factory Provider Bindings/Binding Plan 已由 #295 硬切 schema/model v4，只能把 exact logical factory 映射到同 module Source
Build Descriptor 已拥有的
`STATIC_LIBRARY` target、`asharia/` public header 与受限 `asharia::...` provider function；不得使用全局注册发现、静态
constructor、运行时字符串 symbol lookup、package `src/` include，或把这些字段写回 portable Factory Declaration。provider API
必须为 v4；pre-v4 declaration/plan/adapter 不再接受。派生 Binding Plan 必须复验 Blueprint 与 Source Build Plan 的共同来源及
selected target，且不得成为第三份 lock。
Generated Static Composition Root v1 只能消费已经验证的 Source Build Plan、Host Activation Blueprint 与 Provider Binding
Plan；current Composition renderer 6 生成薄 registration TU、sealed current-image source、私有声明 header、CMake attachment
fragment 与 content-addressed manifest。fragment 为 Host 创建私有 OBJECT attachment target，但不创建 Host target、不运行 CMake、
不把 provider call order 当作 activation order。修改该边界时，除 focused Python tests 外，还必须在
两个编译器环境中运行 opt-in synthetic CMake positive/negative evidence：

```powershell
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && set ""CXX=clang-cl"" && set ""ASHARIA_EXPECT_CMAKE_CXX_COMPILER=clang-cl.exe"" && set ""ASHARIA_RUN_CMAKE_INTEGRATION_TESTS=1"" && python -m unittest tools.tests.test_static_composition_root.StaticCompositionRootTests.test_generated_fragment_configures_compiles_and_links -v"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && set ""CXX=cl"" && set ""ASHARIA_EXPECT_CMAKE_CXX_COMPILER=cl.exe"" && set ""ASHARIA_RUN_CMAKE_INTEGRATION_TESTS=1"" && python -m unittest tools.tests.test_static_composition_root.StaticCompositionRootTests.test_generated_fragment_configures_compiles_and_links -v"
```

该 fixture 必须证明 renderer 6/provider v4 valid root 可 configure/compile/link/execute，并得到 frozen callback table 的 exact owning
RegistrationSnapshot v2；错误 provider
signature 在 compile-time `static_assert` 失败，以及 missing/wrong provider target 和 duplicate attachment 在 final configure
fail closed。

Static Factory Callback Table v1 只允许 provider v4 调用
`registerFactory(localFactoryId, completeDescriptor, availableTypedBindings)`。五个 typed `noexcept` callbacks 必须非空；selected
`StaticContributionBindingV2` 必须由 `bindStaticContributionV2<Contract, &accessor>()` 创建，其中 accessor exact signature 为
`Contract* (FactoryInstanceViewV1) noexcept`。generation、
Blueprint digest、package/version/module/entry point 与 exact selected factory/contribution ID/kind 必须由 generated root 注入，不得让
provider 自报完整 identity。public contract type 唯一声明 kind 与 `single|multiple`；type key 必须来自 writable inline storage，不能
依赖会被 MSVC `/OPT:ICF` 合并的只读 data/function/accessor address。accessor address 只用于 future invocation，不参与 type identity、
hash、snapshot 或 diagnostics。recorder 必须按 Capacity v2 预留 factory 与 selected contribution storage，
callback window 中 recorder-owned storage 零动态分配；首次错误 sticky，失败不返回 partial table/snapshot。成功 table 私有持有
process-local type/accessor evidence，并只向 Snapshot v2 投影 ID/kind/cardinality；pointer/type key/accessor/token 不进入 JSON、
generation ID、receipt 或 diagnostics。
registration-only path 不调用任何 lifecycle callback 或 payload accessor；`single` 在本门禁不执行 table-wide 数量限制。
修改 `engine/host-runtime` registration target、snapshot JSON renderer 或 generated recording glue 时，两个 test presets 都必须运行
registration 与 snapshot JSON tests，并继续运行上述 synthetic fixture：

```powershell
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug-tests && cmake --build --preset clangcl-debug-tests --target asharia-host-runtime-registration-tests asharia-host-runtime-registration-snapshot-json-tests && ctest --preset clangcl-debug-tests -R asharia-host-runtime --output-on-failure"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug-tests && cmake --build --preset msvc-debug-tests --target asharia-host-runtime-registration-tests asharia-host-runtime-registration-snapshot-json-tests && ctest --preset msvc-debug-tests -R asharia-host-runtime --output-on-failure"
```

该 gate 至少覆盖完整/缺失 descriptor、canonical table-owned Snapshot v2、zero-contribution factory、empty composition、
unknown/missing/duplicate factory、contribution expectation canonicality、missing/duplicate/kind mismatch、same-kind type/cardinality
conflict、typed accessor signature/null negatives、abstract-interface/multiple-inheritance pointer adjustment、unselected binding/accessor
inert、canonical type/accessor index alignment、provider/binding observation-order determinism、mixed composition evidence、provider 外调用、
provider/factory/contribution count mismatch、pre-copy expectation failure 的空 provider attribution、sticky first error、
text/diagnostic capacity exhaustion、token ownership transfer 与 valid-token fail-fast。provider invocation window 中 recorder-owned
no-allocation 边界由 create 阶段定长 storage、recording 路径索引写入，以及 ClangCL `noexcept`/exception-escape gate 共同约束。

[Activation Eligibility v1](../architecture/adr-activation-eligibility-v1.md) 保留历史决策；current
[Generated Current-Image Host 与 Project Bootstrap v1](../architecture/adr-generated-current-image-project-bootstrap-host-v1.md) 已把 normal
路径硬切到 Eligibility V2。Stage 1 只能按值消费 generated `CurrentImageActivationDescriptorV2`，并在 provider invocation 前校验
T3/C6/provider-v4/Snapshot-v2 tuple、ProcessScope projection、control thread、process epoch 与一次性 claim；recording 成功后，Stage 2
只能把同一 pending table 按值对证为 `AdmittedStaticFactoryCallbackTableV2`。normal startup 不读取或 hash executable，也不要求外部
launch receipt。
[ProcessScope Lifecycle v1](../architecture/adr-process-scope-lifecycle-v1.md) 与
[ProcessScope Contribution Registry and Activation Lease v1](../architecture/adr-process-scope-contribution-registry-and-activation-lease-v1.md)
继续把 registration-only 取证与 lifecycle execution 分开。ProcessScope 只能消费 admitted table 及其 sealed Blueprint process projection，
preflight 必须在首个 callback 前完成，并为 process-selected contributions 建立 fixed slots。`ProcessScopeExecutorV2` 的启动顺序只能来自
Blueprint；factory 只有在 activate 后 selected accessors 全部成功且 contribution-only lease 原子提交后才 dependency-visible，整个 start
成功后 registry 才开放。停止与失败回滚必须完成 reverse quiesce → registry `Revoking` → reverse lease revoke → reverse
deactivate/destroy → registry `Revoked` gate。修改 eligibility、admitted descriptor access、Factory contexts、ProcessScope executor、typed
registry/handle 或 lease/revoke 时，两个编译器都必须构建 ProcessScope focused tests，并运行全部 `asharia-host-runtime` CTest（其中包括
Active owner 隐式析构的 fail-fast probe）：

```powershell
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug-tests && cmake --build --preset clangcl-debug-tests && ctest --preset clangcl-debug-tests -R asharia-host-runtime --output-on-failure && ctest --preset clangcl-debug-tests -R asharia-project-bootstrap --output-on-failure"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug-tests && cmake --build --preset msvc-debug-tests && ctest --preset msvc-debug-tests -R asharia-host-runtime --output-on-failure && ctest --preset msvc-debug-tests -R asharia-project-bootstrap --output-on-failure"
```

focused tests 至少覆盖 Eligibility V2 wrong tuple/thread/epoch/double-claim、recording header mismatch、Stage 2 table/storage affinity、
empty/success、Blueprint/table order permutation、dependency views、全部 create/activate failure positions、cleanup
failure continuation、token exactly-once destroy、wrong-thread/stale epoch、operation reentrancy、state misuse、结构性 preflight negatives、
plan 外 descriptor 永不调用、single conflict 零 callback、typed `single/at/size` cardinality、canonical multiple order、accessor exactly once、
null accessor atomic rollback、quiesce 可借用、deactivate/destroy 期间 `RegistryRevoking`、stop 后 `RegistryRevoked`、owner 销毁后
`RegistryExpired` 与旧 generation fail-closed。`asharia-project-bootstrap-tests` 还必须覆盖真实 `asharia.project.json` 的 valid/invalid
读取与 deterministic summary。registration-only generated Host 的五个 callbacks 与 payload accessors 仍必须是 abort/counter probes；
该路径不得因 ProcessScope 实现而开始执行 lifecycle 或 accessor。

Windows Development Host 的 build/publication adapter 只允许消费已发布的 exact template/composition generation。adapter 必须在 spawn
前根据完整 generation 只读复验两棵 closed publication tree，拒绝 payload 漂移、额外 entry 与 link/reparse；随后使用 typed argv、
caller-supplied environment 与 `shell=False`，Conan 必须先行；File API binding 必须锁定 latest client reply 中唯一
configuration/`EXECUTABLE`/primary artifact，并在 build 后复验普通文件。restricted Host 只输出 canonical registration JSON，
不得执行 activation/lifecycle、UI、artifact hash 或 receipt publication。#288 的 downstream publisher 必须另外读取同一 stable
File API index 的 configured CXX compiler，把 exact executable 流式复制到 collector-owned staging，运行 staged bytes，交叉验证
registration handoff，并发布/deep-verify Host Executable Binding Receipt；这份 receipt 只属于 build/publication evidence，不是 normal
startup ticket。current T3 normal Host 直接消费 C6 sealed descriptor，执行 admission → recording → table admission → ProcessScope start →
借用并运行 `ProcessApplicationV1` → release → explicit stop。Bootstrap project-open adapter 只接受 strict Summary v1
schema/version/exact fields；exit `65` 是项目拒绝，spawn/timeout/overflow/其他 exit/stderr/protocol failure 是 fixed-Host failure。
修改任一边界时，除 focused Python tests 外，运行双编译器 exact Host integration：

```powershell
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && set ""CXX=clang-cl"" && set ""ASHARIA_HOST_TEST_RUN_CLANG_TIDY=1"" && set ""ASHARIA_EXPECT_CMAKE_CXX_COMPILER_ID=Clang"" && set ""ASHARIA_EXPECT_CMAKE_CXX_COMPILER_VERSION=19.1.5"" && set ""ASHARIA_RUN_HOST_TEMPLATE_INTEGRATION_TESTS=1"" && set ""ASHARIA_HOST_TEST_TOOLCHAIN_FILE=build\conan\clangcl-debug\Debug\generators\conan_toolchain.cmake"" && python -m unittest tools.tests.test_generated_host_executable.GeneratedHostExecutableIntegrationTests.test_exact_host_build_and_project_bootstrap -v"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && set ""CXX=cl"" && set ""ASHARIA_EXPECT_CMAKE_CXX_COMPILER_ID=MSVC"" && set ""ASHARIA_EXPECT_CMAKE_CXX_COMPILER_VERSION=19.44.35215.0"" && set ""ASHARIA_RUN_HOST_TEMPLATE_INTEGRATION_TESTS=1"" && set ""ASHARIA_HOST_TEST_TOOLCHAIN_FILE=build\conan\msvc-debug\Debug\generators\conan_toolchain.cmake"" && python -m unittest tools.tests.test_generated_host_executable.GeneratedHostExecutableIntegrationTests.test_exact_host_build_and_project_bootstrap -v"
```

focused compatibility tests 必须证明 Template renderer 3 + Composition renderer 6/provider v4 + RegistrationSnapshot v2 是唯一接受
组合，并拒绝旧 renderer/provider/snapshot。focused chain 必须分别覆盖 exact target/path binding、single-target build without clean-first、build 后 binding
refresh、receipt atomic publication 与 closed-tree deep-verification negatives；双编译器 fixture 必须以当前组合端到端覆盖 restricted
process stdout/stderr/exit contract、expected generation/Blueprint snapshot 对证、same-index configured compiler、collector-owned staged
Host 执行、receipt publication/deep verification，以及 normal Host 的真实项目 success、坏 descriptor exit 65 + stable diagnostic、非法
restricted/normal 参数混用 exit 64 与所有结果的 clean stop。#298 后 fixture 还必须将 descriptor、Manifest 与 Lock 放在同一 root，
证明只执行 published artifact、Bootstrap `Ready`/`SafeMode`、同 native graph 下只改 `projectId` 可复用 C6，并保持每个编译器只构建一次。
MSVC/ClangCL receipts 可以因 compiler identity/executable bytes 不同而不同。
synthetic provider 的五个 callbacks 与全部 selected payload accessors 必须为 abort/counter probes，以证明 registration/receipt path
零 lifecycle/accessor invocation。
该 gate 不要求 clean-first 或默认 all-target build。restricted 半边不执行 lifecycle；normal 半边必须证明完整 start/run/stop，但不等于
完整 Editor、ProjectScope、asset database 或项目 package 已被激活。
Installed Distribution Repair Verifier v1 必须从调用方提供的 exact expected `EngineGenerationId` 开始；不能只信磁盘 manifest
或目录名，不能在发现损坏后写回安装树，也不能把 `FatalDistributionError` 当作磁盘健康状态。

开发中可先运行本地 pre-PR 提示脚本，让它按当前 diff 提示固定门禁、包级 CTest、smoke 范围和需要检查的文档：

```powershell
powershell -ExecutionPolicy Bypass -File tools\pre-pr.ps1 -IncludeUntracked
```

该脚本默认只提示；需要先跑 encoding、doc sync 和 whitespace 这三个快速门禁时，追加 `-RunCheapGates`。

涉及 `apps/studio` Avalonia shell、managed viewport models、native interop bridge、Scene View composition host/presenter
或 Studio ViewModel/XAML 时，必须跑：

```powershell
dotnet build apps\studio\Asharia.Studio.sln -c Release
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "SceneView|ViewportNative|Composition"
dotnet test apps\studio\Asharia.Studio.sln -c Release --no-build --blame-hang --blame-hang-timeout 10m
```

上面的 `dotnet build` 不构建 native。若改动涉及 `apps/editor`、Viewport native ABI/interop、renderer shader closure，或当前
`build/cmake/msvc-release` artifact 不能证明与 checkout 同代，必须先运行 native-first gate：

```powershell
cmd /c "build\conan\msvc-release\Release\generators\conanbuild.bat && cmake --preset msvc-release && cmake --build build\cmake\msvc-release --target editor-native"
```

Studio build 随后以 `Always` 复制所选 preset 的 `editor_native.dll`，并由
`ValidateStudioViewportNativeRuntimeContract` 对最终 `$(TargetDir)` sibling 执行完整 V10 required / legacy V1--V9 forbidden
admission。这个 gate 不允许 V1--V9 fallback，也不从其他 `bin/`、test preset 或历史隔离输出发现“最新”DLL；失败时必须重建显式选择的
native preset。它只保护普通 build 的实际 sibling，不能替代 `StudioEditorImageProducer` 对发行 publish tree 的静态 PE identity、
export 与 closed-tree qualification。

显式选择 `msvc-debug-tests` 只用于本地 GPU/fault-injection 验收，其中
`editor_viewport_open_stream_v10_for_test` 是允许的测试扩展；正式 Release Editor Image 必须拒绝该 export。

涉及`Program`入口、App/process lifecycle、Window close或teardown exit-code时，focused反馈还必须运行真实的
Windows disposable-child边界（它会启动当前构建输出中的`Editor.exe`，并保证timeout/cancel/fatal路径kill tree后有界reap）：

```powershell
dotnet build apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --disable-build-servers
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --no-build --disable-build-servers --filter "FullyQualifiedName~StudioProcessAcceptanceTests|FullyQualifiedName~ProgramSourceTests|FullyQualifiedName~StudioProcessSessionTests"
```

该test owner不得进入production App，不得增加fault CLI mode、artifact/crash framework或第二diagnostics truth；focused
结果仍不能替代下方canonical solution test。

`Asharia.Studio.sln` 是唯一 managed solution，当前精确列出 8 个 production 与 9 个 test projects（含独立
`asharia-studio-observe`与其tests）；
独立
`Asharia.Studio.Headless.Tests` 使用 Avalonia 12/xUnit v3 dispatcher 运行 production XAML，旧 `Editor.sln` 已删除。
R0.5 `Asharia.Studio.DevelopmentHost`与`Asharia.Studio.DevelopmentProtocol`只允许由`Editor.csproj`的Debug条件边引用；直接使用其类型的Headless/Editor test引用也必须是Debug条件边。Editor Release dependency/publish必须在任意深度
拒绝`Asharia.Studio.DevelopmentProtocol`与`Asharia.Studio.DevelopmentHost`。
current-user产品endpoint只能由精确Debug参数`--development-observation=readonly`启用；环境变量、近似参数和Release参数不得旁路。
相关改动必须用真实Windows Pipe与`%LOCALAPPDATA%/Asharia/Studio/development-sessions/<StudioInstanceId>.json`证明
current-user protected DACL、canonical token、bounded/atomic manifest、disconnect与`manifest撤销 → Pipe stop → Host stop`，并在测试后确认
manifest、listener与相关进程残留为0。仅凭独立server/client test或fixture manifest不能宣称产品endpoint成立。
observe CLI生产项目只能引用DevelopmentProtocol；`list/describe/diagnostics/logs/ui-list-windows/ui-read-tree`必须验证current-user ACL、PID+process-start与handshake descriptor，
要求显式instance，且任何输出不得含attach token。壳UI Probe的`ui.listWindows/readTree`已经以typed golden、真实Avalonia Headless semantic projection和产品Host→Pipe→typed client/CLI闭环关闭；
projection只读显式AutomationId、必须在UI dispatcher上执行并同时具备semantic/visual traversal硬上限，Host也只在真实provider存在时广告两项capability。
未实现的`state/ui.readElement/ui.find`不得注册unavailable stub。`asharia-studio-observe mcp`必须保持最后接入的标准`2025-06-18`、Protocol-only、stdio只读adapter：
只复用前述六个已有typed client方法，固定`initialize → notifications/initialized → tools/list/tools/call`生命周期，只在连接初始化时协商version/capabilities；项目级`.codex/config.toml`必须省略stdio `cwd`，让Codex使用当前thread/runtime的checkout/worktree root，并由architecture gate冻结；不得用`.`或`..`引入宿主process-cwd漂移、保留`server/discover`双协议分支、shell out CLI或借adapter扩大协议面。在Git worktree中必须单独确认本worktree受信任且本树Release tool已构建；不得借用base checkout的绝对DLL或扩大父级worktrees目录信任。配置变更后的宿主验收必须通过官方MCP config reload后的下一active turn或fresh task取得ready、精确六tool catalog和一次真实只读调用；仅重启Desktop、配置存在或独立Inspector均不能替代。
focused filter 只用于快速反馈，不能替代 solution test。real-SDK publish/stage证据位于独立
`tools/studio-distribution.Tests` gate，不属于Application suite；即使hang deadline超时，也必须报告完整gate未通过，
不能用排除慢测试的结果宣称全绿。

涉及 frame loop、swapchain、RenderGraph、renderer 或 Vulkan adapter 时，必须跑：

```powershell
$smokes = @(
    "--smoke-window",
    "--smoke-vulkan",
    "--smoke-frame",
    "--smoke-rendergraph",
    "--smoke-transient",
    "--smoke-dynamic-rendering",
    "--smoke-resize",
    "--smoke-triangle",
    "--smoke-depth-triangle",
    "--smoke-mesh",
    "--smoke-mesh-3d",
    "--smoke-draw-list",
    "--smoke-mrt",
    "--smoke-descriptor-layout",
    "--smoke-material-binding",
    "--smoke-fullscreen-texture",
    "--smoke-scene-draw-packet",
    "--smoke-render-view-scene-mesh",
    "--smoke-render-view-grid-readback",
    "--smoke-offscreen-viewport",
    "--smoke-compute-dispatch",
    "--smoke-buffer-upload",
    "--smoke-texture-upload",
    "--smoke-renderer-format-contract",
    "--smoke-deferred-deletion",
    "--smoke-reflection-registry",
    "--smoke-reflection-transform",
    "--smoke-reflection-attributes",
    "--smoke-serialization-roundtrip",
    "--smoke-serialization-json-archive",
    "--smoke-serialization-migration"
)

foreach ($preset in @("clangcl-debug", "msvc-debug")) {
    $exe = "build\cmake\$preset\apps\sample-viewer\asharia-sample-viewer.exe"
    foreach ($smoke in $smokes) {
        & $exe $smoke
        if ($LASTEXITCODE -ne 0) {
            throw "$preset $smoke failed with exit code $LASTEXITCODE"
        }
    }
}
```

如果某个 smoke 命令尚不存在，审查回复必须说明原因，不能默默跳过。

涉及 RenderView scene mesh、RenderGraph vertex/index buffer access、indexed command encoding、scene raster
policy 或 Vulkan polygon mode capability 时，`--smoke-render-view-scene-mesh` 必须在两个 compiler preset 上运行。
审查证据至少确认：`builtin.render-view-scene-mesh` 的 Color/Depth + `vertices: BufferVertexRead` +
`indices: BufferIndexRead` schema、`DrawIndexed` 五参数与 packet context、空 scene 不插入 pass、unknown resource
保留 typed context，以及同帧 per-view Solid/Wireframe 不互相污染。`fillModeNonSolid` 未启用时，V10 submit 必须在
复制/入队前返回 typed `FeatureUnavailable`，stream 保持 Open 且不得重试同一 Wireframe request；后续显式 Solid
request 必须可恢复。实现不得创建非法 line pipeline、静默回退 Solid 或为 1 px 线宽启用 `wideLines`。

修改 validation mesh fixture/generator 时，还必须运行：

```powershell
python -m unittest tools.tests.test_validation_mesh_product
```

`assets/fixtures/scene-rendering/directional-wedge.obj`、sidecar metadata 与
`tools/generate_validation_mesh_product.py` 只证明 deterministic fixture -> generated product -> renderer-owned
GPU buffer 的门禁链路。它们不是通用 OBJ importer 或稳定 runtime mesh product schema；review 不得据此宣称
asset-backed mesh resource pipeline 已完成。

涉及 Scene schema v2、Document ABI v3、`SceneMeshComponent` 或 `packages/scene-rendering` extraction 时，必须同时在
两个 test preset 构建并运行 CPU smoke。审查要确认 typed mesh reference 仅持久化 authored GUID/type、runtime `EntityId`
与 product/GPU key 不进入 scene，`T * R * S` matrix、empty zero-draw、ready binding、missing/wrong-kind/stale/invalid
逐 item no-draw diagnostics、revision replacement 不共享旧 draw vector 均未漂移：

```powershell
foreach ($preset in @("clangcl-debug-tests", "msvc-debug-tests")) {
    cmake --build --preset $preset --target asharia-scene-rendering-smoke-tests
    ctest --test-dir "build\cmake\$preset" -C Debug `
        -R "^asharia-scene-rendering-smoke-tests$" --output-on-failure
}
```

`scene-rendering` 只接受 caller-explicit product bindings；封闭 validation product native resolver 不是 importer、asset
database 或 runtime resource registry。任何把 fixture resolver 扩展为通用加载/注册服务的改动必须先有独立的 owner 合同和
review slice，不能以 #367 的 smoke 作为授权。

所有 `asharia-sample-viewer --smoke-*` 图形路径，以及通过 `EditorRunMode` 启动的图形 editor smoke，都必须
创建隐藏 GLFW 窗口，不得显示顶层窗口或取得前台焦点。sample-viewer 的 smoke 窗口必须通过唯一
`smokeWindowDesc()`入口取得`visible=false`，editor 则由唯一`runEditor` owner依据`smokeMode`设置；隐藏模式仍
必须走真实 Vulkan surface、swapchain、render、resize（适用时）与 teardown 路径，不能用 offscreen fixture
或 stub 替代 production owner。两个应用无 smoke 参数的交互启动均保持可见。

涉及 `apps/editor` shell、menu、panel registry、action registry、event queue 或 ImGui runtime 时，必须跑：

```powershell
foreach ($preset in @("clangcl-debug", "msvc-debug")) {
    $exe = "build\cmake\$preset\apps\editor\asharia-editor.exe"
    & $exe --smoke-editor-shell
    if ($LASTEXITCODE -ne 0) {
        throw "$preset --smoke-editor-shell failed with exit code $LASTEXITCODE"
    }
}
```

涉及 `AssetBrowserPanel`、editor asset catalog snapshot、`EditorAssetIconRegistry`、Lucide icon id 或 custom asset
icon resolver 时，还必须跑：

```powershell
foreach ($preset in @("clangcl-debug", "msvc-debug")) {
    $exe = "build\cmake\$preset\apps\editor\asharia-editor.exe"
    & $exe --smoke-editor-asset-browser
    if ($LASTEXITCODE -ne 0) {
        throw "$preset --smoke-editor-asset-browser failed with exit code $LASTEXITCODE"
    }
}
```

涉及 `packages/asset-core`、`packages/asset-pipeline` texture profile/catalog facet、editor asset catalog 或 Asset Browser
profile/sub-asset 语义时，还必须跑资产边界检查，确保 `asset-core` 没有重新引入具体 texture profile/importer 解释：

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-asset-boundaries.ps1
```

涉及 `packages/asset-pipeline` product blob read/execution diagnostics、texture import contract/diagnostics、
`.ameta` texture import settings 或 `--smoke-texture-upload` 的 product payload 读取路径时，必须跑资产边界检查
和 asset-pipeline package-local tests，证明 texture profile 解释仍留在 `asset-pipeline`，且 placeholder product
blob payload、PNG Texture2D product payload、raw `.rgba8` / PNG CPU texture payload 和
missing/malformed/unsupported/payload-size/decode diagnostics 没有漂移：

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages\asset-pipeline -B build\cmake\package-asset-pipeline-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build\cmake\package-asset-pipeline-tests-msvc-debug && ctest --test-dir build\cmake\package-asset-pipeline-tests-msvc-debug --output-on-failure"
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake -S packages\asset-pipeline -B build\cmake\package-asset-pipeline-tests-clangcl-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/clangcl-debug/Debug/generators/conan_toolchain.cmake && cmake --build build\cmake\package-asset-pipeline-tests-clangcl-debug && ctest --test-dir build\cmake\package-asset-pipeline-tests-clangcl-debug --output-on-failure"
```

只修改 texture format / product policy 文档时，至少跑 encoding、doc sync 和 whitespace
门禁；如果文档 PR 同时修改 decoder/transcoder dependency、Conan lockfile、product payload schema、
public asset-pipeline headers、asset-processor tool code、runtime texture owner、RenderGraph/RHI upload
路径或 Vulkan format handling，则必须升级到对应 package-local tests、资产边界检查、repository build
和 sample-viewer smoke。KTX/KTX2/Basis/DDS/HDR/EXR policy 不等于 decoder implementation，不能因为
文档提到格式就让 runtime、editor、RenderGraph、renderer 或 RHI 直接依赖具体 decoder/transcoder library。

涉及 `packages/mesh-product` Mesh Product v1 layout/reader/writer，或 `asset-pipeline` 受限 `.glb`
importer/product execution 时，必须跑两个 package-local suites 与资产边界检查：

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages\mesh-product -B build\cmake\package-mesh-product-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build\cmake\package-mesh-product-tests-msvc-debug && ctest --test-dir build\cmake\package-mesh-product-tests-msvc-debug --output-on-failure"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages\asset-pipeline -B build\cmake\package-asset-pipeline-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build\cmake\package-asset-pipeline-tests-msvc-debug && ctest --test-dir build\cmake\package-asset-pipeline-tests-msvc-debug --output-on-failure"
powershell -ExecutionPolicy Bypass -File tools\check-asset-boundaries.ps1
```

审查证据必须确认：canonical little-endian layout/limits 和 malformed/truncated/oversized/range negatives；
相同输入 byte-identical；真实 `assets/fixtures/mesh-product-v1/restricted-static-mesh.glb` 完成 artifact +
manifest + reader round-trip；default-scene/source-order、RH→Asharia LH、node transform、negative determinant
CCW repair、missing normal/UV 与 material slots 没有漂移。Khronos glTF Validator 应对 fixture 返回 zero errors。
`mesh-product` runtime-safe target 不得依赖 fastgltf/`asset-pipeline`；`--smoke-product-execution` 单独不能宣称
ResourceRuntime、GPU mesh、reload/deferred destroy、Scene View 或 ThumbnailService 已完成。

涉及 `packages/asset-artifact`、`packages/resource-runtime` 的 Mesh Product typed CPU load/reload/lease，或
`tools/asset-processor --smoke-mesh-resource` 时，必须在 Conan bootstrap 后运行：

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug-tests && cmake --build --preset msvc-debug-tests --target asharia-asset-artifact-tests asharia-resource-runtime-smoke-tests asharia-asset-processor"
ctest --test-dir build\cmake\msvc-debug-tests --output-on-failure -R "^(asharia-asset-artifact-tests|asharia-resource-runtime-smoke-tests|asharia-asset-processor-smoke-mesh-resource)$"
```

证据必须覆盖 invalid relative path、limit/size/hash/missing artifact、absolute-root redaction、missing/stale/invalid
selection、pending/ready dedup、slot/request 双 generation、stale completion、reload success/failure、unload/reuse、
旧 lease 存活与 owner-thread mutation。真实 smoke 必须从 restricted GLB 重新生成 artifact/manifest，再通过
`MeshResourceStore` lease 复验 11 vertices、9 indices、3 submeshes、3 material slots 与固定 bounds。该门禁只证明
typed CPU resource；GPU upload、fence retirement、Scene View 与 thumbnail 仍需后继 renderer/editor smoke。

涉及 `project-core` 描述符 model/IO 或 Editor 资产目录的项目描述符读取路径时，还必须在两个test presets上
构建并运行package-owned smoke，证明round-trip、duplicate/missing field与malformed input仍被正确处理：

```powershell
foreach ($preset in @("clangcl-debug-tests", "msvc-debug-tests")) {
    cmake --build --preset $preset --target asharia-project-core-smoke-tests
    ctest --test-dir "build\cmake\$preset" -C Debug `
        -R "^asharia-project-core-smoke-tests$" --output-on-failure
}
```

Studio R0 删除的旧 `editor_project_*` bridge 仍不得恢复。当前真实 create/open consumer 固定走
`project-core` 自有 `asharia-project-native` -> `Asharia.Studio.EngineBridge` -> Application `ProjectSession`；相关改动必须
同时跑 `asharia-project-core-smoke-tests`、`asharia-project-native-smoke-tests`、
`asharia-project-native-c-header-smoke`、managed bridge/Application/Headless tests 与 Release distribution closure。

涉及 Studio SceneDocument、Hierarchy/Inspector mutation、dirty/save、scene ABI 或 `scene-core` 持久化时，必须同时跑
`asharia-scene-document-smoke-tests`、`asharia-scene-document-native-smoke-tests`、
`asharia-scene-native-c-smoke-tests`、managed solution tests，以及 `Editor.Tests` 中使用真实 project/scene DLL 的
`StudioSceneEditingAcceptanceTests`。该验收固定覆盖“创建项目 -> 默认场景 -> 创建/修改实体 -> 保存 -> 关闭 -> 重开
数据一致”。Release distribution closure 必须精确包含 `bin/asharia_scene_native.dll` 并验证全部 SceneDocument exports；
缺失、错名、嵌套或同 stem 副产物都必须失败。

涉及 document Undo/Redo、history、Transform mutation receipt 或 dirty/savepoint 时，还必须按
[`ADR-0013`](../../apps/studio/docs/adr/0013-authoritative-document-transform-undo-redo.md) 审查并验证：

- Apply/Undo/Redo 的 native revision 严格单调，dirty 只由 `ContentStateId != SavedContentStateId` 决定；
- changed/no-op/failure receipt 与 authoritative snapshot 一致；failure、cancel、revision conflict、target missing 和
  malformed receipt 均不移动 history cursor；
- history 为 per-document `List + cursor`，新 edit 截断 redo，unsupported persistent mutation 清 history；
- 256 entries 与 16 MiB 两个预算都被测试，淘汰完整最老 entry 后 cursor/byte count 一致；
- focused text input 可优先处理自己的 Undo/Redo，document shortcut 不抢占 draft；toolbar 与 shortcut 消费同一
  Application enablement/label；
- 真实 native acceptance 覆盖 `A clean -> B/save clean -> C dirty -> Undo(B clean) -> Redo(C dirty) -> save/reopen`，
  并确认 stable `ObjectId` target 不依赖当前 selection、Inspector quaternion/Euler projection 不抖动。

对 schema v2 / Document ABI v3 改动，验收还必须确认 hard cut：不导出或接受旧 schema/ABI fallback，mesh authored GUID/type
round-trip 后不出现 runtime EntityId、product generation/hash、Basic resource/material key 或 GPU handle。若同时改动 V10
viewport producer/native ABI，必须证明 malformed V10 packet 被整帧拒绝；item-level asset binding failure 不能被提升为
packet-level partial submit。

涉及 `packages/resource-runtime` runtime handle/status/product-record resolution/diagnostics 时，必须跑
package-local tests，证明 pending / ready / failed、generation、product key mismatch 和 product record
诊断矩阵没有漂移：

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake -S packages\resource-runtime -B build\cmake\package-resource-runtime-tests-msvc-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASHARIA_BUILD_TESTS=ON -DCMAKE_TOOLCHAIN_FILE=%CD%/build/conan/msvc-debug/Debug/generators/conan_toolchain.cmake && cmake --build build\cmake\package-resource-runtime-tests-msvc-debug && ctest --test-dir build\cmake\package-resource-runtime-tests-msvc-debug --output-on-failure"
```

涉及 editor viewport rendering、sampled texture registration、descriptor lifetime、Frame Debug capture/preview state、Live RG View、FrameDebuggerPanel RenderGraph view 或 resize flow 时，还必须跑：

```powershell
foreach ($preset in @("clangcl-debug", "msvc-debug")) {
    $exe = "build\cmake\$preset\apps\editor\asharia-editor.exe"
    & $exe --smoke-editor-viewport-native
    if ($LASTEXITCODE -ne 0) {
        throw "$preset --smoke-editor-viewport-native failed with exit code $LASTEXITCODE"
    }
    & $exe --smoke-editor-viewport
    if ($LASTEXITCODE -ne 0) {
        throw "$preset --smoke-editor-viewport failed with exit code $LASTEXITCODE"
    }
    & $exe --smoke-editor-viewport-resize
    if ($LASTEXITCODE -ne 0) {
        throw "$preset --smoke-editor-viewport-resize failed with exit code $LASTEXITCODE"
    }
    & $exe --smoke-editor-frame-debugger
    if ($LASTEXITCODE -ne 0) {
        throw "$preset --smoke-editor-frame-debugger failed with exit code $LASTEXITCODE"
    }
}
```

涉及 V10 scene packet、`BasicRenderViewSceneDesc::sourceRevision` 或 Frame Debug projection 时，`--smoke-editor-frame-debugger`
必须断言 capture JSON、panel 与冻结的 RenderView diagnostics 精确记录同一个 `sourceRevision`；旧 ImGui viewport 未绑定
authoritative SceneDocument 时允许为 `0`。Studio V10 scene-mesh process acceptance 必须另行断言 request、receipt 与实际 scene
snapshot 的 revision 非零且精确相等。Scene/Game 可共享 authored mesh snapshot，但审查必须确认 raster policy 仍按 view
独立，不能由 Frame Debug 或另一个 viewport 覆盖。

涉及 viewport camera projection、aspect ratio 或 FOV axis 时，Scene 默认必须保持 90° horizontal FOV，Game/Preview 默认必须
保持 60° vertical FOV；这些策略属于 per-session immutable request，不写入 SceneDocument。native camera smoke 必须在固定宽度、
至少两个不同高度下验证 Scene 顶点像素间距误差 `<=1 px`，并确认 camera position/target/canonical FOV 不变。Studio
`--smoke-viewport-multi-endpoint --viewport-multi-mode=scene-game` 必须从实际发布的 request 断言两个 endpoint 的策略互不串扰。
不得用 resize-time camera dolly、auto-focus、FOV interpolation 或 release 后二次 mutation 掩盖构图变化。
涉及 Studio Scene View camera navigation、camera invalidation 或 picking/presented-frame identity 时，还必须运行真实
240 Hz camera cadence gate：

```powershell
$env:ASHARIA_RUN_STUDIO_GPU_ACCEPTANCE = "1"
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --no-build `
    --filter "FullyQualifiedName~Camera_navigation_does_not_starve_scene_view_surface_updates"
```

该 gate 必须保持 exact surface-update `>=60 FPS`、p95 `<=25 ms`、max `<=100 ms`，并在输入停止后证明最终 camera request
已经成为可交互的 presented frame。它不提供 DWM/physical display evidence，也不能用通用 `Update(dt)`、UI timer 或 Physics
替代既有 composition cadence owner。

涉及 Studio Transform Gizmo、`GizmoChanged`、`GizmoModeChanged`、V10 Gizmo packet 或 renderer gizmo lines 时，还必须证明：
轴/环命中、局部轴 rotation 和近平行退化数学为 UI-neutral；scale 固定 drag 起点、只改命中分量、保留镜像符号且不穿过零；
preview sample 不推进 `MinimumPresentableSequence`；240 次连续 preview 仍允许已完成帧呈现；release 恰好一次
`SetEntityTransformAsync`，no-op/Escape/capture/focus/mode/stale/failure 不提交或回滚；V10 native smoke 拒绝 invalid
kind/axis/rotation、孤立 flag/payload 和非零未标记 payload，并以 `lastDebugWorldLineCount` 证明匹配 proxy 轴被
renderer-owned 平移轴、三组 64 段旋转环或局部轴 shaft + endpoint wire cube 替换。最小 focused gate：

```powershell
dotnet test apps\studio\Tests\Asharia.Studio.Application.Tests\Asharia.Studio.Application.Tests.csproj --filter "ViewportSessionTests|ViewportTranslateGizmoTests|ViewportRotateGizmoTests|ViewportScaleGizmoTests"
dotnet test apps\studio\Tests\Asharia.Studio.Headless.Tests\Asharia.Studio.Headless.Tests.csproj --filter StudioScenePanelViewModelTests
dotnet test apps\studio\Tests\Asharia.Studio.EngineBridge.Tests\Asharia.Studio.EngineBridge.Tests.csproj --filter ViewportBridgeTests
& build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-native
```

该 Slice 不新增 Vulkan resource、descriptor、pipeline、pass、barrier 或 queue ownership；若实际改动这些边界，必须升级为对应
renderer/synchronization 审查，而不能把 Gizmo smoke 当作替代。
真实 Studio/Vulkan 验收还必须运行固定宽度的 height-only Window lane：

```powershell
$env:ASHARIA_RUN_STUDIO_GPU_ACCEPTANCE = "1"
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj --no-restore `
    -p:StudioNativeBuildPreset=msvc-debug-tests `
    --filter "DisplayName~window-resize-main-height-aba-projection"
```

该 lane 必须从实际 published requests、native leases 与 `LastPresentedSequence` 证明至少两个 exact rendered 高度、同一非零
revision、90° `MaintainHorizontal`、固定 outer/client/Scene/surface 宽度、x/y pixel scale drift `<=1 px`，并证明 final exact
projection request 唯一、最终 presented sequence 匹配且 `WM_EXITSIZEMOVE` 后没有 request 或 camera/projection mutation。它不采集
WGC 像素，不能据此声称 DWM refresh 或物理 scanout。

## 设计审查门禁

提交前必须结合相关资料做设计审查。优先资料：

1. Khronos Vulkan spec、refpage、Vulkan Guide。
2. VMA、Slang、SPIR-V、shader toolchain 官方文档。
3. 成熟案例：Frostbite FrameGraph、Granite、Diligent Engine、RenderDoc/Nsight 的资源视图思路。
4. 本仓库当前事实与设计边界：`docs/architecture/overview.md`、
   `docs/architecture/flow.md`、`docs/architecture/package-first.md`、
   `docs/rendergraph/rhi-boundary.md`、`docs/architecture/editor.md` 和对应
   `docs/systems/` 文档。
5. C++ Core Guidelines、Game Programming Patterns、Unity DOTS / Entities 和 Data-Oriented Design 资料，用于内部代码设计、
   设计模式和数据导向审查。

设计审查必须覆盖：

- RenderGraph 是否保持后端无关。
- Vulkan layout、stage、access、barrier 是否只出现在 RHI 或 Vulkan backend。
- frame loop 是否只管理 acquire、submit、present、swapchain 生命周期，而不承载 renderer 策略。
- frame callback 是否声明 acquire semaphore 的正确 wait stage。
- `renderer-basic` 和 `renderer-basic-vulkan` 是否分层清楚。
- CMake target 依赖、package manifest 的 `dependencies` / `targetDependencies` 和源码 include 是否一致；
  多 target package 不能用 package-level dependency 代替 target-level 边界。
- swapchain recreate、image view、semaphore、fence、command buffer 的生命周期是否闭合。
- 文档是否同步更新了真实流程。

审查回复中必须写：

```text
设计审查：通过
内部设计审查：通过
参考资料：...
```

若未通过，必须列出设计 finding，并优先修复 P1/P2。

## 内部代码设计审查门禁

架构审查不得只验证 package、target、include 或 Vulkan 边界。每次 review 至少要抽样检查被改动代码及其直接调用者/被调用者的内部设计；涉及 editor、renderer、runtime、RenderGraph 或 RHI 的改动必须完整覆盖下列问题：

- 范式选择：先判断改动主要采用直接过程式、面向对象、数据导向、组件式/ECS 或某个明确设计模式。模式不是目标；
  只有当它对应真实变化点、所有权、生命周期或批处理需求，并且比直接函数/数据结构更清楚时才通过。
- OOP 与类不变量：`class` 应封装明确不变量、资源生命周期或稳定接口；仅把相关字段打包且成员可独立变化时优先
  `struct`、值对象或自由函数。`virtual` 边界必须说明调用方向、ownership、copy/move/slicing 策略和测试方式。
- 设计模式使用：采用 Component、Command、Observer/Event Queue、Factory/Builder、Registry、Strategy、State 等模式时，
  必须点名参与者、owner、注册/注销、执行顺序、错误路径和线程/帧边界；只出现 `Manager`、`Context` 或全局访问不算模式落地。
- 数据导向设计：热路径、批处理、资产/渲染/导入数据应审查数据布局、排序/迭代确定性、stable id/handle、SoA/AoS
  或紧凑 `std::vector` 选择、`reserve`/erase 策略、cache locality 和批量 transform；不能把每元素 work 隐藏在虚调用、
  `std::function` 或无序遍历后面。
- 抽象阈值：新增 facade、registry、polymorphic interface 或 PImpl 必须写出变化点、第二实现/调用方或降低复杂度的本地证据；
  否则以直接函数、局部 helper 或 package-private 类型收敛。
- 职责边界：类、manager、coordinator、registry、context 是否同时承担创建、调度、渲染、状态变更、诊断和 UI；超过一个稳定职责时必须说明拆分计划或当前保留理由。
- 数据合同：跨层输入是否真的被消费，而不是只进入 diagnostics；camera、overlay、format、descriptor、frame params、pass params 和 resource access 必须能追到实际执行点或明确标注为 planned。
- 生命周期：create/update/reload/resize/shutdown、GPU deferred deletion、descriptor retire、frame fence、command buffer、persistent/transient resource 是否形成闭环；不能靠隐式全局状态或 render loop 中的 wait idle 掩盖。
- 状态模型：功能是单实例还是多实例；viewport、view、panel、world、document、selection、capture、refresh reason 等状态必须有 owner，不能用最后一次请求覆盖多视图需求。
- 隐式执行：GPU work、上传、clear、copy、barrier、descriptor update 或 debug probe 是否藏在声明式 graph / frame loop 之外；若保留 external pre-pass，必须在 diagnostics 和审查结论中显式暴露。
- 错误与能力合同：format、feature、queue capability、shader reflection、resource signature、descriptor layout 和 material/pipeline key 不匹配时，是否能 fail early 并保留上下文。
- shader/material adapter 改动必须验证 reflection model 到 `MaterialResourceSignature` 的 descriptor kind、
  stage visibility、set/binding/name/count、hash stability 和 negative diagnostics；不能让 `material-core`
  依赖 Slang、renderer、Vulkan、RenderGraph、asset-pipeline 或 editor。
- Editor 内部设计：panel 不直接修改持久状态；持久 mutation 应通过 command/transaction 或明确 owner；宽 `Context` / service locator 只能作为过渡，并需要 capability-scoped 收敛计划。
- Public API 与实现：大型 header-only 组件、public inline 实现、app-level glue 文件和 god object 必须审查 API/implementation split；暂不拆时要记录触发拆分的阈值。
- 测试与 smoke：新增或修改的内部语义必须有 smoke、package test、counter、diagnostics snapshot 或负向测试证明；只靠编译通过不算内部设计通过。

审查回复中若内部设计适用，必须额外写出：

```text
内部设计范式：direct / OOP / data-oriented / pattern-name / mixed，理由：...
模式/抽象判断：通过 / 未通过 / 不适用，依据：...
数据布局与迭代判断：通过 / 未通过 / 不适用，依据：...
```

快速抽样命令：

```powershell
rg -n "class |struct |Manager|Coordinator|Registry|Context|State|TODO|FIXME|temporary|MVP|for now" apps engine packages -g "*.hpp" -g "*.cpp" -g "*.inl"
rg -n "virtual|override|final|interface|Strategy|Factory|Builder|Observer|Command|Visitor|ServiceLocator|Singleton|Manager|Coordinator|Registry|Context|State" apps engine packages -g "*.hpp" -g "*.cpp" -g "*.inl"
rg -n "std::vector|std::span|std::array|std::unordered|std::map|std::function|std::variant|shared_ptr|unique_ptr|new |delete |reserve\(|erase\(|stable_sort|sort\(" apps engine packages -g "*.hpp" -g "*.cpp" -g "*.inl"
rg -n "vkCmd|vkQueue|vkDeviceWaitIdle|vkQueueWaitIdle|vkUpdateDescriptorSets" apps packages -g "*.hpp" -g "*.cpp" -g "*.inl"
rg -n "debugWorldLines|camera|viewProjection|viewportSlots_|requestedViewport_|RenderGraphImageFormat::Undefined|basicRenderGraphImageFormat" apps packages -g "*.hpp" -g "*.cpp" -g "*.inl"
```

审查发现必须用本地事实举证：给出文件、行号、调用路径和触发场景。若结合网络资料，必须说明资料只支持哪条设计判断，不能用泛泛 best practice 替代仓库证据。

### Renderer format contract gate

修改 swapchain format、RenderView target format、RenderGraph image format 或 Vulkan image create 入口时，必须检查 renderer format contract。若改动引入或修改 `--smoke-renderer-format-contract`，该 smoke 必须在 PR 描述和审查回复中列为验证门禁。

当前 `--smoke-renderer-format-contract` 已进入 frame loop / RenderGraph / renderer / Vulkan adapter smoke 清单；后续新增 format、offscreen target、material/pipeline format key 或 texture preview 范围时，必须继续证明 unsupported format 会在 renderer / RenderGraph import 前 fail early，不能重新引入 `RenderGraphImageFormat::Undefined` fallback。

## Vulkan 同步审查重点

- 使用 `vkQueueSubmit2` 时，`VkSemaphoreSubmitInfo::stageMask` 必须覆盖等待资源的首次实际使用阶段。
- transfer clear 路径可等待 `VK_PIPELINE_STAGE_2_TRANSFER_BIT`。
- dynamic rendering color attachment 路径应等待 `VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT`。
- triangle dynamic rendering 路径应等待 `VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT`，并用 dynamic viewport/scissor 覆盖当前 swapchain extent。
- 若 callback 无法精确声明阶段，短期 fallback 可使用 `VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT`，但应记录为待细化问题。
- layout transition 应由 RenderGraph 编译结果经 Vulkan adapter 生成，避免在业务层手写重复 barrier。

## 包边界审查重点

- `asharia::rhi_vulkan` 是基础 Vulkan 后端，不公开依赖 RenderGraph。
- `asharia::rhi_vulkan_rendergraph` 是 RenderGraph 到 Vulkan 的翻译 target。
- `asharia::renderer_basic` 保持后端无关，只描述 basic renderer graph 片段。
- `asharia::renderer_basic_vulkan` 负责 Vulkan 命令录制。
- app 可以承载 smoke 入口，但不应持有 pass/barrier/pipeline 编排细节。

## 文档同步要求

以下变化必须同步文档：

- 新增或修改 smoke 命令。
- 修改包依赖、target 依赖或 manifest。
- 修改 RenderGraph pass/resource/transition 语义。
- 修改 frame loop、swapchain、同步、资源生命周期。
- 新增 renderer backend 或 shader pipeline 阶段。

`tools\check-doc-sync.ps1` 会在 `apps/`、`engine/`、`packages/`、`shaders/`、CMake/Conan、
`scripts/`、`tools/` 或 GitHub Actions workflow 发生变化但没有文档变化时失败。若确认无需文档更新，
必须在 PR 模板中说明原因；本地临时验证可使用 `-NoDocsReason` 显式给出原因。脚本默认只检查 tracked
diff；需要把未跟踪文件也纳入本地自检时，显式使用 `-IncludeUntracked`。

按改动范围更新唯一事实源：

- package/target/manifest：`docs/architecture/package-first.md`、`docs/architecture/flow.md`。
- frame loop、RHI、renderer、RenderGraph：`docs/architecture/flow.md`、
  `docs/rendergraph/rhi-boundary.md`、`docs/architecture/render-layer.md`。
- asset/resource/scene/schema/material/script：对应 `docs/systems/` 文档和稳定规格。
- native editor：`docs/architecture/editor.md`；Studio 实现细节：
  `apps/studio/docs/architecture/README.md` 及其直接链接文档。
- 构建、smoke、CI：`docs/workflow/build.md`、`docs/workflow/review.md`。
- 新增或删除稳定入口：`docs/README.md`。

不要为了发布或翻译复制第二套工程事实；文档站从 `docs/` 单向同步。

## 提交规则

- 只暂存本次任务相关文件。
- 不提交用户已有的无关本地改动。
- 提交前再跑一次 `git status --short`。
- 提交回复必须包含 commit hash 和已通过门禁。

## 阶段完成 tag

当某个阶段的 Issue/Epic 验收标准已经完成实现、当前部署文档同步和对应验收门禁后，可以给完成该阶段的提交打 tag。

- tag 只打在“完成阶段验收”的实现提交上；如果后续只是补文档或流程，不移动已有阶段 tag。
- 命名使用 `stage-<number>-<short-slug>`，例如 `stage-14-render-view-target-recording`。
- 优先使用 annotated tag，消息格式为 `Stage <number>: <stage title>`。
- 打 tag 前必须确认该阶段的验收标准已在当前机器跑过，并在提交回复中列出关键门禁。
- 如果阶段仍有 P1/P2 finding、未解释的 validation error、失败 smoke 或未同步文档，不打 tag。
- 打 tag 后再跑一次 `git tag --list "stage-*"` 或 `git show <tag>` 核实 tag 指向正确提交。

### Fullscreen smoke resource inventory

Fullscreen/offscreen descriptor checks cover all three 16-set rings: fullscreen, composite,
and selection outline (48 sets in one allocation). These smoke paths drain the graphics queue
before checking end-of-run diagnostics so a failed assertion cannot destroy in-flight resources.
This is teardown synchronization, not a render-loop idle wait. Fullscreen's current fixture only
allocates one debug-line buffer, so its existing exact four-buffer upload assertion remains.
The renderer package manifest includes the three selection shader order dependencies and is
checked against the configured CMake File API graph, not just source topology.
