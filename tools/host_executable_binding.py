"""Canonical identity for one verified generated Host executable.

This pure model binds prior generated inputs, CMake target semantics, exact
artifact bytes, and a registration snapshot. It does not collect files, run the
Host, publish a generation, or claim that activation succeeded.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, replace
from typing import Any

from tools import check_package_contracts as contracts


HOST_EXECUTABLE_BINDING_RECEIPT_NAME = (
    "asharia.host-executable-binding-receipt.json"
)
HOST_EXECUTABLE_BINDING_RECEIPT_SCHEMA = (
    "com.asharia.host-executable-binding-receipt"
)
HOST_EXECUTABLE_BINDING_RECEIPT_SCHEMA_VERSION = 1
HOST_EXECUTABLE_MEDIA_TYPE = "application/vnd.microsoft.portable-executable"
HOST_REGISTRATION_SNAPSHOT_MEDIA_TYPE = "application/json"
HOST_REGISTRATION_SNAPSHOT_PATH = (
    "evidence/asharia.static-factory-registration-snapshot.json"
)


@dataclass(frozen=True, order=True)
class IntegrityRecord:
    """Immutable SHA-256 evidence for exact bytes."""

    algorithm: str
    digest: str


@dataclass(frozen=True, order=True)
class GeneratedInputIdentity:
    """Content identity of one generated control-plane input."""

    generation_id: str
    manifest_integrity: IntegrityRecord


@dataclass(frozen=True)
class HostExecutableBindingInputs:
    """Authoritative generated inputs bound by this receipt."""

    static_composition: GeneratedInputIdentity
    host_template: GeneratedInputIdentity
    host_activation_blueprint_integrity: IntegrityRecord


@dataclass(frozen=True, order=True)
class HostIdentity:
    """Host selection inherited from the verified composition."""

    engine_generation_id: str
    host_kind: str
    target_platform: str


@dataclass(frozen=True, order=True)
class BuildGeneratorEvidence:
    """Configured CMake generator semantics, without machine-local paths."""

    name: str
    multi_config: bool


@dataclass(frozen=True, order=True)
class ConfiguredCompilerEvidence:
    """Configured compiler context; not a trusted-builder attestation."""

    language: str
    compiler_id: str
    compiler_version: str
    target: str | None


@dataclass(frozen=True)
class HostBuildIdentity:
    """Machine-neutral configuration used to identify the Host build."""

    configuration: str
    generator: BuildGeneratorEvidence
    configured_compiler: ConfiguredCompilerEvidence


@dataclass(frozen=True, order=True)
class HostTargetEvidence:
    """Latest CMake File API binding, distinct from artifact-byte evidence."""

    name: str
    target_type: str
    name_on_disk: str
    build_artifact_relative_path: str
    codemodel_major: int
    codemodel_minor: int
    toolchains_major: int
    toolchains_minor: int


@dataclass(frozen=True, order=True)
class BoundFileEvidence:
    """One immutable-generation relative file and its exact bytes."""

    path: str
    media_type: str
    size: int
    integrity: IntegrityRecord


@dataclass(frozen=True)
class HostExecutableBindingReceiptV1:
    """Content-derived binding evidence, deliberately not activation state."""

    binding_generation_id: str
    inputs: HostExecutableBindingInputs
    host: HostIdentity
    build: HostBuildIdentity
    target: HostTargetEvidence
    artifact: BoundFileEvidence
    registration_snapshot: BoundFileEvidence
    integrity: IntegrityRecord


@dataclass(frozen=True)
class HostExecutableBindingParseResult:
    """Atomic strict parse result."""

    receipt: HostExecutableBindingReceiptV1 | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.receipt is not None and not self.diagnostics


def _integrity_data(value: IntegrityRecord) -> dict[str, str]:
    return {"algorithm": value.algorithm, "digest": value.digest}


def _integrity_record(value: dict[str, str]) -> IntegrityRecord:
    return IntegrityRecord(value["algorithm"], value["digest"])


def _input_data(value: GeneratedInputIdentity) -> dict[str, Any]:
    return {
        "generationId": value.generation_id,
        "manifestIntegrity": _integrity_data(value.manifest_integrity),
    }


def _file_data(value: BoundFileEvidence) -> dict[str, Any]:
    return {
        "path": value.path,
        "mediaType": value.media_type,
        "size": value.size,
        "integrity": _integrity_data(value.integrity),
    }


def _descriptor_data(receipt: HostExecutableBindingReceiptV1) -> dict[str, Any]:
    return {
        "schema": HOST_EXECUTABLE_BINDING_RECEIPT_SCHEMA,
        "schemaVersion": HOST_EXECUTABLE_BINDING_RECEIPT_SCHEMA_VERSION,
        "inputs": {
            "staticComposition": _input_data(receipt.inputs.static_composition),
            "hostTemplate": _input_data(receipt.inputs.host_template),
            "hostActivationBlueprintIntegrity": _integrity_data(
                receipt.inputs.host_activation_blueprint_integrity
            ),
        },
        "host": {
            "engineGenerationId": receipt.host.engine_generation_id,
            "hostKind": receipt.host.host_kind,
            "targetPlatform": receipt.host.target_platform,
        },
        "build": {
            "configuration": receipt.build.configuration,
            "generator": {
                "name": receipt.build.generator.name,
                "multiConfig": receipt.build.generator.multi_config,
            },
            "configuredCompiler": {
                "language": receipt.build.configured_compiler.language,
                "compilerId": receipt.build.configured_compiler.compiler_id,
                "compilerVersion": (
                    receipt.build.configured_compiler.compiler_version
                ),
                **(
                    {"target": receipt.build.configured_compiler.target}
                    if receipt.build.configured_compiler.target is not None
                    else {}
                ),
            },
        },
        "target": {
            "name": receipt.target.name,
            "type": receipt.target.target_type,
            "nameOnDisk": receipt.target.name_on_disk,
            "buildArtifactRelativePath": (
                receipt.target.build_artifact_relative_path
            ),
            "fileApi": {
                "codemodel": {
                    "major": receipt.target.codemodel_major,
                    "minor": receipt.target.codemodel_minor,
                },
                "toolchains": {
                    "major": receipt.target.toolchains_major,
                    "minor": receipt.target.toolchains_minor,
                },
            },
        },
        "artifact": _file_data(receipt.artifact),
        "registrationSnapshot": _file_data(receipt.registration_snapshot),
    }


def _payload_data(receipt: HostExecutableBindingReceiptV1) -> dict[str, Any]:
    descriptor = _descriptor_data(receipt)
    return {
        "schema": descriptor.pop("schema"),
        "schemaVersion": descriptor.pop("schemaVersion"),
        "bindingGenerationId": receipt.binding_generation_id,
        **descriptor,
    }


def host_executable_binding_receipt_to_data(
    receipt: HostExecutableBindingReceiptV1,
) -> dict[str, Any]:
    """Return the fixed-field-order JSON-compatible receipt."""

    return {**_payload_data(receipt), "integrity": _integrity_data(receipt.integrity)}


def render_host_executable_binding_receipt(
    receipt: HostExecutableBindingReceiptV1,
) -> str:
    """Render fixed canonical bytes as UTF-8 JSON with LF and one final newline."""

    return json.dumps(
        host_executable_binding_receipt_to_data(receipt),
        ensure_ascii=False,
        indent=2,
    ) + "\n"


def _canonical_integrity(value: Any) -> IntegrityRecord:
    content = json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return _integrity_record(contracts.compute_bytes_integrity(content))


def compute_host_executable_binding_generation_id(
    receipt: HostExecutableBindingReceiptV1,
) -> str:
    """Derive the binding identity from every field except IDs and self-integrity."""

    return "sha256-" + _canonical_integrity(_descriptor_data(receipt)).digest


def compute_host_executable_binding_receipt_integrity(
    receipt: HostExecutableBindingReceiptV1,
) -> IntegrityRecord:
    """Hash every canonical receipt field except self-integrity."""

    return _canonical_integrity(_payload_data(receipt))


def create_host_executable_binding_receipt(
    *,
    inputs: HostExecutableBindingInputs,
    host: HostIdentity,
    build: HostBuildIdentity,
    target: HostTargetEvidence,
    artifact: BoundFileEvidence,
    registration_snapshot: BoundFileEvidence,
) -> HostExecutableBindingReceiptV1:
    """Finalize content-derived identity and self-integrity for verified evidence."""

    empty = IntegrityRecord("sha256", "0" * 64)
    receipt = HostExecutableBindingReceiptV1(
        binding_generation_id="sha256-" + "0" * 64,
        inputs=inputs,
        host=host,
        build=build,
        target=target,
        artifact=artifact,
        registration_snapshot=registration_snapshot,
        integrity=empty,
    )
    receipt = replace(
        receipt,
        binding_generation_id=compute_host_executable_binding_generation_id(
            receipt
        ),
    )
    return replace(
        receipt,
        integrity=compute_host_executable_binding_receipt_integrity(receipt),
    )


def validate_host_executable_binding_receipt_data(
    receipt: HostExecutableBindingReceiptV1 | Any,
    validators: contracts.ContractValidators,
) -> list[contracts.Diagnostic]:
    """Deeply validate closed shape, path semantics, identity, and integrity."""

    from tools import host_executable_binding_codec as codec

    return codec.validate_receipt_data(receipt, validators)


def parse_host_executable_binding_receipt_bytes(
    content: Any,
    validators: contracts.ContractValidators,
) -> HostExecutableBindingParseResult:
    """Strictly parse one complete canonical receipt without partial success."""

    from tools import host_executable_binding_codec as codec

    return codec.parse_receipt_bytes(content, validators)
