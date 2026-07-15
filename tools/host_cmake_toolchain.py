"""Typed configured CXX evidence bound to one stable Host CMake reply."""

from __future__ import annotations

import unicodedata
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import host_cmake_query as query
from tools import host_cmake_reply as reply
from tools import host_cmake_target as target_api


@dataclass(frozen=True, order=True)
class HostCMakeGeneratorEvidence:
    """Machine-neutral generator facts from the selected reply index."""

    name: str
    multi_config: bool


@dataclass(frozen=True, order=True)
class ConfiguredCxxCompilerEvidence:
    """Configured CXX identity without the machine-local compiler path."""

    language: str
    compiler_id: str
    compiler_version: str
    target: str | None


@dataclass(frozen=True)
class HostCMakeConfiguredTargetEvidence:
    """Target and compiler evidence observed through one stable reply index."""

    build_root: Path = field(repr=False)
    reply_index_path: Path = field(repr=False)
    generator: HostCMakeGeneratorEvidence
    target: target_api.HostCMakeTargetEvidence
    configured_compiler: ConfiguredCxxCompilerEvidence
    toolchains_major: int
    toolchains_minor: int


@dataclass(frozen=True)
class HostCMakeConfiguredTargetResult:
    """Atomic result: either one same-index evidence bundle or diagnostics."""

    evidence: HostCMakeConfiguredTargetEvidence | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.evidence is not None and not self.diagnostics


def _sorted_diagnostics(
    diagnostics: Iterable[contracts.Diagnostic],
) -> tuple[contracts.Diagnostic, ...]:
    return tuple(sorted(diagnostics, key=query.diagnostic_sort_key))


def _failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> HostCMakeConfiguredTargetResult:
    return HostCMakeConfiguredTargetResult(None, _sorted_diagnostics(diagnostics))


def _invalid_compiler(pointer: str, field_name: str) -> reply.ReplyFailure:
    return reply.ReplyFailure(
        [
            query.diagnostic(
                "host-build.cmake-cxx-compiler-invalid",
                pointer,
                f"configured CXX {field_name} must be a stable non-path string",
            )
        ]
    )


def _stable_compiler_value(value: Any, pointer: str, field_name: str) -> str:
    if (
        not isinstance(value, str)
        or not value
        or unicodedata.normalize("NFC", value) != value
        or any(marker in value for marker in ("/", "\\", ":", "\r", "\n"))
    ):
        raise _invalid_compiler(pointer, field_name)
    return value


def _configured_cxx_compiler(
    toolchains: dict[str, Any],
) -> ConfiguredCxxCompilerEvidence:
    entries = toolchains["toolchains"]
    matches = [
        value
        for value in entries
        if isinstance(value, dict) and value.get("language") == "CXX"
    ]
    if len(matches) != 1:
        raise reply.ReplyFailure(
            [
                query.diagnostic(
                    "host-build.cmake-cxx-toolchain-mismatch",
                    "/toolchains/toolchains",
                    "toolchains reply must contain exactly one CXX entry",
                )
            ]
        )
    compiler = matches[0].get("compiler")
    if not isinstance(compiler, dict):
        raise reply.ReplyFailure(
            [
                query.diagnostic(
                    "host-build.cmake-cxx-compiler-invalid",
                    "/toolchains/toolchains/CXX/compiler",
                    "configured CXX entry must contain one compiler object",
                )
            ]
        )
    compiler_id = _stable_compiler_value(
        compiler.get("id"),
        "/toolchains/toolchains/CXX/compiler/id",
        "compiler ID",
    )
    compiler_version = _stable_compiler_value(
        compiler.get("version"),
        "/toolchains/toolchains/CXX/compiler/version",
        "compiler version",
    )
    configured_target: str | None = None
    if "target" in compiler:
        configured_target = _stable_compiler_value(
            compiler["target"],
            "/toolchains/toolchains/CXX/compiler/target",
            "compiler target",
        )
    return ConfiguredCxxCompilerEvidence(
        language="CXX",
        compiler_id=compiler_id,
        compiler_version=compiler_version,
        target=configured_target,
    )


def read_host_cmake_configured_target(
    build_root: Any,
    configuration: Any,
    target_name: Any,
    *,
    require_artifact: bool,
) -> HostCMakeConfiguredTargetResult:
    """Read target and configured CXX facts from one stable latest index."""

    try:
        (
            normalized_root,
            normalized_configuration,
            normalized_target_name,
            normalized_requirement,
        ) = target_api._validated_target_request(
            build_root,
            configuration,
            target_name,
            require_artifact=require_artifact,
        )
        observed = reply.read_stable_reply(
            normalized_root,
            normalized_configuration,
            normalized_target_name,
        )
        bound_target = target_api._bind_target(
            observed,
            normalized_requirement,
        )
        configured_compiler = _configured_cxx_compiler(observed.toolchains)
    except reply.ReplyFailure as failure:
        return _failure(failure.diagnostics)

    return HostCMakeConfiguredTargetResult(
        evidence=HostCMakeConfiguredTargetEvidence(
            build_root=observed.build_root,
            reply_index_path=observed.reply_index_path,
            generator=HostCMakeGeneratorEvidence(
                observed.generator_name,
                observed.generator_multi_config,
            ),
            target=bound_target,
            configured_compiler=configured_compiler,
            toolchains_major=observed.toolchains_major,
            toolchains_minor=observed.toolchains_minor,
        ),
        diagnostics=(),
    )


__all__ = [
    "ConfiguredCxxCompilerEvidence",
    "HostCMakeConfiguredTargetEvidence",
    "HostCMakeConfiguredTargetResult",
    "HostCMakeGeneratorEvidence",
    "read_host_cmake_configured_target",
]
