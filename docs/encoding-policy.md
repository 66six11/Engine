# 文本编码策略

## 目标

仓库统一使用 UTF-8，但不对所有文件一刀切添加 BOM。UTF-8 BOM 只在确实提高兼容性的
文件类型中使用；对 JSON、CMake、Markdown、Python、Git/Clang 配置等工具链文件，默认
使用 UTF-8 without BOM。

## 依据

- Unicode FAQ：UTF-8 没有字节序问题，BOM 在 UTF-8 中只是编码签名；某些协议或文件格式
  可能要求或禁止 BOM。
- RFC 8259：JSON 生成器不得在 JSON 文本开头添加 BOM。解析器可以选择忽略 BOM，但不能
  依赖所有工具都会这么做。
- Microsoft PowerShell 文档：PowerShell 6+ 默认输出 UTF-8 without BOM；但旧 Windows
  PowerShell 在读取包含非 ASCII 的脚本时可能需要 UTF-8 with BOM 才能避免按 ANSI code
  page 误读。
- 本项目实测：Conan 生成的 `CMakeUserPresets.json` 带 BOM 后会触发 Python JSON 解析错误。

## 要求 UTF-8 with BOM

- C/C++ 源码和头文件：`.c`、`.cc`、`.cpp`、`.cxx`、`.h`、`.hh`、`.hpp`、`.hxx`、
  `.ipp`、`.inl`。
- PowerShell 脚本：`.ps1`。

理由：

- 当前主平台是 Windows + MSVC，源码中后续可能出现中文注释或非 ASCII 文本，BOM 可以降低
  旧工具误判编码的概率。
- `.ps1` 兼容 Windows PowerShell 5.1 的非 ASCII 读取行为。

## 禁止 UTF-8 BOM

- JSON：`.json`，包括 `CMakePresets.json`、`vke.package.json`、Conan 生成的
  `CMakeUserPresets.json`。
- CMake：`CMakeLists.txt`、`.cmake`。
- Python：`.py`。
- Markdown/text 文档：`.md`、`.txt`。
- Git/Clang/EditorConfig 配置：`.gitignore`、`.gitattributes`、`.clang-format`、
  `.clang-tidy`、`.editorconfig`。
- Shader 文本：`.slang`、`.vert`、`.frag`、`.comp` 等。

理由：

- JSON 有明确互操作要求，不应添加 BOM。
- 构建、脚本、配置文件经常被 Python、CMake、Git、Conan、格式化器等工具读取；这些工具
  不一定统一忽略 BOM。
- Shader 编译器链路后续会接 Slang/SPIR-V 工具，先保持 BOM-less UTF-8，除非某个工具明确
  要求 BOM。

## 工具

使用 `tools/check-text-encoding.ps1` 检查和修正：

```powershell
powershell -ExecutionPolicy Bypass -File tools/check-text-encoding.ps1
powershell -ExecutionPolicy Bypass -File tools/check-text-encoding.ps1 -Fix
```

脚本行为：

- 对要求 BOM 的文件检查 UTF-8 BOM 是否存在，缺失时 `-Fix` 会添加。
- 对禁止 BOM 的文件检查是否有 UTF-8 BOM，存在时 `-Fix` 会移除。
- 所有纳入检查的文件都会先按严格 UTF-8 解码；遇到非法 UTF-8 会报错，不做自动改写。
