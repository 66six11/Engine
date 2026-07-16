"""Generate and atomically publish deterministic static Host composition roots.

This boundary consumes already verified plans. It does not resolve packages, run
CMake, create a Host executable, or implement the Host Runtime lifecycle.
"""

from __future__ import annotations

import json
import os
import shutil
import stat
import tempfile
from dataclasses import dataclass, field, replace
from pathlib import Path
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import generated_publication_tree
from tools import host_activation_blueprint as activation
from tools import source_build_plan
from tools import static_factory_provider_bindings as provider_bindings


STATIC_COMPOSITION_ROOT_NAME = "asharia.static-composition-root.json"
STATIC_COMPOSITION_ROOT_SCHEMA = "com.asharia.static-composition-root"
STATIC_COMPOSITION_ROOT_SCHEMA_VERSION = 1
STATIC_COMPOSITION_RENDERER_REVISION = 4
STATIC_COMPOSITION_PROVIDER_API = "asharia-static-factory-provider-v3"
STATIC_COMPOSITION_HEADER_PATH = (
    "include/asharia/generated/static_composition_root.hpp"
)
STATIC_COMPOSITION_SOURCE_PATH = "src/static_composition_root.cpp"
STATIC_COMPOSITION_CMAKE_PATH = "asharia-static-composition.cmake"
_MIN_DIAGNOSTIC_FACTORY_ID_BYTES = 256
_MIN_DIAGNOSTIC_CONTRIBUTION_ID_BYTES = 256
_UTF8_BOM = b"\xef\xbb\xbf"

_FILE_DESCRIPTORS = (
    (
        STATIC_COMPOSITION_CMAKE_PATH,
        "cmake-attachment",
        "text/x-cmake",
    ),
    (
        STATIC_COMPOSITION_HEADER_PATH,
        "declaration-header",
        "text/x-c++hdr",
    ),
    (
        STATIC_COMPOSITION_SOURCE_PATH,
        "registration-source",
        "text/x-c++src",
    ),
)


@dataclass(frozen=True, order=True)
class IntegrityRecord:
    """Immutable SHA-256 evidence."""

    algorithm: str
    digest: str


@dataclass(frozen=True)
class StaticCompositionInputs:
    """Fingerprints of every authoritative generator input."""

    source_build_plan_integrity: IntegrityRecord
    host_activation_blueprint_integrity: IntegrityRecord
    static_factory_provider_binding_plan_integrity: IntegrityRecord


@dataclass(frozen=True, order=True)
class StaticCompositionFileEvidence:
    """Exact generated file bytes recorded by the generation manifest."""

    path: str
    role: str
    media_type: str
    size: int
    integrity: IntegrityRecord


@dataclass(frozen=True)
class StaticCompositionRootManifest:
    """Content-addressed generated-source manifest."""

    generation_id: str
    renderer_revision: int
    provider_api: str
    inputs: StaticCompositionInputs
    engine_generation_id: str
    host_kind: str
    target_platform: str
    configuration: str
    generator_name: str
    generator_multi_config: bool
    compiler_id: str
    compiler_version: str
    target_system: str
    target_architecture: str
    providers: tuple[provider_bindings.StaticFactoryProvider, ...]
    files: tuple[StaticCompositionFileEvidence, ...]
    integrity: IntegrityRecord


@dataclass(frozen=True, order=True)
class GeneratedStaticCompositionFile:
    """One package-neutral generated output."""

    path: str
    role: str
    media_type: str
    content: bytes = field(repr=False, compare=True)


@dataclass(frozen=True)
class StaticCompositionRootGeneration:
    """Complete in-memory generation; safe to publish as one unit."""

    manifest: StaticCompositionRootManifest
    files: tuple[GeneratedStaticCompositionFile, ...]


@dataclass(frozen=True)
class StaticCompositionGenerationResult:
    """Atomic pure-generation result."""

    generation: StaticCompositionRootGeneration | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.generation is not None and not self.diagnostics


@dataclass(frozen=True)
class StaticCompositionPublicationReceipt:
    """Committed generation location and reuse status."""

    generation_id: str
    generation_path: Path = field(repr=False)
    manifest_integrity: IntegrityRecord
    reused: bool


@dataclass(frozen=True)
class StaticCompositionPublicationResult:
    """Atomic publication result."""

    receipt: StaticCompositionPublicationReceipt | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.receipt is not None and not self.diagnostics


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
        manifest_path=STATIC_COMPOSITION_ROOT_NAME,
        pointer=pointer,
        message=message,
    )


def _failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> StaticCompositionGenerationResult:
    unique = {
        (value.manifest_path, value.pointer, value.code, value.message): value
        for value in diagnostics
    }
    return StaticCompositionGenerationResult(
        generation=None,
        diagnostics=tuple(sorted(unique.values(), key=_diagnostic_sort_key)),
    )


def _publication_failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> StaticCompositionPublicationResult:
    return StaticCompositionPublicationResult(
        receipt=None,
        diagnostics=tuple(sorted(diagnostics, key=_diagnostic_sort_key)),
    )


def _integrity_record(value: dict[str, str]) -> IntegrityRecord:
    return IntegrityRecord(value["algorithm"], value["digest"])


def _integrity_data(value: IntegrityRecord) -> dict[str, str]:
    return {"algorithm": value.algorithm, "digest": value.digest}


def _canonical_integrity(value: Any) -> IntegrityRecord:
    data = json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return _integrity_record(contracts.compute_bytes_integrity(data))


def _provider_sort_key(
    provider: provider_bindings.StaticFactoryProvider,
) -> tuple[bytes, bytes, bytes, bytes, bytes, bytes]:
    return (
        _utf8_key(provider.package_id),
        _utf8_key(provider.package_version),
        _utf8_key(provider.module_id),
        _utf8_key(provider.target.name),
        _utf8_key(provider.entry_point.header),
        _utf8_key(provider.entry_point.function),
    )


def _provider_data(
    provider: provider_bindings.StaticFactoryProvider,
) -> dict[str, Any]:
    return {
        "packageId": provider.package_id,
        "packageVersion": provider.package_version,
        "moduleId": provider.module_id,
        "target": {
            "name": provider.target.name,
            "type": provider.target.target_type,
        },
        "entryPoint": {
            "header": provider.entry_point.header,
            "function": provider.entry_point.function,
        },
        "factories": [
            {
                "factoryId": factory.factory_id,
                "contributions": [
                    {
                        "id": contribution.contribution_id,
                        "kind": contribution.contribution_kind,
                    }
                    for contribution in factory.contributions
                ],
            }
            for factory in provider.factories
        ],
    }


def _file_data(value: StaticCompositionFileEvidence) -> dict[str, Any]:
    return {
        "path": value.path,
        "role": value.role,
        "mediaType": value.media_type,
        "size": value.size,
        "integrity": _integrity_data(value.integrity),
    }


def _generation_descriptor_data(
    manifest: StaticCompositionRootManifest,
) -> dict[str, Any]:
    return {
        "schema": STATIC_COMPOSITION_ROOT_SCHEMA,
        "schemaVersion": STATIC_COMPOSITION_ROOT_SCHEMA_VERSION,
        "rendererRevision": manifest.renderer_revision,
        "inputs": {
            "sourceBuildPlanIntegrity": _integrity_data(
                manifest.inputs.source_build_plan_integrity
            ),
            "hostActivationBlueprintIntegrity": _integrity_data(
                manifest.inputs.host_activation_blueprint_integrity
            ),
            "staticFactoryProviderBindingPlanIntegrity": _integrity_data(
                manifest.inputs.static_factory_provider_binding_plan_integrity
            ),
        },
        "host": {
            "engineGenerationId": manifest.engine_generation_id,
            "hostKind": manifest.host_kind,
            "targetPlatform": manifest.target_platform,
        },
        "configuration": {
            "name": manifest.configuration,
            "generator": {
                "name": manifest.generator_name,
                "multiConfig": manifest.generator_multi_config,
            },
            "toolchain": {
                "compilerId": manifest.compiler_id,
                "compilerVersion": manifest.compiler_version,
                "targetSystem": manifest.target_system,
                "targetArchitecture": manifest.target_architecture,
            },
        },
        "providerApi": manifest.provider_api,
        "providers": [_provider_data(value) for value in manifest.providers],
        "layout": [
            {"path": path, "role": role, "mediaType": media_type}
            for path, role, media_type in _FILE_DESCRIPTORS
        ],
    }


def _manifest_payload_data(
    manifest: StaticCompositionRootManifest,
) -> dict[str, Any]:
    descriptor = _generation_descriptor_data(manifest)
    descriptor.pop("layout")
    return {
        **descriptor,
        "generationId": manifest.generation_id,
        "files": [_file_data(value) for value in manifest.files],
    }


def static_composition_root_manifest_to_data(
    manifest: StaticCompositionRootManifest,
) -> dict[str, Any]:
    """Return the fixed-field-order JSON-compatible manifest."""

    return {
        **_manifest_payload_data(manifest),
        "integrity": _integrity_data(manifest.integrity),
    }


def render_static_composition_root_manifest(
    manifest: StaticCompositionRootManifest,
) -> str:
    """Render manifest JSON with LF and a final newline."""

    return json.dumps(
        static_composition_root_manifest_to_data(manifest),
        ensure_ascii=False,
        indent=2,
    ) + "\n"


def compute_static_composition_root_manifest_integrity(
    manifest: StaticCompositionRootManifest,
) -> dict[str, str]:
    """Hash canonical manifest fields except self-integrity."""

    return _integrity_data(_canonical_integrity(_manifest_payload_data(manifest)))


def _generation_id(manifest: StaticCompositionRootManifest) -> str:
    descriptor_integrity = _canonical_integrity(
        _generation_descriptor_data(manifest)
    )
    return f"sha256-{descriptor_integrity.digest}"


def _manifest_from_data(data: dict[str, Any]) -> StaticCompositionRootManifest:
    providers = tuple(
        provider_bindings.StaticFactoryProvider(
            package_id=value["packageId"],
            package_version=value["packageVersion"],
            module_id=value["moduleId"],
            target=provider_bindings.ProviderTarget(
                value["target"]["name"], value["target"]["type"]
            ),
            entry_point=provider_bindings.ProviderEntryPoint(
                value["entryPoint"]["header"],
                value["entryPoint"]["function"],
            ),
            factories=tuple(
                provider_bindings.StaticFactoryBinding(
                    factory_id=factory["factoryId"],
                    contributions=tuple(
                        provider_bindings.StaticFactoryContributionBinding(
                            contribution_id=contribution["id"],
                            contribution_kind=contribution["kind"],
                        )
                        for contribution in factory["contributions"]
                    ),
                )
                for factory in value["factories"]
            ),
        )
        for value in data["providers"]
    )
    files = tuple(
        StaticCompositionFileEvidence(
            path=value["path"],
            role=value["role"],
            media_type=value["mediaType"],
            size=value["size"],
            integrity=_integrity_record(value["integrity"]),
        )
        for value in data["files"]
    )
    return StaticCompositionRootManifest(
        generation_id=data["generationId"],
        renderer_revision=data["rendererRevision"],
        provider_api=data["providerApi"],
        inputs=StaticCompositionInputs(
            _integrity_record(data["inputs"]["sourceBuildPlanIntegrity"]),
            _integrity_record(
                data["inputs"]["hostActivationBlueprintIntegrity"]
            ),
            _integrity_record(
                data["inputs"]["staticFactoryProviderBindingPlanIntegrity"]
            ),
        ),
        engine_generation_id=data["host"]["engineGenerationId"],
        host_kind=data["host"]["hostKind"],
        target_platform=data["host"]["targetPlatform"],
        configuration=data["configuration"]["name"],
        generator_name=data["configuration"]["generator"]["name"],
        generator_multi_config=data["configuration"]["generator"]["multiConfig"],
        compiler_id=data["configuration"]["toolchain"]["compilerId"],
        compiler_version=data["configuration"]["toolchain"]["compilerVersion"],
        target_system=data["configuration"]["toolchain"]["targetSystem"],
        target_architecture=data["configuration"]["toolchain"][
            "targetArchitecture"
        ],
        providers=providers,
        files=files,
        integrity=_integrity_record(data["integrity"]),
    )


def validate_static_composition_root_manifest_data(
    manifest: StaticCompositionRootManifest | Any,
    validators: contracts.ContractValidators,
) -> list[contracts.Diagnostic]:
    """Validate schema, canonical ordering, generation ID, and self-integrity."""

    data = (
        static_composition_root_manifest_to_data(manifest)
        if isinstance(manifest, StaticCompositionRootManifest)
        else manifest
    )
    diagnostics = contracts.validate_manifest_data(
        data,
        STATIC_COMPOSITION_ROOT_NAME,
        validators,
    )
    if diagnostics or not isinstance(data, dict):
        return diagnostics

    parsed = _manifest_from_data(data)
    if parsed.providers != tuple(sorted(parsed.providers, key=_provider_sort_key)):
        diagnostics.append(
            _diagnostic(
                "static-composition.providers-not-normalized",
                "/providers",
                "providers are not in canonical UTF-8 order",
            )
        )
    contribution_owners: dict[
        tuple[str, str], tuple[str, str, str, str]
    ] = {}
    for provider_index, provider in enumerate(parsed.providers):
        factory_ids = tuple(factory.factory_id for factory in provider.factories)
        if factory_ids != tuple(sorted(factory_ids, key=_utf8_key)) or len(
            factory_ids
        ) != len(set(factory_ids)):
            diagnostics.append(
                _diagnostic(
                    "static-composition.factories-not-normalized",
                    f"/providers/{provider_index}/factories",
                    "provider factories are not in unique canonical UTF-8 order",
                )
            )
        for factory_index, factory in enumerate(provider.factories):
            contribution_ids = tuple(
                contribution.contribution_id
                for contribution in factory.contributions
            )
            if contribution_ids != tuple(
                sorted(contribution_ids, key=_utf8_key)
            ) or len(contribution_ids) != len(set(contribution_ids)):
                diagnostics.append(
                    _diagnostic(
                        "static-composition.contributions-not-normalized",
                        (
                            f"/providers/{provider_index}/factories/"
                            f"{factory_index}/contributions"
                        ),
                        (
                            "factory contributions are not in unique canonical "
                            "UTF-8 ID order"
                        ),
                    )
                )
            for contribution_index, contribution in enumerate(
                factory.contributions
            ):
                owner_key = (provider.package_id, contribution.contribution_id)
                owner = (
                    provider.package_id,
                    provider.package_version,
                    provider.module_id,
                    factory.factory_id,
                )
                previous_owner = contribution_owners.get(owner_key)
                if previous_owner is not None and previous_owner != owner:
                    diagnostics.append(
                        _diagnostic(
                            "static-composition.contribution-owner-duplicate",
                            (
                                f"/providers/{provider_index}/factories/"
                                f"{factory_index}/contributions/"
                                f"{contribution_index}/id"
                            ),
                            (
                                "one package contribution ID is owned by more "
                                "than one factory"
                            ),
                        )
                    )
                else:
                    contribution_owners[owner_key] = owner

    expected_file_shapes = tuple(_FILE_DESCRIPTORS)
    actual_file_shapes = tuple(
        (value.path, value.role, value.media_type) for value in parsed.files
    )
    if actual_file_shapes != expected_file_shapes:
        diagnostics.append(
            _diagnostic(
                "static-composition.files-not-normalized",
                "/files",
                "generated files do not match the fixed canonical layout",
            )
        )
    if parsed.generation_id != _generation_id(parsed):
        diagnostics.append(
            _diagnostic(
                "static-composition.generation-id-mismatch",
                "/generationId",
                "generation ID does not match canonical inputs and provider calls",
            )
        )
    expected_integrity = compute_static_composition_root_manifest_integrity(parsed)
    if _integrity_data(parsed.integrity) != expected_integrity:
        diagnostics.append(
            _diagnostic(
                "static-composition.integrity-mismatch",
                "/integrity",
                "manifest integrity does not match canonical fields",
            )
        )
    return sorted(diagnostics, key=_diagnostic_sort_key)


def _render_recording_header() -> bytes:
    text = """#pragma once

#include \"asharia/host_runtime/static_factory_registration.hpp\"

namespace asharia::generated {

[[nodiscard]] asharia::host_runtime::StaticFactoryRegistrationCapacityV2
staticFactoryRegistrationCapacity() noexcept;

void recordStaticFactoryProviders(
    asharia::host_runtime::StaticFactoryRegistrationRecorder& recorder) noexcept;

} // namespace asharia::generated
"""
    return _UTF8_BOM + text.encode("utf-8")


def _registration_capacity(
    generation_id: str,
    host_activation_blueprint_sha256: str,
    providers: tuple[provider_bindings.StaticFactoryProvider, ...],
) -> tuple[int, int, int, int, int, int]:
    factory_count = sum(len(provider.factories) for provider in providers)
    contribution_count = sum(
        len(factory.contributions)
        for provider in providers
        for factory in provider.factories
    )
    text_values = [generation_id, host_activation_blueprint_sha256]
    for provider in providers:
        text_values.extend(
            (
                provider.package_id,
                provider.package_version,
                provider.module_id,
                provider.entry_point.function,
            )
        )
        for factory in provider.factories:
            text_values.append(factory.factory_id)
            for contribution in factory.contributions:
                text_values.extend(
                    (
                        contribution.contribution_id,
                        contribution.contribution_kind,
                    )
                )
    diagnostic_factory_id_bytes = max(
        _MIN_DIAGNOSTIC_FACTORY_ID_BYTES,
        max(
            (
                len(factory.factory_id.encode("utf-8"))
                for provider in providers
                for factory in provider.factories
            ),
            default=0,
        ),
    )
    diagnostic_contribution_id_bytes = max(
        _MIN_DIAGNOSTIC_CONTRIBUTION_ID_BYTES,
        max(
            (
                len(contribution.contribution_id.encode("utf-8"))
                for provider in providers
                for factory in provider.factories
                for contribution in factory.contributions
            ),
            default=0,
        ),
    )
    return (
        len(providers),
        factory_count,
        contribution_count,
        sum(len(value.encode("utf-8")) for value in text_values),
        diagnostic_factory_id_bytes,
        diagnostic_contribution_id_bytes,
    )


def _render_recording_source(
    generation_id: str,
    host_activation_blueprint_sha256: str,
    providers: tuple[provider_bindings.StaticFactoryProvider, ...],
) -> bytes:
    headers = sorted(
        {
            STATIC_COMPOSITION_HEADER_PATH.removeprefix("include/"),
            *(value.entry_point.header for value in providers),
        },
        key=_utf8_key,
    )
    lines = [
        "#include <array>",
        "#include <span>",
        "#include <string_view>",
        "#include <type_traits>",
        "",
    ]
    lines.extend(f'#include "{value}"' for value in headers)
    lines.append("")
    for provider in providers:
        lines.extend(
            (
                "static_assert(std::is_same_v<",
                f"              decltype(&{provider.entry_point.function}),",
                "              asharia::host_runtime::StaticFactoryProviderV3>);",
                "",
            )
        )
    if providers:
        lines.extend(("namespace {", ""))
        for provider_index, provider in enumerate(providers):
            for factory_index, factory in enumerate(provider.factories):
                lines.append(
                    "constexpr std::array<"
                    "asharia::host_runtime::StaticContributionExpectationV1, "
                    f"{len(factory.contributions)}> "
                    f"kExpectedContributions{provider_index}_{factory_index}{{{{"
                )
                for contribution in factory.contributions:
                    lines.extend(
                        (
                            "    {",
                            f'        .contributionId = "{contribution.contribution_id}",',
                            f'        .contributionKind = "{contribution.contribution_kind}",',
                            "    },",
                        )
                    )
                lines.extend(("}};", ""))

            lines.append(
                "constexpr std::array<"
                "asharia::host_runtime::StaticFactoryExpectationV1, "
                f"{len(provider.factories)}> kExpectedFactories{provider_index}{{{{"
            )
            for factory_index, factory in enumerate(provider.factories):
                lines.extend(
                    (
                        "    {",
                        f'        .factoryId = "{factory.factory_id}",',
                        "        .contributions = std::span<const "
                        "asharia::host_runtime::StaticContributionExpectationV1>{",
                        f"            kExpectedContributions{provider_index}_{factory_index}",
                        "        },",
                        "    },",
                    )
                )
            lines.extend(("}};", ""))
        lines.extend(("} // namespace", ""))

    (
        provider_count,
        factory_count,
        contribution_count,
        text_bytes,
        diagnostic_factory_id_bytes,
        diagnostic_contribution_id_bytes,
    ) = _registration_capacity(
        generation_id,
        host_activation_blueprint_sha256,
        providers,
    )
    lines.extend(
        (
            "asharia::host_runtime::StaticFactoryRegistrationCapacityV2",
            "asharia::generated::staticFactoryRegistrationCapacity() noexcept {",
            "  return {",
            f"      .providerCount = {provider_count}U,",
            f"      .factoryCount = {factory_count}U,",
            f"      .contributionCount = {contribution_count}U,",
            f"      .textBytes = {text_bytes}U,",
            "      .diagnosticFactoryIdBytes = "
            f"{diagnostic_factory_id_bytes}U,",
            "      .diagnosticContributionIdBytes = "
            f"{diagnostic_contribution_id_bytes}U,",
            "  };",
            "}",
            "",
            "void asharia::generated::recordStaticFactoryProviders(",
            "    asharia::host_runtime::StaticFactoryRegistrationRecorder& recorder) noexcept {",
            "  recorder.beginComposition({",
            f'      .generationId = "{generation_id}",',
            "      .hostActivationBlueprintSha256 =",
            f'          "{host_activation_blueprint_sha256}",',
            "      .capacity = staticFactoryRegistrationCapacity(),",
            "  });",
        )
    )
    for index, provider in enumerate(providers):
        lines.extend(
            (
                "  recorder.invokeProvider(",
                "      {",
                f'          .packageId = "{provider.package_id}",',
                f'          .packageVersion = "{provider.package_version}",',
                f'          .moduleId = "{provider.module_id}",',
                f'          .entryPoint = "{provider.entry_point.function}",',
                "          .expectedFactories = std::span<const "
                "asharia::host_runtime::StaticFactoryExpectationV1>{",
                f"              kExpectedFactories{index}",
                "          },",
                "      },",
                f"      &{provider.entry_point.function});",
            )
        )
    lines.extend(("  recorder.endComposition();", "}", ""))
    return _UTF8_BOM + "\n".join(lines).encode("utf-8")


def _render_cmake_attachment(
    generation_id: str,
    providers: tuple[provider_bindings.StaticFactoryProvider, ...],
) -> bytes:
    targets = tuple(
        sorted({value.target.name for value in providers}, key=_utf8_key)
    )
    lines = [
        "# Generated by tools/static_composition_root.py. Do not edit.",
        "function(asharia_attach_static_composition target_name)",
        "  if(NOT ARGC EQUAL 1)",
        '    message(FATAL_ERROR "asharia_attach_static_composition expects one Host target")',
        "  endif()",
        '  if(NOT TARGET "${target_name}")',
        '    message(FATAL_ERROR "Static composition Host target \'${target_name}\' does not exist")',
        "  endif()",
        '  get_target_property(_asharia_host_type "${target_name}" TYPE)',
        '  if(NOT _asharia_host_type STREQUAL "EXECUTABLE")',
        '    message(FATAL_ERROR "Static composition Host target \'${target_name}\' must be an EXECUTABLE")',
        "  endif()",
        '  get_property(_asharia_already_attached TARGET "${target_name}"',
        "      PROPERTY ASHARIA_STATIC_COMPOSITION_GENERATION_ID SET)",
        "  if(_asharia_already_attached)",
        '    message(FATAL_ERROR "Static composition is already attached to \'${target_name}\'")',
        "  endif()",
        "  if(NOT TARGET asharia::host_runtime_registration)",
        '    message(FATAL_ERROR "Required target asharia::host_runtime_registration does not exist")',
        "  endif()",
    ]
    for target in targets:
        lines.extend(
            (
                f"  if(NOT TARGET {target})",
                f'    message(FATAL_ERROR "Static provider target \'{target}\' does not exist")',
                "  endif()",
                f"  get_target_property(_asharia_provider_type {target} TYPE)",
                '  if(NOT _asharia_provider_type STREQUAL "STATIC_LIBRARY")',
                f'    message(FATAL_ERROR "Static provider target \'{target}\' must be a STATIC_LIBRARY")',
                "  endif()",
            )
        )
    lines.extend(
        (
            '  get_filename_component(_asharia_composition_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}" ABSOLUTE)',
            '  target_sources("${target_name}" PRIVATE',
            '      "${_asharia_composition_root}/src/static_composition_root.cpp")',
            '  target_include_directories("${target_name}" PRIVATE',
            '      "${_asharia_composition_root}/include")',
            '  target_link_libraries("${target_name}" PRIVATE',
            "      asharia::host_runtime_registration",
        )
    )
    lines.extend(f"      {value}" for value in targets)
    lines.extend(
        (
            "  )",
            f'  set_property(TARGET "${{target_name}}" PROPERTY ASHARIA_STATIC_COMPOSITION_GENERATION_ID "{generation_id}")',
            "endfunction()",
            "",
        )
    )
    return "\n".join(lines).encode("utf-8")


def _render_files_for_manifest(
    manifest: StaticCompositionRootManifest,
) -> tuple[GeneratedStaticCompositionFile, ...]:
    if (
        manifest.renderer_revision != STATIC_COMPOSITION_RENDERER_REVISION
        or manifest.provider_api != STATIC_COMPOSITION_PROVIDER_API
    ):
        raise ValueError(
            "static-composition renderer/provider API combination is unsupported"
        )

    cmake = _render_cmake_attachment(
        manifest.generation_id,
        manifest.providers,
    )
    header = _render_recording_header()
    source = _render_recording_source(
        manifest.generation_id,
        manifest.inputs.host_activation_blueprint_integrity.digest,
        manifest.providers,
    )

    return (
        GeneratedStaticCompositionFile(
            STATIC_COMPOSITION_CMAKE_PATH,
            "cmake-attachment",
            "text/x-cmake",
            cmake,
        ),
        GeneratedStaticCompositionFile(
            STATIC_COMPOSITION_HEADER_PATH,
            "declaration-header",
            "text/x-c++hdr",
            header,
        ),
        GeneratedStaticCompositionFile(
            STATIC_COMPOSITION_SOURCE_PATH,
            "registration-source",
            "text/x-c++src",
            source,
        ),
    )


def _same_integrity(left: Any, right: Any) -> bool:
    return left.algorithm == right.algorithm and left.digest == right.digest


def generate_static_composition_root(
    build_plan: source_build_plan.SourceBuildPlan,
    blueprint: activation.HostActivationBlueprint,
    binding_plan: provider_bindings.StaticFactoryProviderBindingPlan,
    validators: contracts.ContractValidators,
) -> StaticCompositionGenerationResult:
    """Generate exact bytes without reading or writing the filesystem."""

    diagnostics: list[contracts.Diagnostic] = []
    if not isinstance(build_plan, source_build_plan.SourceBuildPlan):
        diagnostics.append(
            _diagnostic(
                "static-composition.source-build-plan-required",
                "/inputs/sourceBuildPlanIntegrity",
                "generation requires a verified Source Build Plan",
            )
        )
    else:
        diagnostics.extend(
            source_build_plan.validate_source_build_plan_data(build_plan, validators)
        )
    if not isinstance(blueprint, activation.HostActivationBlueprint):
        diagnostics.append(
            _diagnostic(
                "static-composition.activation-blueprint-required",
                "/inputs/hostActivationBlueprintIntegrity",
                "generation requires a verified Host Activation Blueprint",
            )
        )
    else:
        diagnostics.extend(
            activation.validate_host_activation_blueprint_data(
                blueprint, validators
            )
        )
    if not isinstance(
        binding_plan, provider_bindings.StaticFactoryProviderBindingPlan
    ):
        diagnostics.append(
            _diagnostic(
                "static-composition.binding-plan-required",
                "/inputs/staticFactoryProviderBindingPlanIntegrity",
                "generation requires a verified Static Factory Provider Binding Plan",
            )
        )
    else:
        diagnostics.extend(
            provider_bindings.validate_static_factory_provider_binding_plan_data(
                binding_plan, validators
            )
        )
    if diagnostics:
        return _failure(diagnostics)

    assert isinstance(build_plan, source_build_plan.SourceBuildPlan)
    assert isinstance(blueprint, activation.HostActivationBlueprint)
    assert isinstance(
        binding_plan, provider_bindings.StaticFactoryProviderBindingPlan
    )
    if not _same_integrity(
        binding_plan.inputs.source_build_plan_integrity, build_plan.integrity
    ):
        diagnostics.append(
            _diagnostic(
                "static-composition.source-build-plan-mismatch",
                "/inputs/sourceBuildPlanIntegrity",
                "binding plan does not belong to the Source Build Plan",
            )
        )
    if not _same_integrity(
        binding_plan.inputs.host_activation_blueprint_integrity,
        blueprint.integrity,
    ):
        diagnostics.append(
            _diagnostic(
                "static-composition.activation-blueprint-mismatch",
                "/inputs/hostActivationBlueprintIntegrity",
                "binding plan does not belong to the Host Activation Blueprint",
            )
        )
    if not _same_integrity(
        binding_plan.inputs.effective_session_integrity,
        blueprint.inputs.effective_session_integrity,
    ):
        diagnostics.append(
            _diagnostic(
                "static-composition.effective-session-mismatch",
                "/inputs",
                "binding plan and Activation Blueprint use different Effective Sessions",
            )
        )
    if not _same_integrity(
        build_plan.inputs.host_composition_integrity,
        blueprint.inputs.host_composition_integrity,
    ):
        diagnostics.append(
            _diagnostic(
                "static-composition.host-composition-mismatch",
                "/inputs",
                "Source Build Plan and Activation Blueprint use different Host compositions",
            )
        )
    host_facts = {
        (build_plan.host_kind, build_plan.target_platform),
        (blueprint.host_kind, blueprint.target_platform),
        (binding_plan.host_kind, binding_plan.target_platform),
    }
    if len(host_facts) != 1:
        diagnostics.append(
            _diagnostic(
                "static-composition.host-mismatch",
                "/host",
                "Source Build Plan, Activation Blueprint, and binding Host facts differ",
            )
        )
    if binding_plan.engine_generation_id != blueprint.engine_generation_id:
        diagnostics.append(
            _diagnostic(
                "static-composition.engine-generation-mismatch",
                "/host/engineGenerationId",
                "binding plan and Activation Blueprint use different Engine generations",
            )
        )

    expected_factory_bindings = {
        (
            factory.reference.package_id,
            factory.reference.package_version,
            factory.reference.module_id,
            factory.reference.factory_id,
        ): tuple(
            (
                contribution.contribution_id,
                contribution.contribution_kind,
            )
            for contribution in factory.contributions
        )
        for scope in blueprint.scope_templates
        for factory in scope.factories
    }
    bound_factory_bindings = {
        (
            provider.package_id,
            provider.package_version,
            provider.module_id,
            factory.factory_id,
        ): tuple(
            (
                contribution.contribution_id,
                contribution.contribution_kind,
            )
            for contribution in factory.contributions
        )
        for provider in binding_plan.providers
        for factory in provider.factories
    }
    if bound_factory_bindings.keys() != expected_factory_bindings.keys():
        diagnostics.append(
            _diagnostic(
                "static-composition.factory-set-mismatch",
                "/providers",
                "provider mappings do not cover the exact Blueprint factory set",
            )
        )
    elif bound_factory_bindings != expected_factory_bindings:
        diagnostics.append(
            _diagnostic(
                "static-composition.contribution-set-mismatch",
                "/providers",
                (
                    "provider mappings do not carry the exact Blueprint "
                    "contribution set"
                ),
            )
        )
    selected_root_targets = {
        (value.name, value.target_type) for value in build_plan.build_roots
    }
    closure_targets = {
        (value.name, value.target_type) for value in build_plan.target_closure
    }
    for index, provider in enumerate(binding_plan.providers):
        target = (provider.target.name, provider.target.target_type)
        if target not in selected_root_targets or target not in closure_targets:
            diagnostics.append(
                _diagnostic(
                    "static-composition.provider-target-unselected",
                    f"/providers/{index}/target",
                    f"provider target '{provider.target.name}' is absent from the Source Build Plan",
                )
            )
    if diagnostics:
        return _failure(diagnostics)

    providers = tuple(sorted(binding_plan.providers, key=_provider_sort_key))
    inputs = StaticCompositionInputs(
        source_build_plan_integrity=IntegrityRecord(
            build_plan.integrity.algorithm, build_plan.integrity.digest
        ),
        host_activation_blueprint_integrity=IntegrityRecord(
            blueprint.integrity.algorithm, blueprint.integrity.digest
        ),
        static_factory_provider_binding_plan_integrity=IntegrityRecord(
            binding_plan.integrity.algorithm, binding_plan.integrity.digest
        ),
    )
    manifest = StaticCompositionRootManifest(
        generation_id="sha256-" + "0" * 64,
        renderer_revision=STATIC_COMPOSITION_RENDERER_REVISION,
        provider_api=STATIC_COMPOSITION_PROVIDER_API,
        inputs=inputs,
        engine_generation_id=blueprint.engine_generation_id,
        host_kind=blueprint.host_kind,
        target_platform=blueprint.target_platform,
        configuration=build_plan.configuration,
        generator_name=build_plan.generator.name,
        generator_multi_config=build_plan.generator.multi_config,
        compiler_id=build_plan.toolchain.compiler_id,
        compiler_version=build_plan.toolchain.compiler_version,
        target_system=build_plan.toolchain.target_system,
        target_architecture=build_plan.toolchain.target_architecture,
        providers=providers,
        files=(),
        integrity=IntegrityRecord("sha256", "0" * 64),
    )
    manifest = replace(manifest, generation_id=_generation_id(manifest))
    rendered_files = _render_files_for_manifest(manifest)
    file_evidence = tuple(
        StaticCompositionFileEvidence(
            value.path,
            value.role,
            value.media_type,
            len(value.content),
            _integrity_record(contracts.compute_bytes_integrity(value.content)),
        )
        for value in rendered_files
    )
    manifest = replace(manifest, files=file_evidence)
    manifest_integrity = compute_static_composition_root_manifest_integrity(
        manifest
    )
    manifest = replace(
        manifest,
        integrity=_integrity_record(manifest_integrity),
    )
    generation = StaticCompositionRootGeneration(manifest, rendered_files)
    generated_diagnostics = validate_static_composition_root_generation(
        generation, validators
    )
    if generated_diagnostics:
        return _failure(generated_diagnostics)
    return StaticCompositionGenerationResult(generation, ())


def validate_static_composition_root_generation(
    generation: StaticCompositionRootGeneration,
    validators: contracts.ContractValidators,
) -> list[contracts.Diagnostic]:
    """Revalidate manifest and every in-memory generated byte sequence."""

    manifest_diagnostics = validate_static_composition_root_manifest_data(
        generation.manifest, validators
    )
    diagnostics = list(manifest_diagnostics)
    files_by_path = {value.path: value for value in generation.files}
    if len(files_by_path) != len(generation.files):
        diagnostics.append(
            _diagnostic(
                "static-composition.duplicate-output",
                "/files",
                "generation contains duplicate output paths",
            )
        )
    expected_paths = {value.path for value in generation.manifest.files}
    if set(files_by_path) != expected_paths:
        diagnostics.append(
            _diagnostic(
                "static-composition.output-set-mismatch",
                "/files",
                "in-memory output set differs from the manifest",
            )
        )
    for index, evidence in enumerate(generation.manifest.files):
        generated = files_by_path.get(evidence.path)
        if generated is None:
            continue
        actual = _integrity_record(
            contracts.compute_bytes_integrity(generated.content)
        )
        if (
            generated.role != evidence.role
            or generated.media_type != evidence.media_type
            or len(generated.content) != evidence.size
            or actual != evidence.integrity
        ):
            diagnostics.append(
                _diagnostic(
                    "static-composition.output-integrity-mismatch",
                    f"/files/{index}",
                    f"generated bytes for '{evidence.path}' differ from the manifest",
                )
            )
    if not manifest_diagnostics:
        expected_rendered = {
            value.path: value
            for value in _render_files_for_manifest(generation.manifest)
        }
        for index, evidence in enumerate(generation.manifest.files):
            generated = files_by_path.get(evidence.path)
            expected = expected_rendered.get(evidence.path)
            if (
                generated is not None
                and expected is not None
                and generated != expected
            ):
                diagnostics.append(
                    _diagnostic(
                        "static-composition.renderer-output-mismatch",
                        f"/files/{index}",
                        (
                            f"generated bytes for '{evidence.path}' do not match "
                            f"renderer revision {generation.manifest.renderer_revision}"
                        ),
                    )
                )
    return sorted(diagnostics, key=_diagnostic_sort_key)


def expected_static_composition_publication_files(
    generation: StaticCompositionRootGeneration,
) -> dict[str, bytes]:
    """Return the exact closed publication file set for one generation."""

    return {
        STATIC_COMPOSITION_ROOT_NAME: render_static_composition_root_manifest(
            generation.manifest
        ).encode("utf-8"),
        **{value.path: value.content for value in generation.files},
    }


def _is_link_or_reparse(status: os.stat_result) -> bool:
    if stat.S_ISLNK(status.st_mode):
        return True
    reparse_attribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    return bool(getattr(status, "st_file_attributes", 0) & reparse_attribute)


def _prepare_publication_root(path: Path) -> Path:
    if not isinstance(path, Path):
        raise OSError("publication root must use an explicit pathlib.Path")

    absolute = path.absolute()

    def inspect_existing_chain() -> None:
        current = Path(absolute.anchor)
        for component in absolute.parts[1:]:
            current /= component
            try:
                status = current.lstat()
            except FileNotFoundError:
                break
            except OSError as error:
                raise OSError(
                    f"could not inspect publication root component '{current}': {error}"
                ) from error
            if _is_link_or_reparse(status):
                raise OSError(
                    f"publication root crosses link/reparse point '{current}'"
                )
            if not stat.S_ISDIR(status.st_mode):
                raise OSError(
                    f"publication root component is not a directory: '{current}'"
                )

    inspect_existing_chain()
    absolute.mkdir(parents=True, exist_ok=True)
    inspect_existing_chain()
    return absolute.resolve(strict=True)


def publish_static_composition_root(
    generation: StaticCompositionRootGeneration,
    destination_root: Path,
    validators: contracts.ContractValidators,
) -> StaticCompositionPublicationResult:
    """Publish one immutable generation with staging, verification, and rename."""

    diagnostics = validate_static_composition_root_generation(
        generation, validators
    )
    if diagnostics:
        return _publication_failure(diagnostics)

    expected = expected_static_composition_publication_files(generation)
    staging_path: Path | None = None
    try:
        destination_root = _prepare_publication_root(destination_root)
        final_path = destination_root / generation.manifest.generation_id
        if os.path.lexists(final_path):
            generated_publication_tree.verify_exact_publication_tree(
                final_path, expected
            )
            return StaticCompositionPublicationResult(
                StaticCompositionPublicationReceipt(
                    generation.manifest.generation_id,
                    final_path,
                    generation.manifest.integrity,
                    True,
                ),
                (),
            )

        staging_path = Path(
            tempfile.mkdtemp(prefix=".asharia-static-composition-", dir=destination_root)
        )
        for relative, content in expected.items():
            destination = staging_path / Path(relative)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(content)
        generated_publication_tree.verify_exact_publication_tree(
            staging_path, expected
        )
        try:
            os.rename(staging_path, final_path)
            staging_path = None
            reused = False
        except OSError:
            if not os.path.lexists(final_path):
                raise
            generated_publication_tree.verify_exact_publication_tree(
                final_path, expected
            )
            reused = True
        return StaticCompositionPublicationResult(
            StaticCompositionPublicationReceipt(
                generation.manifest.generation_id,
                final_path,
                generation.manifest.integrity,
                reused,
            ),
            (),
        )
    except OSError as error:
        return _publication_failure(
            [
                _diagnostic(
                    "static-composition.publication-failed",
                    "/generationId",
                    f"could not publish immutable generation: {error}",
                )
            ]
        )
    finally:
        if staging_path is not None:
            shutil.rmtree(staging_path, ignore_errors=True)
