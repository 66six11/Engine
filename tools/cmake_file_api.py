"""Read-only normalization of one CMake File API codemodel v2 reply."""

from __future__ import annotations

import json
import unicodedata
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Iterable

from tools import check_package_contracts as contracts


CMAKE_CODEMODEL_SNAPSHOT_NAME = "asharia.cmake-codemodel-snapshot.json"
CMAKE_CODEMODEL_SNAPSHOT_SCHEMA = "com.asharia.cmake-codemodel-snapshot"
CMAKE_CODEMODEL_SNAPSHOT_SCHEMA_VERSION = 1
DEFAULT_FILE_API_CLIENT = "client-asharia-planning"
SUPPORTED_TARGET_TYPES = frozenset(
    {
        "EXECUTABLE",
        "STATIC_LIBRARY",
        "SHARED_LIBRARY",
        "MODULE_LIBRARY",
        "OBJECT_LIBRARY",
        "INTERFACE_LIBRARY",
        "UTILITY",
    }
)


@dataclass(frozen=True, order=True)
class CMakeGeneratorEvidence:
    """Stable generator identity copied from the configured reply index."""

    name: str
    multi_config: bool


@dataclass(frozen=True, order=True)
class CMakeToolchainEvidence:
    """Caller-supplied machine-neutral toolchain evidence for this configure."""

    compiler_id: str
    compiler_version: str
    target_system: str
    target_architecture: str


@dataclass(frozen=True, order=True)
class CMakeTargetEvidence:
    """One normalized configured buildsystem target."""

    name: str
    target_type: str
    dependencies: tuple[str, ...]
    artifacts: tuple[str, ...]


@dataclass(frozen=True)
class CMakeCodemodelSnapshot:
    """Canonical evidence from exactly one CMake configuration."""

    configuration: str
    generator: CMakeGeneratorEvidence
    toolchain: CMakeToolchainEvidence
    targets: tuple[CMakeTargetEvidence, ...]


@dataclass(frozen=True)
class CMakeCodemodelSnapshotResult:
    """Atomic result: either one complete snapshot or stable diagnostics."""

    snapshot: CMakeCodemodelSnapshot | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.snapshot is not None and not self.diagnostics


def _utf8_key(value: str) -> bytes:
    return value.encode("utf-8")


def _diagnostic_sort_key(
    diagnostic: contracts.Diagnostic,
) -> tuple[str, str, str, str]:
    return (
        diagnostic.manifest_path,
        diagnostic.pointer,
        diagnostic.code,
        diagnostic.message,
    )


def _diagnostic(code: str, pointer: str, message: str) -> contracts.Diagnostic:
    return contracts.Diagnostic(
        code=code,
        manifest_path="cmake-file-api",
        pointer=pointer,
        message=message,
    )


def _failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> CMakeCodemodelSnapshotResult:
    return CMakeCodemodelSnapshotResult(
        snapshot=None,
        diagnostics=tuple(sorted(diagnostics, key=_diagnostic_sort_key)),
    )


def _generator_data(generator: CMakeGeneratorEvidence) -> dict[str, Any]:
    return {"name": generator.name, "multiConfig": generator.multi_config}


def _toolchain_data(toolchain: CMakeToolchainEvidence) -> dict[str, str]:
    return {
        "compilerId": toolchain.compiler_id,
        "compilerVersion": toolchain.compiler_version,
        "targetSystem": toolchain.target_system,
        "targetArchitecture": toolchain.target_architecture,
    }


def cmake_codemodel_snapshot_to_data(
    snapshot: CMakeCodemodelSnapshot,
) -> dict[str, Any]:
    """Return the fixed-field-order JSON-compatible snapshot representation."""

    return {
        "schema": CMAKE_CODEMODEL_SNAPSHOT_SCHEMA,
        "schemaVersion": CMAKE_CODEMODEL_SNAPSHOT_SCHEMA_VERSION,
        "configuration": snapshot.configuration,
        "generator": _generator_data(snapshot.generator),
        "toolchain": _toolchain_data(snapshot.toolchain),
        "targets": [
            {
                "name": target.name,
                "type": target.target_type,
                "dependencies": sorted(target.dependencies, key=_utf8_key),
                "artifacts": sorted(target.artifacts, key=_utf8_key),
            }
            for target in sorted(
                snapshot.targets, key=lambda value: _utf8_key(value.name)
            )
        ],
    }


def render_cmake_codemodel_snapshot(snapshot: CMakeCodemodelSnapshot) -> str:
    """Render canonical snapshot JSON with LF and a final newline."""

    return json.dumps(
        cmake_codemodel_snapshot_to_data(snapshot),
        ensure_ascii=False,
        indent=2,
    ) + "\n"


def compute_cmake_codemodel_snapshot_integrity(
    snapshot: CMakeCodemodelSnapshot,
) -> dict[str, str]:
    """Hash canonical machine-neutral codemodel evidence."""

    return contracts.compute_bytes_integrity(
        render_cmake_codemodel_snapshot(snapshot).encode("utf-8")
    )


def validate_cmake_codemodel_snapshot(
    snapshot: Any,
    validators: contracts.ContractValidators,
) -> list[contracts.Diagnostic]:
    """Validate a serialized or in-memory codemodel snapshot."""

    data = (
        cmake_codemodel_snapshot_to_data(snapshot)
        if isinstance(snapshot, CMakeCodemodelSnapshot)
        else snapshot
    )
    return contracts.validate_manifest_data(
        data,
        CMAKE_CODEMODEL_SNAPSHOT_NAME,
        validators,
    )


def _validate_toolchain(
    toolchain: Any,
) -> tuple[CMakeToolchainEvidence | None, list[contracts.Diagnostic]]:
    if not isinstance(toolchain, CMakeToolchainEvidence):
        return None, [
            _diagnostic(
                "build.codemodel.toolchain-invalid",
                "/toolchain",
                "toolchain evidence must use CMakeToolchainEvidence",
            )
        ]
    diagnostics: list[contracts.Diagnostic] = []
    for field_name, value in (
        ("compilerId", toolchain.compiler_id),
        ("compilerVersion", toolchain.compiler_version),
        ("targetSystem", toolchain.target_system),
        ("targetArchitecture", toolchain.target_architecture),
    ):
        if not isinstance(value, str) or not value or any(
            marker in value for marker in ("/", "\\", ":")
        ):
            diagnostics.append(
                _diagnostic(
                    "build.codemodel.toolchain-invalid",
                    f"/toolchain/{field_name}",
                    f"{field_name} must be a non-path stable string",
                )
            )
    return toolchain, diagnostics


def _load_json_bytes(
    path: Path,
    pointer: str,
    code: str,
) -> tuple[bytes | None, Any | None, list[contracts.Diagnostic]]:
    try:
        if path.is_symlink() or not path.is_file():
            raise OSError
        content = path.read_bytes()
        return content, json.loads(content.decode("utf-8")), []
    except (OSError, UnicodeDecodeError, json.JSONDecodeError):
        return None, None, [
            _diagnostic(code, pointer, "referenced CMake File API JSON is unreadable")
        ]


def _reply_object_path(
    reply_directory: Path,
    value: Any,
    pointer: str,
) -> tuple[Path | None, list[contracts.Diagnostic]]:
    if not isinstance(value, str):
        return None, [
            _diagnostic(
                "build.codemodel.reply-reference-invalid",
                pointer,
                "reply jsonFile must be a relative filename",
            )
        ]
    pure = PurePosixPath(value)
    if (
        pure.is_absolute()
        or len(pure.parts) != 1
        or pure.name != value
        or value in {"", ".", ".."}
        or "\\" in value
        or ":" in value
    ):
        return None, [
            _diagnostic(
                "build.codemodel.reply-reference-invalid",
                pointer,
                "reply jsonFile must stay inside the explicit reply directory",
            )
        ]
    return reply_directory / value, []


def _normalized_relative_path(value: Any) -> str | None:
    if not isinstance(value, str) or not value:
        return None
    if unicodedata.normalize("NFC", value) != value or "\\" in value or ":" in value:
        return None
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in {"", ".", ".."} for part in path.parts):
        return None
    return path.as_posix()


def _index_codemodel_reply(
    index: Any,
    client_name: str,
) -> tuple[dict[str, Any] | None, list[contracts.Diagnostic]]:
    if not isinstance(index, dict):
        return None, [
            _diagnostic(
                "build.codemodel.index-malformed",
                "",
                "CMake File API index root must be an object",
            )
        ]
    reply = index.get("reply")
    client = reply.get(client_name) if isinstance(reply, dict) else None
    response = client.get("codemodel-v2") if isinstance(client, dict) else None
    if not isinstance(response, dict):
        return None, [
            _diagnostic(
                "build.codemodel.reply-missing",
                "/reply",
                f"reply index does not contain {client_name}/codemodel-v2",
            )
        ]
    version = response.get("version")
    if (
        response.get("kind") != "codemodel"
        or not isinstance(version, dict)
        or version.get("major") != 2
    ):
        return None, [
            _diagnostic(
                "build.codemodel.version-unsupported",
                f"/reply/{client_name}/codemodel-v2",
                "reader requires a successful codemodel major version 2 reply",
            )
        ]
    return response, []


def read_cmake_codemodel_snapshot(
    reply_index_path: Any,
    configuration: Any,
    toolchain: Any,
    validators: contracts.ContractValidators,
    *,
    client_name: str = DEFAULT_FILE_API_CLIENT,
) -> CMakeCodemodelSnapshotResult:
    """Read one explicit reply index without invoking CMake or guessing latest files."""

    diagnostics: list[contracts.Diagnostic] = []
    if not isinstance(reply_index_path, Path):
        diagnostics.append(
            _diagnostic(
                "build.codemodel.index-invalid",
                "",
                "reply index must be an explicit pathlib.Path",
            )
        )
    if not isinstance(configuration, str) or not configuration:
        diagnostics.append(
            _diagnostic(
                "build.codemodel.configuration-invalid",
                "/configuration",
                "configuration must be a non-empty string",
            )
        )
    if not isinstance(client_name, str) or not client_name:
        diagnostics.append(
            _diagnostic(
                "build.codemodel.client-invalid",
                "/reply",
                "File API client name must be a non-empty string",
            )
        )
    normalized_toolchain, toolchain_diagnostics = _validate_toolchain(toolchain)
    diagnostics.extend(toolchain_diagnostics)
    if diagnostics:
        return _failure(diagnostics)
    assert isinstance(reply_index_path, Path)
    assert isinstance(configuration, str)
    assert normalized_toolchain is not None

    index_bytes, index, read_diagnostics = _load_json_bytes(
        reply_index_path,
        "",
        "build.codemodel.index-unreadable",
    )
    if read_diagnostics:
        return _failure(read_diagnostics)
    assert index_bytes is not None
    assert index is not None
    response, response_diagnostics = _index_codemodel_reply(index, client_name)
    if response_diagnostics:
        return _failure(response_diagnostics)
    assert response is not None

    cmake = index.get("cmake")
    generator = cmake.get("generator") if isinstance(cmake, dict) else None
    if (
        not isinstance(generator, dict)
        or not isinstance(generator.get("name"), str)
        or not isinstance(generator.get("multiConfig"), bool)
    ):
        return _failure(
            [
                _diagnostic(
                    "build.codemodel.index-malformed",
                    "/cmake/generator",
                    "reply index must contain generator name and multiConfig",
                )
            ]
        )
    generator_evidence = CMakeGeneratorEvidence(
        name=generator["name"],
        multi_config=generator["multiConfig"],
    )

    reply_directory = reply_index_path.parent
    codemodel_path, path_diagnostics = _reply_object_path(
        reply_directory,
        response.get("jsonFile"),
        f"/reply/{client_name}/codemodel-v2/jsonFile",
    )
    if path_diagnostics:
        return _failure(path_diagnostics)
    assert codemodel_path is not None
    _, codemodel, codemodel_diagnostics = _load_json_bytes(
        codemodel_path,
        "/codemodel",
        "build.codemodel.reply-unreadable",
    )
    if codemodel_diagnostics:
        return _failure(codemodel_diagnostics)
    if not isinstance(codemodel, dict):
        return _failure(
            [
                _diagnostic(
                    "build.codemodel.reply-malformed",
                    "/codemodel",
                    "codemodel reply root must be an object",
                )
            ]
        )
    version = codemodel.get("version")
    if not isinstance(version, dict) or version.get("major") != 2:
        return _failure(
            [
                _diagnostic(
                    "build.codemodel.version-unsupported",
                    "/codemodel/version",
                    "codemodel object must use major version 2",
                )
            ]
        )
    configurations = codemodel.get("configurations")
    if not isinstance(configurations, list):
        return _failure(
            [
                _diagnostic(
                    "build.codemodel.reply-malformed",
                    "/codemodel/configurations",
                    "codemodel configurations must be an array",
                )
            ]
        )
    matches = [
        value
        for value in configurations
        if isinstance(value, dict) and value.get("name") == configuration
    ]
    if len(matches) != 1:
        return _failure(
            [
                _diagnostic(
                    "build.codemodel.configuration-mismatch",
                    "/codemodel/configurations",
                    f"configuration '{configuration}' must appear exactly once",
                )
            ]
        )
    target_summaries = matches[0].get("targets")
    if not isinstance(target_summaries, list):
        return _failure(
            [
                _diagnostic(
                    "build.codemodel.reply-malformed",
                    "/codemodel/configurations/targets",
                    "selected configuration targets must be an array",
                )
            ]
        )

    summaries_by_id: dict[str, dict[str, Any]] = {}
    names: set[str] = set()
    for target_index, summary in enumerate(target_summaries):
        pointer = f"/codemodel/configurations/targets/{target_index}"
        if not isinstance(summary, dict):
            diagnostics.append(
                _diagnostic(
                    "build.codemodel.target-malformed",
                    pointer,
                    "target summary must be an object",
                )
            )
            continue
        target_id = summary.get("id")
        target_name = summary.get("name")
        if not isinstance(target_id, str) or not target_id:
            diagnostics.append(
                _diagnostic(
                    "build.codemodel.target-malformed",
                    f"{pointer}/id",
                    "target summary id must be a non-empty opaque string",
                )
            )
            continue
        if not isinstance(target_name, str) or not target_name:
            diagnostics.append(
                _diagnostic(
                    "build.codemodel.target-malformed",
                    f"{pointer}/name",
                    "target summary name must be a non-empty string",
                )
            )
            continue
        if target_id in summaries_by_id:
            diagnostics.append(
                _diagnostic(
                    "build.codemodel.duplicate-id",
                    f"{pointer}/id",
                    "opaque target id maps to multiple target summaries",
                )
            )
        if target_name in names:
            diagnostics.append(
                _diagnostic(
                    "build.codemodel.duplicate-target",
                    f"{pointer}/name",
                    f"configured target name '{target_name}' is not unique",
                )
            )
        summaries_by_id[target_id] = summary
        names.add(target_name)
    if diagnostics:
        return _failure(diagnostics)

    targets: list[CMakeTargetEvidence] = []
    for target_id, summary in sorted(
        summaries_by_id.items(), key=lambda item: _utf8_key(item[1]["name"])
    ):
        target_name = summary["name"]
        target_path, target_path_diagnostics = _reply_object_path(
            reply_directory,
            summary.get("jsonFile"),
            "/codemodel/configurations/targets/jsonFile",
        )
        diagnostics.extend(target_path_diagnostics)
        if target_path is None:
            continue
        _, target_object, target_read_diagnostics = _load_json_bytes(
            target_path,
            "/codemodel/target",
            "build.codemodel.target-unreadable",
        )
        diagnostics.extend(target_read_diagnostics)
        if not isinstance(target_object, dict):
            if not target_read_diagnostics:
                diagnostics.append(
                    _diagnostic(
                        "build.codemodel.target-malformed",
                        "/codemodel/target",
                        f"target '{target_name}' object must be an object",
                    )
                )
            continue
        if target_object.get("id") != target_id or target_object.get("name") != target_name:
            diagnostics.append(
                _diagnostic(
                    "build.codemodel.target-identity-mismatch",
                    "/codemodel/target",
                    f"target object identity does not match summary for '{target_name}'",
                )
            )
            continue
        target_type = target_object.get("type")
        if target_type not in SUPPORTED_TARGET_TYPES:
            diagnostics.append(
                _diagnostic(
                    "build.codemodel.target-type-unsupported",
                    "/codemodel/target/type",
                    f"target '{target_name}' has unsupported type '{target_type}'",
                )
            )
            continue

        raw_dependencies = target_object.get("dependencies", [])
        raw_artifacts = target_object.get("artifacts", [])
        if not isinstance(raw_dependencies, list) or not isinstance(raw_artifacts, list):
            diagnostics.append(
                _diagnostic(
                    "build.codemodel.target-malformed",
                    "/codemodel/target",
                    f"target '{target_name}' dependencies and artifacts must be arrays",
                )
            )
            continue

        dependency_names: set[str] = set()
        for dependency_index, dependency in enumerate(raw_dependencies):
            dependency_id = dependency.get("id") if isinstance(dependency, dict) else None
            dependency_summary = summaries_by_id.get(dependency_id)
            if dependency_summary is None:
                diagnostics.append(
                    _diagnostic(
                        "build.codemodel.dangling-dependency",
                        f"/codemodel/target/dependencies/{dependency_index}",
                        f"target '{target_name}' references an unknown opaque target id",
                    )
                )
                continue
            dependency_names.add(dependency_summary["name"])

        artifacts: set[str] = set()
        for artifact_index, artifact in enumerate(raw_artifacts):
            artifact_path = artifact.get("path") if isinstance(artifact, dict) else None
            normalized_path = _normalized_relative_path(artifact_path)
            if normalized_path is None:
                diagnostics.append(
                    _diagnostic(
                        "build.codemodel.artifact-path-invalid",
                        f"/codemodel/target/artifacts/{artifact_index}/path",
                        f"target '{target_name}' artifact must be build-tree relative",
                    )
                )
                continue
            artifacts.add(normalized_path)

        targets.append(
            CMakeTargetEvidence(
                name=target_name,
                target_type=target_type,
                dependencies=tuple(sorted(dependency_names, key=_utf8_key)),
                artifacts=tuple(sorted(artifacts, key=_utf8_key)),
            )
        )

    try:
        final_index_bytes = reply_index_path.read_bytes()
    except OSError:
        final_index_bytes = None
    if final_index_bytes != index_bytes:
        diagnostics.append(
            _diagnostic(
                "build.codemodel.reply-changed",
                "",
                "reply index changed while codemodel evidence was read",
            )
        )
    if diagnostics:
        return _failure(diagnostics)

    snapshot = CMakeCodemodelSnapshot(
        configuration=configuration,
        generator=generator_evidence,
        toolchain=normalized_toolchain,
        targets=tuple(targets),
    )
    snapshot_diagnostics = validate_cmake_codemodel_snapshot(snapshot, validators)
    if snapshot_diagnostics:
        return _failure(snapshot_diagnostics)
    return CMakeCodemodelSnapshotResult(snapshot=snapshot, diagnostics=())
