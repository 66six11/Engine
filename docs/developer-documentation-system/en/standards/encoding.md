# Standards: Text Encoding

## Rules

| File type | Encoding |
|---|---|
| C/C++ sources and headers: `.c`, `.cc`, `.cpp`, `.cxx`, `.h`, `.hh`, `.hpp`, `.hxx`, `.ipp`, `.inl` | UTF-8 with BOM |
| Everything else: `.json`, `.cmake`, `.py`, `.md`, `.slang`, `.txt`, `.ps1`, `.clang-format`, `.clang-tidy`, root files such as `CODEOWNERS` | UTF-8 without BOM |

## Why It Matters

- MSVC/editor tooling may expect BOM for C/C++ source files in this repository.
- JSON, CMake, Python, Markdown, Slang, and PowerShell should avoid BOM.
- Conan-generated `CMakeUserPresets.json` with BOM can trigger Python JSON parse errors.

## Check

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
```

Useful options:

| Option | Purpose |
|---|---|
| `-Root <path>` | Check another repository root |
| `-VerboseList` | Print each checked file and detected encoding |
| `-Fix` | Rewrite files to the repository policy |

The checker ignores generated or external directories such as `.git`, `build`, and common dependency caches.

## Fix

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1 -Fix
```

## Review Checklist

- New `.md` files are UTF-8 without BOM.
- New `.cpp`/`.hpp` files are UTF-8 with BOM.
- Generated files under `build/` are not committed.
- Encoding fix does not rewrite unrelated files.
