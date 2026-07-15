"""Semantic and deterministic-byte validation for generated Host Templates."""

from __future__ import annotations

from typing import Any

from tools import check_package_contracts as contracts
from tools import host_executable_template as host_template
from tools import host_template_renderer as renderer
from tools import static_composition_root as composition


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
        manifest_path=host_template.HOST_TEMPLATE_NAME,
        pointer=pointer,
        message=message,
    )


def _integrity_record(value: dict[str, str]) -> composition.IntegrityRecord:
    return composition.IntegrityRecord(value["algorithm"], value["digest"])


def _integrity_data(value: composition.IntegrityRecord) -> dict[str, str]:
    return {"algorithm": value.algorithm, "digest": value.digest}


def _manifest_from_data(
    data: dict[str, Any],
) -> host_template.WindowsDevelopmentHostTemplateManifestV1:
    files = tuple(
        host_template.HostTemplateFileEvidence(
            path=value["path"],
            role=value["role"],
            media_type=value["mediaType"],
            size=value["size"],
            integrity=_integrity_record(value["integrity"]),
        )
        for value in data["files"]
    )
    return host_template.WindowsDevelopmentHostTemplateManifestV1(
        generation_id=data["generationId"],
        renderer_revision=data["rendererRevision"],
        static_composition_generation_id=data["staticComposition"]["generationId"],
        static_composition_manifest_integrity=_integrity_record(
            data["staticComposition"]["manifestIntegrity"]
        ),
        engine_generation_id=data["host"]["engineGenerationId"],
        host_kind=data["host"]["hostKind"],
        target_platform=data["host"]["targetPlatform"],
        configuration=data["build"]["configuration"],
        target_name=data["build"]["target"]["name"],
        files=files,
        integrity=_integrity_record(data["integrity"]),
    )


def validate_manifest_data(
    manifest: host_template.WindowsDevelopmentHostTemplateManifestV1 | Any,
    validators: contracts.ContractValidators,
) -> list[contracts.Diagnostic]:
    """Validate schema plus the invariants JSON Schema cannot express."""

    data = (
        host_template.windows_development_host_template_manifest_to_data(manifest)
        if isinstance(
            manifest,
            host_template.WindowsDevelopmentHostTemplateManifestV1,
        )
        else manifest
    )
    diagnostics = contracts.validate_manifest_data(
        data,
        host_template.HOST_TEMPLATE_NAME,
        validators,
    )
    if diagnostics or not isinstance(data, dict):
        return diagnostics

    parsed = _manifest_from_data(data)
    expected_shapes = tuple(renderer.HOST_TEMPLATE_FILE_DESCRIPTORS)
    actual_shapes = tuple(
        (value.path, value.role, value.media_type) for value in parsed.files
    )
    if actual_shapes != expected_shapes:
        diagnostics.append(
            _diagnostic(
                "host-template.files-not-normalized",
                "/files",
                "generated files do not match the fixed canonical layout",
            )
        )
    expected_generation_id = (
        host_template.compute_windows_development_host_template_generation_id(parsed)
    )
    if parsed.generation_id != expected_generation_id:
        diagnostics.append(
            _diagnostic(
                "host-template.generation-id-mismatch",
                "/generationId",
                "generation ID does not match the canonical Host Template descriptor",
            )
        )
    expected_integrity = (
        host_template.compute_windows_development_host_template_manifest_integrity(
            parsed
        )
    )
    if _integrity_data(parsed.integrity) != expected_integrity:
        diagnostics.append(
            _diagnostic(
                "host-template.integrity-mismatch",
                "/integrity",
                "manifest integrity does not match canonical fields",
            )
        )
    return sorted(diagnostics, key=_diagnostic_sort_key)


def _expected_files(
    manifest: host_template.WindowsDevelopmentHostTemplateManifestV1,
) -> tuple[host_template.GeneratedHostTemplateFile, ...]:
    return (
        host_template.GeneratedHostTemplateFile(
            renderer.HOST_TEMPLATE_CMAKE_PATH,
            "cmake-host-template",
            "text/x-cmake",
            renderer.render_windows_development_cmake(
                manifest.target_name,
                manifest.generation_id,
                manifest.static_composition_generation_id,
            ),
        ),
        host_template.GeneratedHostTemplateFile(
            renderer.HOST_TEMPLATE_MAIN_PATH,
            "registration-verification-main",
            "text/x-c++src",
            renderer.render_registration_verification_main(),
        ),
    )


def validate_generation(
    generation: host_template.WindowsDevelopmentHostTemplateGenerationV1,
    validators: contracts.ContractValidators,
) -> list[contracts.Diagnostic]:
    """Require one complete generation produced by the fixed v1 renderer."""

    diagnostics = validate_manifest_data(generation.manifest, validators)
    files = {value.path: value for value in generation.files}
    if len(files) != len(generation.files):
        diagnostics.append(
            _diagnostic(
                "host-template.duplicate-output",
                "/files",
                "generation contains duplicate output paths",
            )
        )
    expected = {value.path: value for value in _expected_files(generation.manifest)}
    manifest_paths = {value.path for value in generation.manifest.files}
    if set(files) != manifest_paths or set(files) != set(expected):
        diagnostics.append(
            _diagnostic(
                "host-template.output-set-mismatch",
                "/files",
                "in-memory output set differs from the fixed manifest layout",
            )
        )

    evidence_by_path = {value.path: value for value in generation.manifest.files}
    for index, (path, rendered) in enumerate(expected.items()):
        generated = files.get(path)
        evidence = evidence_by_path.get(path)
        if generated is None or evidence is None:
            continue
        actual_integrity = _integrity_record(
            contracts.compute_bytes_integrity(generated.content)
        )
        if (
            generated.role != evidence.role
            or generated.media_type != evidence.media_type
            or len(generated.content) != evidence.size
            or actual_integrity != evidence.integrity
        ):
            diagnostics.append(
                _diagnostic(
                    "host-template.output-integrity-mismatch",
                    f"/files/{index}",
                    f"generated bytes for '{path}' differ from the manifest",
                )
            )
        if generated != rendered:
            diagnostics.append(
                _diagnostic(
                    "host-template.rendered-output-mismatch",
                    f"/files/{index}",
                    f"generated bytes for '{path}' differ from the fixed renderer",
                )
            )
    return sorted(diagnostics, key=_diagnostic_sort_key)
