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
    r"\b(vk(?:"
    r"(?:Create|Allocate|Map|Bind|Begin|End|QueuePresent|AcquireNextImage|"
    r"WaitForFences|ResetFences|QueueSubmit)[A-Za-z0-9_]*|"
    r"QueueWaitIdle|DeviceWaitIdle|"
    r"Enumerate(?:InstanceLayerProperties|InstanceExtensionProperties|"
    r"DeviceExtensionProperties|PhysicalDevices)|"
    r"GetPhysicalDeviceSurface(?:FormatsKHR|PresentModesKHR)|"
    r"GetSwapchainImagesKHR))\s*\("
)
STD_RE = re.compile(r"\bCMAKE_CXX_STANDARD\s+([0-9]+)\b")
FEATURE_RE = re.compile(r"\bcxx_std_([0-9]+)\b")
VULKAN_TYPE_RE = re.compile(r"\bVk[A-Z][A-Za-z0-9_]*\b|\bVK_[A-Z0-9_]+\b")
INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*[<"]\s*([^>"]+?)\s*[>"]', re.MULTILINE
)
PACKAGE_SRC_INCLUDE_RE = re.compile(
    r'#\s*include\s+[<"][^"<>]*(?:packages[\\/][^"<>\\/]+[\\/]|\.\.[\\/][^"<>]*?)src[\\/]',
    re.IGNORECASE,
)
TARGET_LINK_COMMAND_RE = re.compile(
    r"\btarget_link_libraries\s*\(", re.IGNORECASE
)
TARGET_REFERENCE_RE = re.compile(
    r"(?<![A-Za-z0-9_-])(?:asharia::[A-Za-z0-9_-]+|"
    r"asharia-[A-Za-z0-9_-]+|Vulkan::[A-Za-z0-9_-]+)(?![A-Za-z0-9_-])"
)

TARGET_ALIASES = {
    "asharia-rendergraph": "rendergraph",
    "asharia::rendergraph": "rendergraph",
    "asharia-rhi-vulkan": "rhi_vulkan",
    "asharia::rhi_vulkan": "rhi_vulkan",
    "asharia-renderer-basic": "renderer_basic",
    "asharia::renderer_basic": "renderer_basic",
}

VK_RESULT_HELPERS = (
    "VK_CHECK",
    "VK_ASSERT",
    "checkVk",
    "check_vk",
    "throw_if",
    "vulkanError",
    "vkResultName",
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
        else:
            candidates = (
                path
                for path in root.rglob("*")
                if path.is_file()
                and is_candidate(path)
                and not ({part.lower() for part in path.parts} & EXCLUDED_DIRS)
            )
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


def mask_cpp_non_code(text: str) -> str:
    """Mask comments and literals while retaining source length and newlines."""

    output = list(text)
    index = 0

    def mask(start: int, end: int) -> None:
        for position in range(start, end):
            if output[position] not in "\r\n":
                output[position] = " "

    while index < len(text):
        if text.startswith("//", index):
            end = text.find("\n", index + 2)
            end = len(text) if end < 0 else end
            mask(index, end)
            index = end
            continue
        if text.startswith("/*", index):
            closing = text.find("*/", index + 2)
            end = len(text) if closing < 0 else closing + 2
            mask(index, end)
            index = end
            continue

        raw_match = re.match(r'(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(', text[index:])
        if raw_match:
            terminator = ")" + raw_match.group(1) + '"'
            closing = text.find(terminator, index + raw_match.end())
            end = len(text) if closing < 0 else closing + len(terminator)
            mask(index, end)
            index = end
            continue

        if text[index] in "\"'":
            quote = text[index]
            line_start = text.rfind("\n", 0, index) + 1
            is_quoted_include = quote == '"' and re.fullmatch(
                r"\s*#\s*include\s*", "".join(output[line_start:index])
            )
            cursor = index + 1
            while cursor < len(text):
                if text[cursor] == "\\":
                    cursor = min(cursor + 2, len(text))
                    continue
                if text[cursor] == quote:
                    cursor += 1
                    break
                cursor += 1
            if not is_quoted_include:
                mask(index, cursor)
            index = cursor
            continue
        index += 1

    return "".join(output)


def mask_cmake_comments(text: str) -> str:
    output = list(text)
    in_quote = False
    escaped = False
    index = 0
    while index < len(text):
        character = text[index]
        if in_quote:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                in_quote = False
            index += 1
            continue
        if character == '"':
            in_quote = True
            index += 1
            continue
        if character == "#":
            end = text.find("\n", index)
            end = len(text) if end < 0 else end
            for position in range(index, end):
                output[position] = " "
            index = end
            continue
        index += 1
    return "".join(output)


def find_matching_parenthesis(text: str, opening: int) -> int | None:
    depth = 0
    in_quote = False
    escaped = False
    for index in range(opening, len(text)):
        character = text[index]
        if in_quote:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                in_quote = False
            continue
        if character == '"':
            in_quote = True
        elif character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
            if depth == 0:
                return index
    return None


def iter_target_link_blocks(text: str) -> Iterable[tuple[int, str]]:
    sanitized = mask_cmake_comments(text)
    for match in TARGET_LINK_COMMAND_RE.finditer(sanitized):
        opening = sanitized.find("(", match.start())
        closing = find_matching_parenthesis(sanitized, opening)
        if closing is not None:
            yield match.start(), sanitized[opening + 1 : closing]


def is_vulkan_or_rhi_dependency(reference: str) -> bool:
    lowered = reference.lower()
    return "vulkan" in lowered or "rhi_vulkan" in lowered or "rhi-vulkan" in lowered


def statement_start(text: str, offset: int) -> int:
    return max(text.rfind(token, 0, offset) for token in ";{}") + 1


def statement_end(text: str, offset: int) -> int:
    ending = text.find(";", offset)
    return len(text) if ending < 0 else ending + 1


def assigned_result_name(prefix: str) -> str | None:
    match = re.search(
        r"(?:(?:const|volatile)\s+)*(?:VkResult|auto)\s+([A-Za-z_]\w*)\s*=\s*$",
        prefix,
    )
    if match:
        return match.group(1)
    match = re.search(r"\b([A-Za-z_]\w*)\s*=\s*$", prefix)
    return match.group(1) if match else None


def result_is_consumed(text: str, statement_ending: int, variable: str) -> bool:
    line_limit = statement_ending
    for _ in range(12):
        next_line = text.find("\n", line_limit + 1)
        if next_line < 0:
            line_limit = len(text)
            break
        line_limit = next_line
    window = text[statement_ending:line_limit]
    reassignment = re.search(rf"\b{re.escape(variable)}\s*=(?!=)", window)
    if reassignment:
        window = window[: reassignment.start()]

    escaped = re.escape(variable)
    comparison = rf"(?:\b{escaped}\b\s*(?:==|!=|<=|>=|<|>)|(?:==|!=|<=|>=|<|>)\s*\b{escaped}\b)"
    if re.search(comparison, window):
        return True
    if re.search(rf"\b(?:return|co_return)\s+\b{escaped}\b", window):
        return True
    if re.search(rf"\bswitch\s*\(\s*{escaped}\s*\)", window):
        return True
    helper_names = "|".join(re.escape(name) for name in VK_RESULT_HELPERS)
    return bool(
        re.search(rf"\b(?:{helper_names})\s*\([^;{{}}]*\b{escaped}\b", window)
    )


def vk_result_is_handled(text: str, match: re.Match[str]) -> bool:
    start = statement_start(text, match.start())
    prefix = text[start : match.start()]
    if re.search(r"\b(?:return|co_return)\s*$", prefix):
        return True
    helper_names = "|".join(re.escape(name) for name in VK_RESULT_HELPERS)
    if re.search(rf"\b(?:{helper_names})\s*\([^;{{}}]*$", prefix):
        return True

    opening = text.find("(", match.start(), match.end())
    closing = find_matching_parenthesis(text, opening)
    if closing is None:
        return False
    context_end_candidates = [
        position
        for position in (text.find(";", closing), text.find("{", closing))
        if position >= 0
    ]
    context_end = min(context_end_candidates, default=len(text))
    if re.search(r"(?:==|!=|<=|>=|<|>)", text[closing + 1 : context_end]):
        return True

    variable = assigned_result_name(prefix)
    if variable is None:
        return False
    ending = statement_end(text, closing)
    return result_is_consumed(text, ending, variable)


def scan_cmake(path: Path, text: str, lines: list[str], findings: list[Finding]) -> None:
    sanitized = mask_cmake_comments(text)
    sanitized_lines = sanitized.splitlines()
    saw_standard = False
    for line_no, line in enumerate(sanitized_lines, start=1):
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

    for offset, block in iter_target_link_blocks(text):
        references = TARGET_REFERENCE_RE.findall(block)
        if not references:
            continue
        target = TARGET_ALIASES.get(references[0].lower())
        dependencies = references[1:]
        if target == "rhi_vulkan" and any(
            TARGET_ALIASES.get(reference.lower()) == "rendergraph"
            for reference in dependencies
        ):
            add(
                findings,
                "warning",
                "vkengine.rhi-vulkan-rendergraph-dependency",
                path,
                line_number(text, offset),
                "The base asharia-rhi-vulkan target appears to link RenderGraph.",
                "Keep the base RHI independent; link RenderGraph only from "
                "asharia-rhi-vulkan-rendergraph.",
            )
        if target == "rendergraph" and any(
            is_vulkan_or_rhi_dependency(reference) for reference in dependencies
        ):
            add(
                findings,
                "warning",
                "vkengine.rendergraph-vulkan-dependency",
                path,
                line_number(text, offset),
                "The backend-agnostic RenderGraph target appears to link Vulkan or the Vulkan RHI.",
                "Keep RenderGraph backend-agnostic and link Vulkan only from an adapter target.",
            )
        if target == "renderer_basic" and any(
            is_vulkan_or_rhi_dependency(reference) for reference in dependencies
        ):
            add(
                findings,
                "warning",
                "vkengine.renderer-basic-vulkan-dependency",
                path,
                line_number(text, offset),
                "The backend-agnostic renderer_basic target appears to link Vulkan or the "
                "Vulkan RHI.",
                "Move the dependency to asharia-renderer-basic-vulkan.",
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


def scan_source(path: Path, text: str, findings: list[Finding]) -> None:
    code = mask_cpp_non_code(text)
    normalized_path = "/" + path.as_posix().lower().lstrip("/")
    is_rendergraph = "/packages/rendergraph/" in normalized_path
    is_rhi_vulkan_public = "/packages/rhi-vulkan/include/asharia/rhi_vulkan/" in normalized_path
    is_renderer_basic_public = (
        "/packages/renderer-basic/include/asharia/renderer_basic/" in normalized_path
    )
    is_renderer_basic_vulkan_public = (
        "/packages/renderer-basic/include/asharia/renderer_basic_vulkan/" in normalized_path
    )

    for match in INCLUDE_RE.finditer(code):
        include_path = match.group(1).strip().replace("\\", "/")
        line_no = line_number(code, match.start())
        include_text = match.group(0)
        if PACKAGE_SRC_INCLUDE_RE.search(include_text):
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
            include_path.startswith("vulkan/")
            or include_path.startswith("asharia/rhi_vulkan/")
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

        if is_rhi_vulkan_public and include_path.startswith("asharia/rendergraph/"):
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
                include_path.startswith("vulkan/")
                or include_path.startswith("asharia/rhi_vulkan/")
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

        if include_path == "vulkan/vulkan.h":
            add(
                findings,
                "info",
                "vulkan.loader-strategy",
                path,
                line_no,
                "Direct vulkan.h include detected.",
                "Confirm this matches the repository's raw Vulkan header strategy.",
            )

    boundary_patterns: list[tuple[re.Pattern[str], str, str, str]] = []
    if is_rendergraph:
        boundary_patterns.append(
            (
                VULKAN_TYPE_RE,
                "vkengine.rendergraph-vulkan-boundary",
                "RenderGraph code appears to reference Vulkan symbols.",
                "Keep RenderGraph backend-agnostic and translate in rhi_vulkan_rendergraph.",
            )
        )
    if is_renderer_basic_public and not is_renderer_basic_vulkan_public:
        boundary_patterns.append(
            (
                VULKAN_TYPE_RE,
                "vkengine.renderer-basic-backend-boundary",
                "Backend-agnostic renderer_basic API appears to reference Vulkan symbols.",
                "Move Vulkan code to renderer_basic_vulkan.",
            )
        )
    for pattern, rule, message, suggestion in boundary_patterns:
        for match in pattern.finditer(code):
            add(
                findings,
                "warning",
                rule,
                path,
                line_number(code, match.start()),
                message,
                suggestion,
            )

    simple_rules = (
        (
            re.compile(r"\busing\s+namespace\s+std\s*;"),
            "cpp.namespace-std",
            "Global using namespace std detected.",
            "Use qualified names or narrow declarations.",
        ),
        (
            re.compile(r"\bnew\s+[A-Za-z_:]|\bdelete\s+|\bmalloc\s*\(|\bfree\s*\("),
            "cpp.raw-ownership",
            "Raw allocation/deallocation detected.",
            "Use RAII, containers, smart pointers, or project allocators.",
        ),
        (
            re.compile(r"\bvkDeviceWaitIdle\s*\("),
            "vulkan.device-wait-idle",
            "vkDeviceWaitIdle can hide synchronization bugs and stall the GPU.",
            "Restrict it to documented shutdown, debug, or recovery paths.",
        ),
        (
            re.compile(r"\b(?:vkCmdPipelineBarrier|VkImageMemoryBarrier|VkBufferMemoryBarrier)\b"),
            "vulkan.sync2",
            "Legacy synchronization API detected.",
            "Prefer synchronization2 and verify producer/consumer stages and access masks.",
        ),
        (
            re.compile(r"\bVK_PIPELINE_STAGE_(?:TOP|BOTTOM)_OF_PIPE_BIT\b"),
            "vulkan.pipeline-stage-broad",
            "TOP_OF_PIPE/BOTTOM_OF_PIPE stage detected.",
            "Use precise synchronization2 stages unless this is justified.",
        ),
        (
            re.compile(r"\bvkAllocateMemory\s*\("),
            "vulkan.memory-allocation",
            "Direct vkAllocateMemory call detected.",
            "Use VMA or the project allocation facade.",
        ),
    )
    for pattern, rule, message, suggestion in simple_rules:
        for match in pattern.finditer(code):
            add(
                findings,
                "warning",
                rule,
                path,
                line_number(code, match.start()),
                message,
                suggestion,
            )
    for match in re.finditer(r"\bvkCreateSwapchainKHR\s*\(", code):
        add(
            findings,
            "info",
            "vulkan.swapchain-recreation",
            path,
            line_number(code, match.start()),
            "Swapchain creation detected.",
            "Review resize, oldSwapchain handoff, failure cleanup, and in-flight lifetime.",
        )
    for match in VK_RESULT_CALL_RE.finditer(code):
        if not vk_result_is_handled(code, match):
            add(
                findings,
                "warning",
                "vulkan.vkresult-unchecked",
                path,
                line_number(code, match.start()),
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
        scan_source(path, text, findings)
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

    roots = [Path(item).resolve() for item in args.paths]
    findings: list[Finding] = []
    candidates: list[Path] = []
    seen_candidates: set[Path] = set()
    for root in roots:
        if not root.exists():
            findings.append(
                Finding(
                    "error",
                    "scanner.path-missing",
                    str(root),
                    1,
                    "Input path does not exist.",
                    "Pass an existing Vulkan/C++ source, build file, or directory.",
                )
            )
            continue
        root_candidates = list(iter_files((root,)))
        if not root_candidates:
            findings.append(
                Finding(
                    "error",
                    "scanner.no-candidates",
                    str(root),
                    1,
                    "Input path contains no candidate C/C++ or CMake files.",
                    "Pass a source/build file or a directory containing reviewable files.",
                )
            )
            continue
        for candidate in root_candidates:
            if candidate not in seen_candidates:
                seen_candidates.add(candidate)
                candidates.append(candidate)

    for path in candidates:
        findings.extend(scan_file(path))
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
