# 构建流程

本项目使用 Conan 2 + CMake Presets。日常开发以 MSVC 预设为主，代码检查以 ClangCL 预设为主；四个入口全部使用 Ninja 生成器。

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
`vulkan-memory-allocator` 等依赖漂移。

## 构建命令

在 Visual Studio 的 CMake 集成中，选择对应 preset 后配置和构建即可。

如果在普通 PowerShell 中构建 Ninja + MSVC/ClangCL，需要先加载 Conan 生成的 VS 编译环境：

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
cmd /c "build\conan\msvc-release\Release\generators\conanbuild.bat && cmake --preset msvc-release && cmake --build --preset msvc-release"
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\clangcl-release\Release\generators\conanbuild.bat && cmake --preset clangcl-release && cmake --build --preset clangcl-release"
```

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
