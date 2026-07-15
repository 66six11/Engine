# ADR: Windows Development Host Template v1

## 状态

Accepted and implemented for #290. 本 ADR 冻结第一个 final native Host target、受控构建和 registration-only
verification 边界。它消费 #287 的 immutable static-composition generation 与 #289 的 identity-only recorder，
但不创建 factory instance，也不自行发布 #288 的
[Host Executable Binding Receipt](adr-host-executable-binding-receipt-v1.md)；后者已作为独立 downstream publisher 实现。

## 问题

现有 generated static composition root 能把 exact provider targets、generated source 和
`asharia::host_runtime_registration` 附加到一个已经存在的 executable，然而它刻意不拥有：

- final target 名称、`main()`、Windows subsystem 与输出布局；
- CMake configure/build 进程；
- build 后的 final target/configuration/artifact-path 观察；
- 从 exact built Host 取得 registration snapshot 的进程边界。

如果把这些职责塞回 package resolver、generated root 或 recorder，就会让依赖选择、源码生成、构建执行和运行证据重新
混成一个边界。#290 只补齐最小可执行 Host 闭环。

## 决策摘要

v1 只提供一个固定的 `windows-development-v1` 模板：

```text
verified static-composition generation
  -> immutable Host Template generation
  -> explicit CMake configure
  -> latest final CMake File API binding
  -> explicit single-target build
  -> refresh final binding + regular-file check
  -> exact Host --asharia-verify-static-registration
  -> canonical RegistrationSnapshot JSON on stdout
```

该模板是开发期构建模板，不是通用 Build Profile、产品 staging 系统或完整 Runtime Host。

## 所有权

| 能力 | owner | 不拥有 |
| --- | --- | --- |
| target name、唯一 `main()`、console subsystem、runtime output layout | Host Template generation | package resolution、provider enumeration |
| static source/include/provider target attachment | generated static-composition fragment | final target、进程执行 |
| configure/build 参数与受控环境 | Final Host Build Adapter | shell 文本、Conan resolution、Cook/Stage |
| latest File API index、exact configuration/target/primary artifact | Final Host Target Binder | artifact bytes/hash、receipt |
| recorder 执行与 canonical JSON stdout | restricted Host verification mode | factory instance、activation lifecycle |
| exact composition path、template path、build root、toolchain 与环境 | caller | 隐式工作目录或全盘扫描 |

`engine/package-runtime`、resolver 与 Project Lock 不执行 CMake，也不启动 Host。`engine/host-runtime` 只增加 snapshot 的纯
canonical JSON renderer；文件系统与子进程仍属于 build tooling。

## 1. Host Template generation

生成物固定为：

```text
asharia.windows-development-host-template.json
asharia-host-template.cmake
src/main.cpp
```

manifest 绑定：

- `windows-development-v1` renderer revision；
- exact static-composition generation ID 与 manifest SHA-256；
- Engine generation、Host kind、Windows target platform 与 configuration；
- final logical target name、console subsystem 与 runtime output layout；
- 每个生成文件的 size 与 SHA-256。

generation ID 只由 canonical descriptor 计算，不包含绝对路径、timestamp、PID 或 filesystem enumeration order。发布使用
staging directory、exact-byte re-read 和 atomic directory rename；同 generation 的完整内容可以复用。

生成的 CMake fragment：

1. 要求 Windows 平台与显式 `ASHARIA_STATIC_COMPOSITION_ROOT`；
2. 创建唯一 `EXECUTABLE` target；
3. 使用 console subsystem，保留 verification stdout/stderr；
4. 把 runtime 输出固定在 build root 内的 `asharia-host/bin/$<CONFIG>`；
5. include exact static-composition fragment 并只调用一次 `asharia_attach_static_composition()`；
6. 复验 target 上的 `ASHARIA_STATIC_COMPOSITION_GENERATION_ID` 与模板绑定值一致。

仓库根 CMake 只提供一个显式、默认关闭的 include hook。未传 Host Template root 时，现有开发者 configure 行为不变。

## 2. Restricted verification main

v1 `main()` 只接受：

```text
--asharia-verify-static-registration
```

合法路径固定为：

```text
generated capacity
  -> create recorder
  -> generated provider calls
  -> finish owning snapshot
  -> canonical JSON renderer
  -> exact UTF-8/LF bytes to stdout
  -> exit 0
```

成功时 stdout 只能包含一个 canonical snapshot；stderr 必须为空。失败时不输出 partial JSON，stderr 只写稳定错误码并返回非零。
Windows CRT stdout 采用 binary mode，避免把 canonical LF 改写为 CRLF。

verification mode 不枚举 provider、不读取 manifest、不创建 callback table，也不执行
create/activate/quiesce/deactivate/destroy。

## 3. Final configure/build adapter

Build Request 是 typed、单次使用输入，至少包含：

- explicit `cmake` executable、source/build/template/composition/toolchain paths；
- 完整 Host Template generation 与 static-composition generation，而不只是两份 manifest；
- exact target name、configuration、generator kind 与 single/multi-config fact；
- positive parallel job count；
- caller-supplied complete environment map；
- configure 与 build timeout。

adapter 自己构造参数数组并以 `shell=False` 执行。它不接受 shell command string、native-tool passthrough 或隐式当前目录。
在写 File API query 或启动 CMake 前，adapter 必须重算两份 generation 的 identity/self-integrity，并按内存中的 exact
file set 只读复验两个发布目录；manifest、CMake、`main.cpp`、composition source/header 或任意额外文件、目录、link/reparse
不一致时都 fail closed。

registration verification 使用独立 typed request，另行拥有 verification timeout 与最大 snapshot bytes；它不把进程策略塞回
build request。

单配置 configure 显式设置 `CMAKE_BUILD_TYPE`；多配置只在 build 时传 `--config`。build 固定为：

```text
cmake --build <build-root> --target <exact-target> [--config <configuration>] --parallel <N>
```

不使用 `--clean-first`，保留增量编译收益。Conan 仍由 caller 在进入本 adapter 前完成；adapter 只消费已存在的 toolchain file
和受控 compiler environment。

## 4. CMake File API final binding

configure 前写入 stateful client query：

```text
<build>/.cmake/api/v1/query/client-asharia-host-build-v1/query.json
```

请求 codemodel v2.6。configure 成功后读取 reply 目录中字典序最大的 `index-*.json`，只跟随本 client response 的安全
`jsonFile` 引用。读取期间 index 改变或引用被并发 regenerate 移除时，从最新 index 有限重试；最终不稳定则 fail closed。

binding 依次要求：

1. exact configuration 恰好出现一次；
2. exact logical target 恰好出现一次；
3. target object identity 与 summary 一致且 `type == EXECUTABLE`；
4. `nameOnDisk` 存在；
5. artifacts 中恰好一个 basename 与 `nameOnDisk` 相同；
6. primary artifact 解析后仍位于 caller-owned build root；
7. build 成功后重新读取 latest reply，并要求 primary artifact 是普通文件。

File API 只证明 CMake 的 target/configuration/path 事实，不证明 artifact bytes。#288 已在独立
[Host Executable Binding Receipt v1](adr-host-executable-binding-receipt-v1.md) 中补齐 configured compiler、SHA-256、size、
collector-owned staged execution 与 immutable publication。

## 5. RegistrationSnapshot handoff

canonical JSON shape 固定为：

```json
{
  "schema": "com.asharia.static-factory-registration-snapshot",
  "schemaVersion": 1,
  "generationId": "sha256-...",
  "hostActivationBlueprintSha256": "...",
  "registrations": []
}
```

registration records 保持 #289 snapshot 已有的 UTF-8 byte canonical order。Python consumer 复验 closed schema、canonical
fixed-field bytes、无重复 registration、expected generation 与 expected Blueprint digest；任何失败都不暴露部分 snapshot。

## 6. 失败边界

稳定诊断至少覆盖：

- template/composition/toolchain/environment 输入无效或过期；
- File API query、configure 或 build 失败/超时；
- latest reply 缺失、变化、malformed 或 client response 不匹配；
- configuration/target 缺失、重复、类型错误；
- primary artifact 缺失、歧义、逃逸 build root 或 build 后不是普通文件；
- verification spawn、timeout、非零退出、意外 stderr 或非 canonical stdout；
- snapshot generation/Blueprint 与模板绑定值不一致。

失败 outcome 只含 stage、exit facts 与 owning diagnostics，不含 partial target evidence 或 partial snapshot。

## 编译效率边界

- Host Template 只新增一个很薄的 `main.cpp`；provider/module 继续保持独立静态 targets；
- generated static composition 仍只有一个薄 TU；
- build adapter 只构建 exact Host target，不触发默认 all target；
- 不使用 clean-first，不增加 PCH、unity、module scan 或全局 clang-tidy policy；
- generation 与 template 内容相同则复用，不因 timestamp 重写源文件。

## 不做事项

- 通用 Build Profile inheritance、Cook、Stage、Package、Deploy 或 Launch Session；
- Editor/Bootstrap UI、ImGui 或 Avalonia；
- factory callbacks、instance、scope、activation DAG、rollback 或 shutdown；
- artifact hash/publication、Host Executable Binding Receipt、signing、installer 或 repair；
- dynamic plugin、hot unload 或 stable cross-generation ABI；
- 任意 package code 的运行时发现或 filesystem 扫描。

## 官方依据

- [CMake File API 3.28](https://cmake.org/cmake/help/v3.28/manual/cmake-file-api.7.html)
- [CMake build mode 3.28](https://cmake.org/cmake/help/v3.28/manual/cmake.1.html#build-a-project)
- [CMAKE_BUILD_TYPE](https://cmake.org/cmake/help/v3.28/variable/CMAKE_BUILD_TYPE.html)
- [CMAKE_CONFIGURATION_TYPES](https://cmake.org/cmake/help/v3.28/variable/CMAKE_CONFIGURATION_TYPES.html)

## 验证要求

- deterministic template generation、immutable publication 与 stale composition negatives；
- exact File API target/configuration/artifact positive/negative fixtures；
- controlled subprocess configure/build failure and timeout tests；
- exact built Host restricted verification 在 MSVC 与 ClangCL 下运行并产生同语义 snapshot；
- full Python/contracts/topology/encoding/doc-sync/diff/Vulkan review；
- Conan-before-CMake MSVC 与 ClangCL builds/tests。

## 后续

#288 已消费本 Slice 的 final target evidence、exact Host path 与 canonical registration snapshot，并发布
[Host Executable Binding Receipt v1](adr-host-executable-binding-receipt-v1.md)。receipt 运行 collector-owned staged executable，
而不是 mutable build-tree executable；具体 Host Runtime lifecycle 与 Bootstrap/Session 状态映射继续保持独立 Slice。
