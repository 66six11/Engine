"""Generate deterministic Windows Development Host executable templates.

This boundary owns the final CMake target and registration-only ``main``. It
does not execute CMake, discover packages, or collect final artifact bytes.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field, replace
from pathlib import Path
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import host_template_renderer as renderer
from tools import static_composition_root as composition


HOST_TEMPLATE_NAME = "asharia.windows-development-host-template.json"
HOST_TEMPLATE_SCHEMA = "com.asharia.windows-development-host-template"
HOST_TEMPLATE_SCHEMA_VERSION = 1
HOST_TEMPLATE_RENDERER_REVISION = 2
HOST_TEMPLATE_KIND = "windows-development-v1"
HOST_TEMPLATE_CMAKE_PATH = renderer.HOST_TEMPLATE_CMAKE_PATH
HOST_TEMPLATE_MAIN_PATH = renderer.HOST_TEMPLATE_MAIN_PATH
HOST_TEMPLATE_SUBSYSTEM = renderer.HOST_TEMPLATE_SUBSYSTEM
HOST_TEMPLATE_RUNTIME_OUTPUT_DIRECTORY = renderer.HOST_TEMPLATE_RUNTIME_OUTPUT_DIRECTORY

_TARGET_NAME = re.compile(r"^[A-Za-z0-9_.+\-]+$")
_FILE_DESCRIPTORS = renderer.HOST_TEMPLATE_FILE_DESCRIPTORS
_REQUIRED_STATIC_COMPOSITION_RENDERER_REVISION = 5
_REQUIRED_STATIC_COMPOSITION_PROVIDER_API = "asharia-static-factory-provider-v4"


@dataclass(frozen=True, order=True)
class HostTemplateFileEvidence:
    """Exact generated file bytes recorded by the Host Template manifest."""

    path: str
    role: str
    media_type: str
    size: int
    integrity: composition.IntegrityRecord


@dataclass(frozen=True)
class WindowsDevelopmentHostTemplateManifestV1:
    """Content-addressed description of one fixed final Host target."""

    generation_id: str
    renderer_revision: int
    static_composition_generation_id: str
    static_composition_manifest_integrity: composition.IntegrityRecord
    engine_generation_id: str
    host_kind: str
    target_platform: str
    configuration: str
    target_name: str
    files: tuple[HostTemplateFileEvidence, ...]
    integrity: composition.IntegrityRecord


@dataclass(frozen=True, order=True)
class GeneratedHostTemplateFile:
    """One generated Host Template output."""

    path: str
    role: str
    media_type: str
    content: bytes = field(repr=False, compare=True)


@dataclass(frozen=True)
class WindowsDevelopmentHostTemplateGenerationV1:
    """Complete in-memory generation, ready for immutable publication."""

    manifest: WindowsDevelopmentHostTemplateManifestV1
    files: tuple[GeneratedHostTemplateFile, ...]


@dataclass(frozen=True)
class HostTemplateGenerationResult:
    """Atomic generation result."""

    generation: WindowsDevelopmentHostTemplateGenerationV1 | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.generation is not None and not self.diagnostics


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
        manifest_path=HOST_TEMPLATE_NAME,
        pointer=pointer,
        message=message,
    )


def _failure(diagnostics: Iterable[contracts.Diagnostic]) -> HostTemplateGenerationResult:
    return HostTemplateGenerationResult(
        generation=None,
        diagnostics=tuple(sorted(diagnostics, key=_diagnostic_sort_key)),
    )


def _integrity_record(value: dict[str, str]) -> composition.IntegrityRecord:
    return composition.IntegrityRecord(value["algorithm"], value["digest"])


def _integrity_data(value: composition.IntegrityRecord) -> dict[str, str]:
    return {"algorithm": value.algorithm, "digest": value.digest}


def _canonical_integrity(value: Any) -> composition.IntegrityRecord:
    encoded = json.dumps(
        value,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    return _integrity_record(contracts.compute_bytes_integrity(encoded))


def _file_data(value: HostTemplateFileEvidence) -> dict[str, Any]:
    return {
        "path": value.path,
        "role": value.role,
        "mediaType": value.media_type,
        "size": value.size,
        "integrity": _integrity_data(value.integrity),
    }


def _descriptor_data(
    manifest: WindowsDevelopmentHostTemplateManifestV1,
) -> dict[str, Any]:
    return {
        "schema": HOST_TEMPLATE_SCHEMA,
        "schemaVersion": HOST_TEMPLATE_SCHEMA_VERSION,
        "rendererRevision": manifest.renderer_revision,
        "templateKind": HOST_TEMPLATE_KIND,
        "staticComposition": {
            "generationId": manifest.static_composition_generation_id,
            "manifestIntegrity": _integrity_data(
                manifest.static_composition_manifest_integrity
            ),
        },
        "host": {
            "engineGenerationId": manifest.engine_generation_id,
            "hostKind": manifest.host_kind,
            "targetPlatform": manifest.target_platform,
        },
        "build": {
            "configuration": manifest.configuration,
            "target": {"name": manifest.target_name, "type": "EXECUTABLE"},
            "subsystem": HOST_TEMPLATE_SUBSYSTEM,
            "runtimeOutputDirectory": HOST_TEMPLATE_RUNTIME_OUTPUT_DIRECTORY,
        },
        "layout": [
            {"path": path, "role": role, "mediaType": media_type}
            for path, role, media_type in _FILE_DESCRIPTORS
        ],
    }


def _payload_data(
    manifest: WindowsDevelopmentHostTemplateManifestV1,
) -> dict[str, Any]:
    descriptor = _descriptor_data(manifest)
    descriptor.pop("layout")
    return {
        **descriptor,
        "generationId": manifest.generation_id,
        "files": [_file_data(value) for value in manifest.files],
    }


def windows_development_host_template_manifest_to_data(
    manifest: WindowsDevelopmentHostTemplateManifestV1,
) -> dict[str, Any]:
    """Return the fixed-field-order JSON-compatible manifest."""

    return {
        **_payload_data(manifest),
        "integrity": _integrity_data(manifest.integrity),
    }


def render_windows_development_host_template_manifest(
    manifest: WindowsDevelopmentHostTemplateManifestV1,
) -> str:
    """Render canonical human-readable manifest JSON with LF."""

    return json.dumps(
        windows_development_host_template_manifest_to_data(manifest),
        ensure_ascii=False,
        indent=2,
    ) + "\n"


def compute_windows_development_host_template_manifest_integrity(
    manifest: WindowsDevelopmentHostTemplateManifestV1,
) -> dict[str, str]:
    """Hash canonical manifest fields except self-integrity."""

    return _integrity_data(_canonical_integrity(_payload_data(manifest)))


def compute_windows_development_host_template_generation_id(
    manifest: WindowsDevelopmentHostTemplateManifestV1,
) -> str:
    """Derive the content generation identity from the fixed descriptor."""

    return "sha256-" + _canonical_integrity(_descriptor_data(manifest)).digest


def validate_windows_development_host_template_manifest_data(
    manifest: WindowsDevelopmentHostTemplateManifestV1 | Any,
    validators: contracts.ContractValidators,
) -> list[contracts.Diagnostic]:
    """Validate schema, fixed layout, generation identity, and self-integrity."""

    from tools import host_template_validation

    return host_template_validation.validate_manifest_data(manifest, validators)


def validate_windows_development_host_template_generation(
    generation: WindowsDevelopmentHostTemplateGenerationV1,
    validators: contracts.ContractValidators,
) -> list[contracts.Diagnostic]:
    """Revalidate the manifest and deterministic renderer output."""

    from tools import host_template_validation

    return host_template_validation.validate_generation(generation, validators)


def generate_windows_development_host_template(
    static_composition_manifest: Any,
    target_name: Any,
    validators: contracts.ContractValidators,
) -> HostTemplateGenerationResult:
    """Generate one deterministic Host Template from a verified composition."""

    diagnostics: list[contracts.Diagnostic] = []
    if not isinstance(static_composition_manifest, composition.StaticCompositionRootManifest):
        diagnostics.append(
            _diagnostic(
                "host-template.composition-required",
                "/staticComposition",
                "generator requires a StaticCompositionRootManifest",
            )
        )
    else:
        composition_diagnostics = (
            composition.validate_static_composition_root_manifest_data(
                static_composition_manifest,
                validators,
            )
        )
        diagnostics.extend(composition_diagnostics)
        if not composition_diagnostics:
            if (
                static_composition_manifest.renderer_revision
                != _REQUIRED_STATIC_COMPOSITION_RENDERER_REVISION
                or static_composition_manifest.provider_api
                != _REQUIRED_STATIC_COMPOSITION_PROVIDER_API
            ):
                diagnostics.append(
                    _diagnostic(
                        "host-template.composition-incompatible",
                        "/staticComposition",
                        "renderer revision 2 requires static composition "
                        "revision 5 with provider API v4",
                    )
                )
    if (
        not isinstance(target_name, str)
        or not target_name
        or len(target_name) > 200
        or _TARGET_NAME.fullmatch(target_name) is None
    ):
        diagnostics.append(
            _diagnostic(
                "host-template.target-invalid",
                "/build/target/name",
                "target name must be one safe CMake logical target name",
            )
        )
    if diagnostics:
        return _failure(diagnostics)
    assert isinstance(static_composition_manifest, composition.StaticCompositionRootManifest)
    assert isinstance(target_name, str)

    if (
        static_composition_manifest.target_system != "Windows"
        or not static_composition_manifest.target_platform.startswith(
            "com.asharia.platform.windows"
        )
    ):
        return _failure(
            [
                _diagnostic(
                    "host-template.platform-unsupported",
                    "/host/targetPlatform",
                    "windows-development-v1 requires exact Windows build evidence",
                )
            ]
        )

    empty_integrity = composition.IntegrityRecord("sha256", "0" * 64)
    manifest = WindowsDevelopmentHostTemplateManifestV1(
        generation_id="sha256-" + "0" * 64,
        renderer_revision=HOST_TEMPLATE_RENDERER_REVISION,
        static_composition_generation_id=static_composition_manifest.generation_id,
        static_composition_manifest_integrity=static_composition_manifest.integrity,
        engine_generation_id=static_composition_manifest.engine_generation_id,
        host_kind=static_composition_manifest.host_kind,
        target_platform=static_composition_manifest.target_platform,
        configuration=static_composition_manifest.configuration,
        target_name=target_name,
        files=(),
        integrity=empty_integrity,
    )
    generation_id = compute_windows_development_host_template_generation_id(manifest)
    generated_files = (
        GeneratedHostTemplateFile(
            HOST_TEMPLATE_CMAKE_PATH,
            "cmake-host-template",
            "text/x-cmake",
            renderer.render_windows_development_cmake(
                target_name,
                generation_id,
                static_composition_manifest.generation_id,
            ),
        ),
        GeneratedHostTemplateFile(
            HOST_TEMPLATE_MAIN_PATH,
            "registration-verification-main",
            "text/x-c++src",
            renderer.render_registration_verification_main(),
        ),
    )
    evidence = tuple(
        HostTemplateFileEvidence(
            value.path,
            value.role,
            value.media_type,
            len(value.content),
            _integrity_record(contracts.compute_bytes_integrity(value.content)),
        )
        for value in sorted(generated_files, key=lambda item: _utf8_key(item.path))
    )
    manifest = replace(manifest, generation_id=generation_id, files=evidence)
    integrity = compute_windows_development_host_template_manifest_integrity(manifest)
    manifest = replace(manifest, integrity=_integrity_record(integrity))
    generation = WindowsDevelopmentHostTemplateGenerationV1(
        manifest=manifest,
        files=tuple(sorted(generated_files, key=lambda item: _utf8_key(item.path))),
    )
    output_diagnostics = validate_windows_development_host_template_generation(
        generation, validators
    )
    if output_diagnostics:
        return _failure(output_diagnostics)
    return HostTemplateGenerationResult(generation=generation, diagnostics=())


def expected_host_template_publication_files(
    generation: WindowsDevelopmentHostTemplateGenerationV1,
) -> dict[str, bytes]:
    """Return the exact immutable publication file set."""

    return {
        HOST_TEMPLATE_NAME: render_windows_development_host_template_manifest(
            generation.manifest
        ).encode("utf-8"),
        **{value.path: value.content for value in generation.files},
    }
