# 文本编码策略

## 目标

仓库统一使用 UTF-8，但不对所有文件一刀切添加 BOM。UTF-8 BOM 只用于 C/C++ 源代码和头文件；工具链文本、脚本、文档和配置文件默认使用 UTF-8 without BOM。

## 依据

- UTF-8 没有字节序问题，BOM 在 UTF-8 中只是一种编码签名。
- JSON、CMake、Python、Markdown、Git/Clang 配置等工具链文件经常被多种工具读取，BOM 可能触发兼容性问题。
- 当前主平台是 Windows + MSVC，C/C++ 源码中后续可能出现中文注释或非 ASCII 文本，BOM 可以降低旧工具误判编码的概率。
- 本项目实测：Conan 生成的 `CMakeUserPresets.json` 带 BOM 后会触发 Python JSON 解析错误。

## 要求 UTF-8 with BOM

- C/C++ 源代码和头文件：`.c`、`.cc`、`.cpp`、`.cxx`、`.h`、`.hh`、`.hpp`、`.hxx`、`.ipp`、`.inl`。

## 禁止 UTF-8 BOM

- JSON：`.json`，包括 `CMakePresets.json`、`asharia.package.json`、Conan 生成的 `CMakeUserPresets.json`。
- CMake：`CMakeLists.txt`、`.cmake`。
- Python：`.py`。
- PowerShell：`.ps1`。
- Markdown/text 文档：`.md`、`.txt`。
- Git/Clang/EditorConfig 配置：`.gitignore`、`.gitattributes`、`.clang-format`、`.clang-tidy`、`.editorconfig`、`CODEOWNERS`。
- Shader 文本：`.slang`、`.vert`、`.frag`、`.comp` 等。

## 工具

使用 `tools/check-text-encoding.ps1` 检查和修正：

```powershell
powershell -ExecutionPolicy Bypass -File tools/check-text-encoding.ps1
powershell -ExecutionPolicy Bypass -File tools/check-text-encoding.ps1 -Fix
```

脚本行为：

- 对要求 BOM 的文件检查 UTF-8 BOM 是否存在；缺失时 `-Fix` 会添加。
- 对禁止 BOM 的文件检查是否有 UTF-8 BOM；存在时 `-Fix` 会移除。
- 所有纳入检查的文件都会先按严格 UTF-8 解码；遇到非法 UTF-8 会报错，不做自动改写。
