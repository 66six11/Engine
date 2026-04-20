# 构建流程

本项目的 CMake/Conan 入口遵循 Conan 官方推荐的 `CMakeToolchain` + `CMakeDeps` +
`CMakePresets` 流程。

## 依据

- Conan `CMakeToolchain` 生成 `conan_toolchain.cmake`，并生成包含 generator、toolchain
  路径、binaryDir、build/test preset 的 `CMakePresets.json`。
- Conan `CMakeToolchain` 应和 `CMakeDeps` 配套使用，不混用旧的 `cmake` 或
  `cmake_paths` generator。
- CMake 官方定位是：`CMakePresets.json` 可提交为项目共享配置，
  `CMakeUserPresets.json` 是开发者本地配置，不提交。
- 本项目采用 Conan 官方默认方式：Conan 生成根目录 `CMakeUserPresets.json`，它 include
  `build/generators/CMakePresets.json`。项目提交的 `CMakePresets.json` 不 include 生成文件，
  避免在干净仓库或清理 build 目录后出现断链。

## 首次或依赖变更后

先生成 Conan toolchain、dependency config 和 presets：

```powershell
conan install . --profile:host=profiles/windows-msvc-debug --profile:build=profiles/windows-msvc-debug --build=missing
conan install . --profile:host=profiles/windows-msvc-release --profile:build=profiles/windows-msvc-release --build=missing
```

这一步会生成：

- `build/generators/conan_toolchain.cmake`
- `build/generators/CMakePresets.json`
- `CMakeUserPresets.json`

这些文件都是本地生成物，不提交。

## 配置与构建

Conan 生成物存在后，统一通过项目 preset 进入 CMake：

```powershell
cmake --preset conan-default
cmake --build --preset conan-debug
cmake --build --preset conan-release
```

`conan-default`、`conan-debug` 和 `conan-release` 都由 `conan install` 生成。不要手写
`-DCMAKE_TOOLCHAIN_FILE=...` 作为常规路径，也不要直接编辑 Conan 生成的 preset。项目级
cache 选项通过 `conanfile.py` 写入 `CMakeToolchain`。

CLion 会把 CMake preset 导入为只读 CMake profile，并默认绑定 CLion 的默认 toolchain。
如果默认 toolchain 是 MinGW，IDE 会提示 CMake 实际使用的 MSVC 与配置的 MinGW 不兼容。
`conanfile.py` 会在 Conan 生成的 configure preset 中写入 JetBrains vendor 字段，把
`conan-default` 绑定到名为 `Visual Studio` 的 CLion toolchain。该名称必须和
`Settings > Build, Execution, Deployment > Toolchains` 中的工具链名称一致。

## 本地覆盖

个人机器上的临时配置放在 `CMakeUserPresets.json`。该文件只用于本地覆盖，并已被
`.gitignore` 忽略。
