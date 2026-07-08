# Standards：文本编码

## Rules

| File type | Encoding |
|---|---|
| C/C++ sources and headers: `.c`, `.cc`, `.cpp`, `.cxx`, `.h`, `.hh`, `.hpp`, `.hxx`, `.ipp`, `.inl` | UTF-8 with BOM |
| Everything else: `.json`, `.cmake`, `.py`, `.md`, `.slang`, `.txt`, `.ps1`, `.clang-format`, `.clang-tidy`、`CODEOWNERS` 等 root files | UTF-8 without BOM |

## Why It Matters

- 本仓库中的 MSVC/editor tooling 对 C/C++ source files 采用 BOM 规则。
- JSON、CMake、Python、Markdown、Slang、PowerShell 不应带 BOM。
- Conan-generated `CMakeUserPresets.json` 带 BOM 会触发 Python JSON parse errors。

## Check

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
```

常用参数：

| 参数 | 用途 |
|---|---|
| `-Root <path>` | 检查另一个 repository root |
| `-VerboseList` | 打印每个 checked file 和 detected encoding |
| `-Fix` | 按仓库策略重写文件编码 |

检查脚本会跳过 `.git`、`build` 和常见 dependency cache 等 generated/external 目录。

## Fix

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1 -Fix
```

## Review Checklist

- 新 `.md` files 是 UTF-8 without BOM。
- 新 `.cpp`/`.hpp` files 是 UTF-8 with BOM。
- generated files under `build/` 不提交。
- encoding fix 不重写无关文件。
