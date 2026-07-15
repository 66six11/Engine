"""Validate and render one explicit final Host build request."""

from __future__ import annotations

import math
import os
import stat
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from tools import check_package_contracts as contracts
from tools import host_build_publication
from tools import host_executable_template as host_template
from tools import static_composition_root as composition


_MANIFEST_PATH = "final-host-build"
_MAX_PARALLEL_JOBS = 256
_MAX_TIMEOUT_SECONDS = 3600.0


@dataclass(frozen=True)
class FinalHostBuildRequestV1:
    """Complete typed input for one Windows Development Host build."""

    cmake_executable: Path = field(repr=False)
    source_root: Path = field(repr=False)
    build_root: Path = field(repr=False)
    host_template_root: Path = field(repr=False)
    static_composition_root: Path = field(repr=False)
    toolchain_file: Path = field(repr=False)
    host_template_generation: host_template.WindowsDevelopmentHostTemplateGenerationV1
    static_composition_generation: composition.StaticCompositionRootGeneration
    target_name: str
    configuration: str
    generator_name: str
    generator_multi_config: bool
    parallel_jobs: int
    enable_clang_tidy: bool
    environment: tuple[tuple[str, str], ...] = field(repr=False)
    configure_timeout_seconds: float = 300.0
    build_timeout_seconds: float = 900.0


@dataclass(frozen=True)
class ValidatedFinalHostBuildRequestV1:
    cmake_executable: Path = field(repr=False)
    source_root: Path = field(repr=False)
    build_root: Path = field(repr=False)
    host_template_root: Path = field(repr=False)
    static_composition_root: Path = field(repr=False)
    toolchain_file: Path = field(repr=False)
    environment: dict[str, str] = field(repr=False)


def _diagnostic(code: str, pointer: str, message: str) -> contracts.Diagnostic:
    return contracts.Diagnostic(code, _MANIFEST_PATH, pointer, message)


def _is_link_or_reparse(status: os.stat_result) -> bool:
    if stat.S_ISLNK(status.st_mode):
        return True
    reparse_attribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    return bool(getattr(status, "st_file_attributes", 0) & reparse_attribute)


def _regular_path(value: Any, *, directory: bool) -> Path | None:
    if not isinstance(value, Path):
        return None
    try:
        absolute = value.absolute()
        status = absolute.lstat()
        expected_type = stat.S_ISDIR if directory else stat.S_ISREG
        if _is_link_or_reparse(status) or not expected_type(status.st_mode):
            return None
        return absolute.resolve(strict=True)
    except OSError:
        return None


def _normalized_build_root(value: Any) -> Path | None:
    if not isinstance(value, Path):
        return None
    absolute = value.absolute()
    try:
        if absolute.exists():
            status = absolute.lstat()
            if _is_link_or_reparse(status) or not stat.S_ISDIR(status.st_mode):
                return None
            return absolute.resolve(strict=True)
        return absolute.parent.resolve(strict=True) / absolute.name
    except OSError:
        return None


def _controlled_environment(
    value: Any,
) -> tuple[dict[str, str] | None, list[contracts.Diagnostic]]:
    if not isinstance(value, tuple):
        return None, [_diagnostic(
            "host-build.environment-invalid", "/environment",
            "environment must be one explicit tuple of key/value pairs",
        )]
    result: dict[str, str] = {}
    folded_keys: set[str] = set()
    diagnostics: list[contracts.Diagnostic] = []
    for index, item in enumerate(value):
        valid = (
            isinstance(item, tuple)
            and len(item) == 2
            and isinstance(item[0], str)
            and isinstance(item[1], str)
            and bool(item[0])
            and "=" not in item[0]
            and "\0" not in item[0]
            and "\0" not in item[1]
        )
        if not valid:
            diagnostics.append(_diagnostic(
                "host-build.environment-invalid", f"/environment/{index}",
                "environment entries must be non-empty string key/value pairs",
            ))
            continue
        key, environment_value = item
        folded = key.casefold()
        if folded in folded_keys:
            diagnostics.append(_diagnostic(
                "host-build.environment-duplicate", f"/environment/{index}",
                f"environment key '{key}' is duplicated case-insensitively",
            ))
            continue
        folded_keys.add(folded)
        result[key] = environment_value
    return (result if not diagnostics else None), diagnostics


def _timeout(value: Any, pointer: str) -> contracts.Diagnostic | None:
    if (
        not isinstance(value, (int, float))
        or isinstance(value, bool)
        or not math.isfinite(value)
        or value <= 0
        or value > _MAX_TIMEOUT_SECONDS
    ):
        return _diagnostic(
            "host-build.timeout-invalid",
            pointer,
            f"timeout must be positive and at most {_MAX_TIMEOUT_SECONDS:g} seconds",
        )
    return None


def validate_final_host_build_request(
    request: Any,
    validators: contracts.ContractValidators,
) -> tuple[ValidatedFinalHostBuildRequestV1 | None, list[contracts.Diagnostic]]:
    """Validate all request-owned paths, manifests, scalars, and environment."""

    if not isinstance(request, FinalHostBuildRequestV1):
        return None, [
            _diagnostic(
                "host-build.request-invalid", "", "build request must use FinalHostBuildRequestV1"
            )
        ]

    diagnostics: list[contracts.Diagnostic] = []
    paths = {
        "cmake_executable": _regular_path(request.cmake_executable, directory=False),
        "source_root": _regular_path(request.source_root, directory=True),
        "build_root": _normalized_build_root(request.build_root),
        "host_template_root": _regular_path(request.host_template_root, directory=True),
        "static_composition_root": _regular_path(
            request.static_composition_root, directory=True
        ),
        "toolchain_file": _regular_path(request.toolchain_file, directory=False),
    }
    for field_name, pointer, label in (
        ("cmake_executable", "/cmakeExecutable", "cmake executable"),
        ("source_root", "/sourceRoot", "source root"),
        ("build_root", "/buildRoot", "build root"),
        ("host_template_root", "/hostTemplateRoot", "Host Template root"),
        ("static_composition_root", "/staticCompositionRoot", "static-composition root"),
        ("toolchain_file", "/toolchainFile", "toolchain file"),
    ):
        if paths[field_name] is None:
            diagnostics.append(
                _diagnostic("host-build.path-invalid", pointer,
                            f"{label} must be one explicit regular local path")
            )

    environment, environment_diagnostics = _controlled_environment(request.environment)
    diagnostics.extend(environment_diagnostics)
    for value, pointer in (
        (request.configure_timeout_seconds, "/configureTimeoutSeconds"),
        (request.build_timeout_seconds, "/buildTimeoutSeconds"),
    ):
        timeout_diagnostic = _timeout(value, pointer)
        if timeout_diagnostic is not None:
            diagnostics.append(timeout_diagnostic)
    if (
        not isinstance(request.parallel_jobs, int)
        or isinstance(request.parallel_jobs, bool)
        or not 1 <= request.parallel_jobs <= _MAX_PARALLEL_JOBS
    ):
        diagnostics.append(
            _diagnostic("host-build.parallelism-invalid", "/parallelJobs",
                        f"parallel jobs must be between 1 and {_MAX_PARALLEL_JOBS}")
        )
    for value, code, pointer, message in (
        (
            request.generator_multi_config,
            "host-build.generator-invalid",
            "/generatorMultiConfig",
            "generator multi-config fact must be boolean",
        ),
        (
            request.enable_clang_tidy,
            "host-build.clang-tidy-invalid",
            "/enableClangTidy",
            "clang-tidy selection must be boolean",
        ),
    ):
        if not isinstance(value, bool):
            diagnostics.append(_diagnostic(code, pointer, message))
    for pointer, value, label in (
        ("/targetName", request.target_name, "target name"),
        ("/configuration", request.configuration, "configuration"),
        ("/generatorName", request.generator_name, "generator name"),
    ):
        if not isinstance(value, str) or not value or "\0" in value:
            diagnostics.append(
                _diagnostic("host-build.identity-invalid", pointer,
                            f"{label} must be a non-empty string")
            )

    template_generation = request.host_template_generation
    composition_generation = request.static_composition_generation
    if not isinstance(
        template_generation,
        host_template.WindowsDevelopmentHostTemplateGenerationV1,
    ):
        diagnostics.append(
            _diagnostic(
                "host-build.template-generation-invalid",
                "/hostTemplate",
                "request requires one complete Host Template generation",
            )
        )
    else:
        diagnostics.extend(
            host_template.validate_windows_development_host_template_generation(
                template_generation,
                validators,
            )
        )
    if not isinstance(
        composition_generation,
        composition.StaticCompositionRootGeneration,
    ):
        diagnostics.append(
            _diagnostic(
                "host-build.composition-generation-invalid",
                "/staticComposition",
                "request requires one complete static-composition generation",
            )
        )
    else:
        diagnostics.extend(
            composition.validate_static_composition_root_generation(
                composition_generation,
                validators,
            )
        )
    if diagnostics:
        return None, diagnostics

    assert isinstance(
        template_generation,
        host_template.WindowsDevelopmentHostTemplateGenerationV1,
    )
    assert isinstance(
        composition_generation,
        composition.StaticCompositionRootGeneration,
    )
    template_manifest = template_generation.manifest
    composition_manifest = composition_generation.manifest
    if (
        template_manifest.static_composition_generation_id
        != composition_manifest.generation_id
        or template_manifest.static_composition_manifest_integrity
        != composition_manifest.integrity
    ):
        diagnostics.append(
            _diagnostic("host-build.composition-stale", "/staticComposition",
                        "Host Template and static-composition manifests do not match")
        )
    if (
        request.target_name != template_manifest.target_name
        or request.configuration != template_manifest.configuration
        or request.configuration != composition_manifest.configuration
        or request.generator_name != composition_manifest.generator_name
        or request.generator_multi_config != composition_manifest.generator_multi_config
    ):
        diagnostics.append(
            _diagnostic("host-build.request-mismatch", "/build",
                        "requested target/configuration/generator differs from verified inputs")
        )

    assert isinstance(paths["host_template_root"], Path)
    assert isinstance(paths["static_composition_root"], Path)
    diagnostics.extend(
        host_build_publication.validate_final_host_publications(
            paths["host_template_root"],
            template_generation,
            paths["static_composition_root"],
            composition_generation,
        )
    )
    if diagnostics:
        return None, diagnostics

    assert environment is not None
    assert all(isinstance(value, Path) for value in paths.values())
    return (
        ValidatedFinalHostBuildRequestV1(
            cmake_executable=paths["cmake_executable"],
            source_root=paths["source_root"],
            build_root=paths["build_root"],
            host_template_root=paths["host_template_root"],
            static_composition_root=paths["static_composition_root"],
            toolchain_file=paths["toolchain_file"],
            environment=environment,
        ),
        [],
    )


def configure_final_host_arguments(
    request: FinalHostBuildRequestV1,
    validated: ValidatedFinalHostBuildRequestV1,
    build_root: Path,
) -> tuple[str, ...]:
    arguments = [
        str(validated.cmake_executable),
        "-S",
        str(validated.source_root),
        "-B",
        str(build_root),
        "-G",
        request.generator_name,
        f"-DCMAKE_TOOLCHAIN_FILE:FILEPATH={validated.toolchain_file}",
        "-DASHARIA_BUILD_APPS:BOOL=OFF",
        "-DASHARIA_BUILD_TESTS:BOOL=OFF",
        "-DASHARIA_ENABLE_CLANG_TIDY:BOOL=" + (
            "ON" if request.enable_clang_tidy else "OFF"
        ),
        f"-DASHARIA_GENERATED_HOST_TEMPLATE_ROOT:PATH={validated.host_template_root}",
        f"-DASHARIA_STATIC_COMPOSITION_ROOT:PATH={validated.static_composition_root}",
    ]
    if not request.generator_multi_config:
        arguments.append(f"-DCMAKE_BUILD_TYPE:STRING={request.configuration}")
    return tuple(arguments)


def build_final_host_arguments(
    request: FinalHostBuildRequestV1,
    validated: ValidatedFinalHostBuildRequestV1,
    build_root: Path,
) -> tuple[str, ...]:
    arguments = [
        str(validated.cmake_executable),
        "--build",
        str(build_root),
        "--target",
        request.target_name,
    ]
    if request.generator_multi_config:
        arguments.extend(("--config", request.configuration))
    arguments.extend(("--parallel", str(request.parallel_jobs)))
    return tuple(arguments)
