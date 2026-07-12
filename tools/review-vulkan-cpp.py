#!/usr/bin/env python3
"""Conservative heuristic review scanner for Asharia Vulkan/C++23 code.

Findings are prompts for human review, not proof of defects. The scanner has no
third-party dependencies so the same gate can run locally and in hosted CI.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import xml.etree.ElementTree as ET
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

VK_CALL_RE = re.compile(r"\b(vk[A-Z][A-Za-z0-9_]*)\s*\(")
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
TARGET_REFERENCE_RE = re.compile(
    r"(?<![A-Za-z0-9_-])(?:asharia::[A-Za-z0-9_-]+|"
    r"asharia-[A-Za-z0-9_-]+|Vulkan::[A-Za-z0-9_-]+)(?![A-Za-z0-9_-])"
)

TARGET_ALIASES = {
    "asharia-rendergraph": "rendergraph",
    "asharia::rendergraph": "rendergraph",
    "asharia-rhi-vulkan": "rhi_vulkan",
    "asharia::rhi_vulkan": "rhi_vulkan",
    "asharia-rhi-vulkan-rendergraph": "rhi_vulkan_rendergraph",
    "asharia::rhi_vulkan_rendergraph": "rhi_vulkan_rendergraph",
    "asharia-renderer-basic": "renderer_basic",
    "asharia::renderer_basic": "renderer_basic",
    "asharia-renderer-basic-vulkan": "renderer_basic_vulkan",
    "asharia::renderer_basic_vulkan": "renderer_basic_vulkan",
}

TERMINAL_VK_RESULT_HELPERS = (
    "VK_CHECK",
    "VK_ASSERT",
    "throw_if",
)
CONVERTING_VK_RESULT_HELPERS = ("checkVk", "check_vk", "vulkanError")

# This fallback keeps repository scans useful when a Vulkan SDK registry is not
# installed. CI and normal engine development load the complete command set for
# the installed target SDK from vk.xml.
FALLBACK_VK_RESULT_COMMANDS = frozenset(
    {
        "vkAcquireNextImageKHR",
        "vkAllocateCommandBuffers",
        "vkAllocateDescriptorSets",
        "vkBeginCommandBuffer",
        "vkCreateCommandPool",
        "vkCreateComputePipelines",
        "vkCreateDescriptorPool",
        "vkCreateDescriptorSetLayout",
        "vkCreateDevice",
        "vkCreateFence",
        "vkCreateGraphicsPipelines",
        "vkCreateImageView",
        "vkCreateInstance",
        "vkCreatePipelineCache",
        "vkCreatePipelineLayout",
        "vkCreateQueryPool",
        "vkCreateSampler",
        "vkCreateSemaphore",
        "vkCreateShaderModule",
        "vkCreateSwapchainKHR",
        "vkDeviceWaitIdle",
        "vkEndCommandBuffer",
        "vkEnumerateDeviceExtensionProperties",
        "vkEnumerateInstanceExtensionProperties",
        "vkEnumerateInstanceLayerProperties",
        "vkEnumeratePhysicalDevices",
        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
        "vkGetPhysicalDeviceSurfaceFormatsKHR",
        "vkGetPhysicalDeviceSurfacePresentModesKHR",
        "vkGetPhysicalDeviceSurfaceSupportKHR",
        "vkGetQueryPoolResults",
        "vkGetSwapchainImagesKHR",
        "vkMapMemory",
        "vkQueuePresentKHR",
        "vkQueueSubmit2",
        "vkQueueWaitIdle",
        "vkResetCommandBuffer",
        "vkResetFences",
        "vkWaitForFences",
    }
)


def vulkan_registry_candidates() -> Iterable[Path]:
    sdk_root = os.environ.get("VULKAN_SDK")
    if sdk_root:
        root = Path(sdk_root)
        yield root / "share" / "vulkan" / "registry" / "vk.xml"
        yield root / "Registry" / "vk.xml"
    yield Path("/usr/share/vulkan/registry/vk.xml")
    yield Path("/usr/local/share/vulkan/registry/vk.xml")


def load_vk_result_commands() -> tuple[frozenset[str], Path | None]:
    for candidate in vulkan_registry_candidates():
        if not candidate.is_file():
            continue
        try:
            root = ET.parse(candidate).getroot()
        except (ET.ParseError, OSError):
            continue

        return_types: dict[str, str] = {}
        aliases: dict[str, str] = {}
        for command in root.findall("./commands/command"):
            prototype = command.find("proto")
            if prototype is not None:
                name = prototype.findtext("name")
                return_type = prototype.findtext("type")
                if name and return_type:
                    return_types[name] = return_type
                continue
            name = command.get("name")
            alias = command.get("alias")
            if name and alias:
                aliases[name] = alias

        changed = True
        while changed:
            changed = False
            for name, alias in aliases.items():
                if name not in return_types and alias in return_types:
                    return_types[name] = return_types[alias]
                    changed = True
        commands = frozenset(
            name for name, return_type in return_types.items() if return_type == "VkResult"
        ) | FALLBACK_VK_RESULT_COMMANDS
        return commands, candidate
    return FALLBACK_VK_RESULT_COMMANDS, None


VK_RESULT_COMMANDS, VULKAN_REGISTRY_PATH = load_vk_result_commands()


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


def path_is_excluded(path: Path, excluded_roots: Iterable[Path]) -> bool:
    return any(path == excluded or path.is_relative_to(excluded) for excluded in excluded_roots)


def path_matches_exclusion_glob(path: Path, pattern: str, base: Path) -> bool:
    try:
        relative = path.relative_to(base)
    except ValueError:
        relative = path
    return relative.match(pattern)


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
            while end >= 0 and end > 0 and text[end - 1] == "\\":
                end = text.find("\n", end + 1)
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
        bracket_match = re.match(r"(#?)\[(=*)\[", text[index:])
        if not in_quote and bracket_match:
            terminator = "]" + bracket_match.group(2) + "]"
            closing = text.find(terminator, index + bracket_match.end())
            end = len(text) if closing < 0 else closing + len(terminator)
            if bracket_match.group(1):
                for position in range(index, end):
                    if output[position] not in "\r\n":
                        output[position] = " "
            index = end
            continue
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
    index = opening
    while index < len(text):
        bracket_match = re.match(r"\[(=*)\[", text[index:])
        if not in_quote and bracket_match:
            terminator = "]" + bracket_match.group(1) + "]"
            closing = text.find(terminator, index + bracket_match.end())
            if closing < 0:
                return None
            index = closing + len(terminator)
            continue
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
        elif character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
            if depth == 0:
                return index
        index += 1
    return None


def iter_cmake_command_blocks(text: str, command_name: str) -> Iterable[tuple[int, str]]:
    sanitized = mask_cmake_comments(text)
    command_re = re.compile(rf"\b{re.escape(command_name)}\s*\(", re.IGNORECASE)
    index = 0
    in_quote = False
    escaped = False
    while index < len(sanitized):
        bracket_match = re.match(r"\[(=*)\[", sanitized[index:])
        if not in_quote and bracket_match:
            terminator = "]" + bracket_match.group(1) + "]"
            closing = sanitized.find(terminator, index + bracket_match.end())
            if closing < 0:
                return
            index = closing + len(terminator)
            continue
        character = sanitized[index]
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
        match = command_re.match(sanitized, index)
        if not match:
            index += 1
            continue
        opening = sanitized.find("(", match.start())
        closing = find_matching_parenthesis(sanitized, opening)
        if closing is None:
            return
        yield match.start(), sanitized[opening + 1 : closing]
        index = closing + 1


def iter_target_link_blocks(text: str) -> Iterable[tuple[int, str]]:
    return iter_cmake_command_blocks(text, "target_link_libraries")


def first_cmake_argument(block: str) -> tuple[str, str] | None:
    bracket_match = re.match(r"\s*\[(=*)\[(.*?)\]\1\]", block, re.DOTALL)
    if bracket_match:
        return bracket_match.group(2), block[bracket_match.end() :]
    match = re.match(r'\s*(?:"((?:\\.|[^"\\])*)"|([^\s)]+))', block)
    if not match:
        return None
    value = match.group(1) if match.group(1) is not None else match.group(2)
    return value, block[match.end() :]


def split_cmake_arguments(block: str) -> list[str]:
    arguments: list[str] = []
    index = 0
    while index < len(block):
        while index < len(block) and block[index].isspace():
            index += 1
        if index >= len(block):
            break
        if block[index] == '"':
            cursor = index + 1
            value: list[str] = []
            while cursor < len(block):
                if block[cursor] == "\\" and cursor + 1 < len(block):
                    value.append(block[cursor + 1])
                    cursor += 2
                    continue
                if block[cursor] == '"':
                    cursor += 1
                    break
                value.append(block[cursor])
                cursor += 1
            arguments.append("".join(value))
            index = cursor
            continue
        bracket_match = re.match(r"\[(=*)\[", block[index:])
        if bracket_match:
            terminator = "]" + bracket_match.group(1) + "]"
            content_start = index + bracket_match.end()
            closing = block.find(terminator, content_start)
            if closing < 0:
                arguments.append(block[content_start:])
                break
            arguments.append(block[content_start:closing])
            index = closing + len(terminator)
            continue
        cursor = index
        while cursor < len(block) and not block[cursor].isspace():
            cursor += 1
        arguments.append(block[index:cursor])
        index = cursor
    return arguments


def simple_cmake_variables(text: str) -> dict[str, str]:
    variables: dict[str, str] = {}
    commands = [
        (offset, "set", block) for offset, block in iter_cmake_command_blocks(text, "set")
    ]
    commands.extend(
        (offset, "list", block) for offset, block in iter_cmake_command_blocks(text, "list")
    )
    for _, command, block in sorted(commands):
        arguments = split_cmake_arguments(block)
        if command == "set" and arguments:
            name, *values = arguments
            variables[name] = ";".join(values)
        elif (
            command == "list"
            and len(arguments) >= 2
            and arguments[0].upper() == "APPEND"
        ):
            name = arguments[1]
            appended = ";".join(arguments[2:])
            if name in variables and variables[name] and appended:
                variables[name] += ";" + appended
            elif appended:
                variables[name] = appended
    return variables


def expand_simple_cmake_variables(value: str, variables: dict[str, str]) -> str:
    for _ in range(8):
        expanded = re.sub(
            r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}",
            lambda match: variables.get(match.group(1), match.group(0)),
            value,
        )
        if expanded == value:
            return expanded
        value = expanded
    return value


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


def assigned_expression_name(prefix: str) -> str | None:
    prefix = prefix.rstrip(";\r\n ")
    matches = list(re.finditer(r"\b([A-Za-z_]\w*)\s*=(?!=)[^;{}]*$", prefix))
    return matches[-1].group(1) if matches else None


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
    comparison = (
        rf"(?:\b{escaped}\b\s*(?:==|!=|<=|>=|<|>)|"
        rf"(?:==|!=|<=|>=|<|>)\s*\b{escaped}\b)"
    )
    if re.search(rf"\b(?:if|while)\s*\([^;{{}}]*{comparison}", window):
        return True
    if re.search(rf"\b(?:return|co_return)\b[^;{{}}]*\b{escaped}\b", window):
        return True
    if re.search(rf"\bswitch\s*\(\s*{escaped}\s*\)", window):
        return True
    if re.search(rf"\b(?:if|while)\s*\(\s*!?\s*{escaped}\s*\)", window):
        return True
    comparison_statement = re.search(
        rf"([^;{{}}]*{comparison}[^;{{}}]*;)", window,
    )
    if comparison_statement:
        statement = comparison_statement.group(1)
        assigned = assigned_expression_name(statement)
        if assigned and assigned != variable:
            statement_end_offset = statement_ending + comparison_statement.end()
            if result_is_consumed(text, statement_end_offset, assigned):
                return True
    terminal_helpers = "|".join(re.escape(name) for name in TERMINAL_VK_RESULT_HELPERS)
    if re.search(rf"\b(?:{terminal_helpers})\s*\([^;{{}}]*\b{escaped}\b", window):
        return True
    converting_helpers = "|".join(
        re.escape(name) for name in CONVERTING_VK_RESULT_HELPERS
    )
    return bool(
        re.search(
            rf"\b(?:return|co_return|throw)\b[^;{{}}]*\b(?:{converting_helpers})"
            rf"\s*\([^;{{}}]*\b{escaped}\b",
            window,
        )
    )


def enclosing_converting_helper(
    text: str, statement_beginning: int, call_start: int, call_closing: int
) -> tuple[int, int] | None:
    prefix = text[statement_beginning:call_start]
    helper_names = "|".join(re.escape(name) for name in CONVERTING_VK_RESULT_HELPERS)
    matches = list(re.finditer(rf"\b(?:{helper_names})\s*\(", prefix))
    for helper_match in reversed(matches):
        helper_start = statement_beginning + helper_match.start()
        helper_opening = text.find("(", helper_start, call_start)
        helper_closing = find_matching_parenthesis(text, helper_opening)
        if helper_closing is not None and helper_closing >= call_closing:
            return helper_start, helper_closing
    return None


def vk_result_is_handled(text: str, match: re.Match[str]) -> bool:
    start = statement_start(text, match.start())
    opening = text.find("(", match.start(), match.end())
    closing = find_matching_parenthesis(text, opening)
    if closing is None:
        return False

    original_prefix = text[start : match.start()]
    terminal_helpers = "|".join(re.escape(name) for name in TERMINAL_VK_RESULT_HELPERS)
    if re.search(rf"\b(?:{terminal_helpers})\s*\([^;{{}}]*$", original_prefix):
        return True

    effective_start = match.start()
    helper = enclosing_converting_helper(text, start, match.start(), closing)
    if helper is not None:
        effective_start, closing = helper
    prefix = text[start:effective_start]
    if re.search(r"\b(?:return|co_return|throw)\b[^;{}]*$", prefix):
        return True

    suffix = text[closing + 1 :]
    is_control_condition = bool(re.search(r"\b(?:if|while)\s*\([^;{}]*$", prefix))
    comparison_before = bool(re.search(r"(?:==|!=|<=|>=|<|>)\s*$", prefix))
    comparison_after = bool(re.match(r"\s*(?:==|!=|<=|>=|<|>)", suffix))
    if is_control_condition and (comparison_before or comparison_after):
        return True
    if re.search(r"\b(?:if|while)\s*\(\s*!?\s*$", prefix) and re.match(r"\s*\)", suffix):
        return True

    variable = assigned_result_name(prefix) or assigned_expression_name(prefix)
    if variable is None:
        return False
    ending = statement_end(text, closing)
    return result_is_consumed(text, ending, variable)


def scan_cmake(path: Path, text: str, lines: list[str], findings: list[Finding]) -> None:
    sanitized = mask_cmake_comments(text)
    sanitized_lines = sanitized.splitlines()
    variables = simple_cmake_variables(text)
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
        arguments = first_cmake_argument(block)
        if arguments is None:
            continue
        target_argument, dependency_block = arguments
        target_name = expand_simple_cmake_variables(target_argument, variables)
        expanded_dependencies = expand_simple_cmake_variables(dependency_block, variables)
        target = TARGET_ALIASES.get(target_name.lower())
        dependencies = TARGET_REFERENCE_RE.findall(expanded_dependencies)
        if target is None and "$" in target_name and dependencies and any(
            TARGET_ALIASES.get(reference.lower()) in {"rendergraph", "rhi_vulkan"}
            or is_vulkan_or_rhi_dependency(reference)
            for reference in dependencies
        ):
            add(
                findings,
                "warning",
                "vkengine.cmake-target-unresolved",
                path,
                line_number(text, offset),
                f"Could not resolve target_link_libraries target {target_argument!r}.",
                "Use a literal project target or a simple set() variable so package boundaries "
                "can be reviewed.",
            )
            continue
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
    for match in VK_CALL_RE.finditer(code):
        if match.group(1) in VK_RESULT_COMMANDS and not vk_result_is_handled(code, match):
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
        "--exclude",
        action="append",
        default=[],
        metavar="PATH",
        help="Explicit file or directory subtree to exclude (repeatable)",
    )
    parser.add_argument(
        "--exclude-glob",
        action="append",
        default=[],
        metavar="PATTERN",
        help="Explicit repository-relative Path.match pattern to exclude (repeatable)",
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
    excluded_roots = [Path(item).resolve() for item in args.exclude]
    exclusion_base = Path.cwd().resolve()
    matched_exclusion_globs = {pattern: 0 for pattern in args.exclude_glob}
    findings: list[Finding] = []
    if VULKAN_REGISTRY_PATH is None:
        findings.append(
            Finding(
                "error",
                "scanner.vulkan-registry-missing",
                "<VULKAN_SDK>",
                1,
                "Vulkan vk.xml was not found; complete VkResult command coverage is unavailable.",
                "Set VULKAN_SDK or install the platform Vulkan registry package.",
            )
        )
    for excluded in excluded_roots:
        if not excluded.exists():
            findings.append(
                Finding(
                    "error",
                    "scanner.exclude-missing",
                    str(excluded),
                    1,
                    "Excluded path does not exist.",
                    "Correct the exclusion path so intended source is not scanned accidentally.",
                )
            )
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
            if path_is_excluded(candidate, excluded_roots):
                continue
            matching_globs = [
                pattern
                for pattern in args.exclude_glob
                if path_matches_exclusion_glob(candidate, pattern, exclusion_base)
            ]
            if matching_globs:
                for pattern in matching_globs:
                    matched_exclusion_globs[pattern] += 1
                continue
            if candidate not in seen_candidates:
                seen_candidates.add(candidate)
                candidates.append(candidate)

    for pattern, match_count in matched_exclusion_globs.items():
        if match_count == 0:
            findings.append(
                Finding(
                    "error",
                    "scanner.exclude-glob-unmatched",
                    pattern,
                    1,
                    "Exclusion glob matched no candidate files.",
                    "Correct or remove the glob so scope exclusions remain auditable.",
                )
            )

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
