#!/usr/bin/env python3
"""Conservative heuristic review scanner for Asharia Vulkan/C++23 code.

Findings are prompts for human review, not proof of defects. The scanner has no
third-party dependencies so the same gate can run locally and in hosted CI.
"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


SOURCE_EXTENSIONS = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".ipp",
    ".inl",
    ".ixx",
}
BUILD_EXTENSIONS = {".cmake"}
EXCLUDED_DIRS = {
    ".git",
    ".vs",
    ".vscode",
    "build",
    "cmake-build-debug",
    "cmake-build-release",
    "external",
    "third_party",
    "vendor",
    "node_modules",
}
SEVERITY_RANK = {"info": 0, "warning": 1, "error": 2}

VK_RESULT_CALL_RE = re.compile(
    r"\b(vk(?:Create|Allocate|Map|Bind|Begin|End|QueuePresent|AcquireNextImage|"
    r"WaitForFences|ResetFences|QueueSubmit)[A-Za-z0-9_]*)\s*\("
)
STD_RE = re.compile(r"\bCMAKE_CXX_STANDARD\s+([0-9]+)\b")
FEATURE_RE = re.compile(r"\bcxx_std_([0-9]+)\b")
VULKAN_TYPE_RE = re.compile(r"\bVk[A-Z][A-Za-z0-9_]*\b|\bVK_[A-Z0-9_]+\b")
PACKAGE_SRC_INCLUDE_RE = re.compile(
    r'#\s*include\s+[<"][^"<>]*(?:packages[\\/][^"<>\\/]+[\\/]|\.\.[\\/][^"<>]*?)src[\\/]',
    re.IGNORECASE,
)
TARGET_LINK_RE = re.compile(
    r"target_link_libraries\s*\((.*?)\)", re.IGNORECASE | re.DOTALL
)


@dataclass(frozen=True)
class Finding:
    severity: str
    rule: str
    path: str
    line: int
    message: str
    suggestion: str


def is_candidate(path: Path) -> bool:
    return (
        path.name == "CMakeLists.txt"
        or path.suffix.lower() in SOURCE_EXTENSIONS | BUILD_EXTENSIONS
    )


def iter_files(paths: Iterable[Path]) -> Iterable[Path]:
    seen: set[Path] = set()
    for root in paths:
        if root.is_file():
            candidates = (root,) if is_candidate(root) else ()
        elif root.exists():
            candidates = (
                path
                for path in root.rglob("*")
                if path.is_file()
                and is_candidate(path)
                and not ({part.lower() for part in path.parts} & EXCLUDED_DIRS)
            )
        else:
            candidates = ()
        for path in candidates:
            resolved = path.resolve()
            if resolved not in seen:
                seen.add(resolved)
                yield resolved


def add(
    findings: list[Finding],
    severity: str,
    rule: str,
    path: Path,
    line_no: int,
    message: str,
    suggestion: str,
) -> None:
    findings.append(Finding(severity, rule, str(path), line_no, message, suggestion))


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def vk_result_checked(line: str, match: re.Match[str]) -> bool:
    prefix = line[: match.start()]
    if re.search(r"(?:^|\W)(?:return|co_return)\s+$", prefix):
        return True
    if re.search(r"(?:^|[;{}])[^;{}]*(?<![=!<>])=(?!=)\s*$", prefix):
        return True
    if re.search(r"(?:VK_CHECK|VK_ASSERT|checkVk|check_vk|throw_if)\s*\([^)]*$", prefix):
        return True
    if re.search(r"\bif\s*\([^)]*$", prefix):
        return True
    return False


def scan_cmake(path: Path, text: str, lines: list[str], findings: list[Finding]) -> None:
    saw_standard = False
    for line_no, line in enumerate(lines, start=1):
        rules = (
            (STD_RE, "cpp23.cmake-standard"),
            (FEATURE_RE, "cpp23.target-feature"),
        )
        for pattern, rule in rules:
            match = pattern.search(line)
            if not match:
                continue
            saw_standard = True
            value = int(match.group(1))
            if value < 23:
                add(
                    findings,
                    "error",
                    rule,
                    path,
                    line_no,
                    f"CMake requests C++{value}, below C++23.",
                    "Require C++23 unless the project intentionally pins an older language mode.",
                )

    for match in TARGET_LINK_RE.finditer(text):
        tokens = re.findall(r"[A-Za-z0-9_:.-]+", match.group(1))
        if not tokens or tokens[0] != "asharia-rhi-vulkan":
            continue
        if "asharia::rendergraph" in tokens or "asharia-rendergraph" in tokens:
            add(
                findings,
                "warning",
                "vkengine.rhi-vulkan-rendergraph-dependency",
                path,
                line_number(text, match.start()),
                "The base asharia-rhi-vulkan target appears to link RenderGraph.",
                "Keep the base RHI independent; link RenderGraph only from "
                "asharia-rhi-vulkan-rendergraph.",
            )

    if path.name == "CMakeLists.txt" and not saw_standard:
        add(
            findings,
            "info",
            "cpp23.standard-missing",
            path,
            1,
            "No C++ standard requirement was detected in this CMake file.",
            "Confirm C++23 is inherited from the repository target configuration.",
        )


def scan_source(path: Path, lines: list[str], findings: list[Finding]) -> None:
    normalized_path = "/" + path.as_posix().lower().lstrip("/")
    is_rendergraph = "/packages/rendergraph/" in normalized_path
    is_rhi_vulkan_public = "/packages/rhi-vulkan/include/asharia/rhi_vulkan/" in normalized_path
    is_renderer_basic_public = (
        "/packages/renderer-basic/include/asharia/renderer_basic/" in normalized_path
    )
    is_renderer_basic_vulkan_public = (
        "/packages/renderer-basic/include/asharia/renderer_basic_vulkan/" in normalized_path
    )

    for line_no, line in enumerate(lines, start=1):
        stripped = line.strip()
        if stripped.startswith("//"):
            continue

        if PACKAGE_SRC_INCLUDE_RE.search(line):
            add(
                findings,
                "warning",
                "vkengine.package-private-include",
                path,
                line_no,
                "Include appears to reference a package src directory.",
                "Consume the package public include API and asharia::<name> target.",
            )

        if is_rendergraph and (
            "#include <vulkan/" in line
            or '#include "asharia/rhi_vulkan/' in line
            or VULKAN_TYPE_RE.search(line)
        ):
            add(
                findings,
                "warning",
                "vkengine.rendergraph-vulkan-boundary",
                path,
                line_no,
                "RenderGraph code appears to reference Vulkan symbols or headers.",
                "Keep RenderGraph backend-agnostic and translate in rhi_vulkan_rendergraph.",
            )

        if is_rhi_vulkan_public and '#include "asharia/rendergraph/' in line:
            add(
                findings,
                "warning",
                "vkengine.rhi-vulkan-public-rendergraph",
                path,
                line_no,
                "The base rhi_vulkan public API appears to include RenderGraph.",
                "Expose translation through asharia::rhi_vulkan_rendergraph.",
            )

        if (
            is_renderer_basic_public
            and not is_renderer_basic_vulkan_public
            and (
                "#include <vulkan/" in line
                or '#include "asharia/rhi_vulkan/' in line
                or VULKAN_TYPE_RE.search(line)
            )
        ):
            add(
                findings,
                "warning",
                "vkengine.renderer-basic-backend-boundary",
                path,
                line_no,
                "Backend-agnostic renderer_basic API appears to reference Vulkan.",
                "Move Vulkan code to renderer_basic_vulkan.",
            )

        if "#include <vulkan/vulkan.h>" in line:
            add(
                findings,
                "info",
                "vulkan.loader-strategy",
                path,
                line_no,
                "Direct vulkan.h include detected.",
                "Confirm this matches the repository's raw Vulkan header strategy.",
            )

        if re.search(r"\busing\s+namespace\s+std\s*;", line):
            add(
                findings,
                "warning",
                "cpp.namespace-std",
                path,
                line_no,
                "Global using namespace std detected.",
                "Use qualified names or narrow declarations.",
            )

        if re.search(r"\bnew\s+[A-Za-z_:]|\bdelete\s+|\bmalloc\s*\(|\bfree\s*\(", line):
            add(
                findings,
                "warning",
                "cpp.raw-ownership",
                path,
                line_no,
                "Raw allocation/deallocation detected.",
                "Use RAII, containers, smart pointers, or project allocators.",
            )

        if "vkDeviceWaitIdle(" in line:
            add(
                findings,
                "warning",
                "vulkan.device-wait-idle",
                path,
                line_no,
                "vkDeviceWaitIdle can hide synchronization bugs and stall the GPU.",
                "Restrict it to documented shutdown, debug, or recovery paths.",
            )

        if (
            "vkCmdPipelineBarrier(" in line
            or "VkImageMemoryBarrier " in line
            or "VkBufferMemoryBarrier " in line
        ):
            add(
                findings,
                "warning",
                "vulkan.sync2",
                path,
                line_no,
                "Legacy synchronization API detected.",
                "Prefer synchronization2 and verify producer/consumer stages and access masks.",
            )

        if (
            "VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT" in line
            or "VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT" in line
        ):
            add(
                findings,
                "warning",
                "vulkan.pipeline-stage-broad",
                path,
                line_no,
                "TOP_OF_PIPE/BOTTOM_OF_PIPE stage detected.",
                "Use precise synchronization2 stages unless this is justified.",
            )

        if "vkAllocateMemory(" in line:
            add(
                findings,
                "warning",
                "vulkan.memory-allocation",
                path,
                line_no,
                "Direct vkAllocateMemory call detected.",
                "Use VMA or the project allocation facade.",
            )

        if "vkCreateSwapchainKHR(" in line:
            add(
                findings,
                "info",
                "vulkan.swapchain-recreation",
                path,
                line_no,
                "Swapchain creation detected.",
                "Review resize, oldSwapchain handoff, failure cleanup, and in-flight lifetime.",
            )

        for match in VK_RESULT_CALL_RE.finditer(line):
            if not stripped.startswith("PFN_") and not vk_result_checked(line, match):
                add(
                    findings,
                    "warning",
                    "vulkan.vkresult-unchecked",
                    path,
                    line_no,
                    f"{match.group(1)} appears to be called without an obvious VkResult check.",
                    "Route the VkResult through explicit project error handling.",
                )


def scan_file(path: Path) -> list[Finding]:
    try:
        text = path.read_text(encoding="utf-8-sig")
    except (OSError, UnicodeError) as exc:
        return [
            Finding(
                "warning",
                "scanner.read-failed",
                str(path),
                1,
                f"Could not read file: {exc}",
                "Check file permissions and UTF-8 encoding.",
            )
        ]

    lines = text.splitlines()
    findings: list[Finding] = []
    if path.name == "CMakeLists.txt" or path.suffix.lower() in BUILD_EXTENSIONS:
        scan_cmake(path, text, lines, findings)
    if path.suffix.lower() in SOURCE_EXTENSIONS:
        scan_source(path, lines, findings)
    return findings


def print_text(findings: list[Finding]) -> None:
    if not findings:
        print("No findings.")
        return
    counts = {severity: 0 for severity in SEVERITY_RANK}
    for finding in findings:
        counts[finding.severity] += 1
        print(
            f"{finding.severity.upper()} {finding.path}:{finding.line} "
            f"[{finding.rule}] {finding.message}"
        )
        print(f"  Suggestion: {finding.suggestion}")
    print(
        f"\nSummary: {counts['error']} error(s), "
        f"{counts['warning']} warning(s), {counts['info']} info item(s)."
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Heuristically review Asharia Vulkan/C++23 source and CMake files."
    )
    parser.add_argument(
        "paths", nargs="*", default=["."], help="Files or directories to scan"
    )
    parser.add_argument(
        "--format", choices=("text", "json"), default="text", help="Output format"
    )
    parser.add_argument(
        "--fail-on",
        choices=("error", "warning", "info"),
        default="error",
        help="Return non-zero at this severity or higher",
    )
    args = parser.parse_args()

    findings = [
        finding
        for path in iter_files(Path(item).resolve() for item in args.paths)
        for finding in scan_file(path)
    ]
    findings.sort(
        key=lambda item: (
            -SEVERITY_RANK[item.severity],
            item.path,
            item.line,
            item.rule,
        )
    )
    if args.format == "json":
        print(json.dumps([asdict(finding) for finding in findings], indent=2))
    else:
        print_text(findings)
    threshold = SEVERITY_RANK[args.fail_on]
    return int(any(SEVERITY_RANK[finding.severity] >= threshold for finding in findings))


if __name__ == "__main__":
    raise SystemExit(main())
