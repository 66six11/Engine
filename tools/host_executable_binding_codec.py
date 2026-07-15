"""Strict serde and deep validation for Host executable binding receipts."""

from __future__ import annotations

import json
import unicodedata
from pathlib import PurePosixPath
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import host_executable_binding as binding


def _diagnostic(code: str, pointer: str, message: str) -> contracts.Diagnostic:
    return contracts.Diagnostic(
        code=code,
        manifest_path=binding.HOST_EXECUTABLE_BINDING_RECEIPT_NAME,
        pointer=pointer,
        message=message,
    )


def _sorted_diagnostics(
    diagnostics: Iterable[contracts.Diagnostic],
) -> tuple[contracts.Diagnostic, ...]:
    return tuple(
        sorted(
            diagnostics,
            key=lambda value: (
                value.manifest_path,
                value.pointer,
                value.code,
                value.message,
            ),
        )
    )


def _failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> binding.HostExecutableBindingParseResult:
    return binding.HostExecutableBindingParseResult(
        None, _sorted_diagnostics(diagnostics)
    )


def _relative_path(value: str) -> PurePosixPath | None:
    if (
        unicodedata.normalize("NFC", value) != value
        or "\\" in value
        or ":" in value
        or "//" in value
        or value.endswith("/")
    ):
        return None
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in {"", ".", ".."} for part in path.parts):
        return None
    return path


def _integrity(value: dict[str, str]) -> binding.IntegrityRecord:
    return binding.IntegrityRecord(value["algorithm"], value["digest"])


def _receipt_from_data(data: dict[str, Any]) -> binding.HostExecutableBindingReceiptV1:
    inputs = data["inputs"]
    build = data["build"]
    compiler = build["configuredCompiler"]
    target = data["target"]
    codemodel = target["fileApi"]["codemodel"]
    toolchains = target["fileApi"]["toolchains"]

    def identity(value: dict[str, Any]) -> binding.GeneratedInputIdentity:
        return binding.GeneratedInputIdentity(
            value["generationId"], _integrity(value["manifestIntegrity"])
        )

    def file_evidence(value: dict[str, Any]) -> binding.BoundFileEvidence:
        return binding.BoundFileEvidence(
            value["path"],
            value["mediaType"],
            value["size"],
            _integrity(value["integrity"]),
        )

    return binding.HostExecutableBindingReceiptV1(
        binding_generation_id=data["bindingGenerationId"],
        inputs=binding.HostExecutableBindingInputs(
            identity(inputs["staticComposition"]),
            identity(inputs["hostTemplate"]),
            _integrity(inputs["hostActivationBlueprintIntegrity"]),
        ),
        host=binding.HostIdentity(
            data["host"]["engineGenerationId"],
            data["host"]["hostKind"],
            data["host"]["targetPlatform"],
        ),
        build=binding.HostBuildIdentity(
            build["configuration"],
            binding.BuildGeneratorEvidence(
                build["generator"]["name"], build["generator"]["multiConfig"]
            ),
            binding.ConfiguredCompilerEvidence(
                compiler["language"],
                compiler["compilerId"],
                compiler["compilerVersion"],
                compiler.get("target"),
            ),
        ),
        target=binding.HostTargetEvidence(
            target["name"],
            target["type"],
            target["nameOnDisk"],
            target["buildArtifactRelativePath"],
            codemodel["major"],
            codemodel["minor"],
            toolchains["major"],
            toolchains["minor"],
        ),
        artifact=file_evidence(data["artifact"]),
        registration_snapshot=file_evidence(data["registrationSnapshot"]),
        integrity=_integrity(data["integrity"]),
    )


def validate_receipt_data(
    receipt: binding.HostExecutableBindingReceiptV1 | Any,
    validators: contracts.ContractValidators,
) -> list[contracts.Diagnostic]:
    """Validate schema, relative layout, content identity, and self-integrity."""

    data = (
        binding.host_executable_binding_receipt_to_data(receipt)
        if isinstance(receipt, binding.HostExecutableBindingReceiptV1)
        else receipt
    )
    diagnostics = contracts.validate_manifest_data(
        data, binding.HOST_EXECUTABLE_BINDING_RECEIPT_NAME, validators
    )
    if diagnostics or not isinstance(data, dict):
        return diagnostics
    parsed = _receipt_from_data(data)
    build_path = _relative_path(parsed.target.build_artifact_relative_path)
    if build_path is None or build_path.name != parsed.target.name_on_disk:
        diagnostics.append(
            _diagnostic(
                "host-binding.target-artifact-mismatch",
                "/target/buildArtifactRelativePath",
                "File API artifact path must be relative and end in nameOnDisk",
            )
        )
    if parsed.artifact.path != f"host/{parsed.target.name_on_disk}":
        diagnostics.append(
            _diagnostic(
                "host-binding.artifact-path-mismatch",
                "/artifact/path",
                "published Host artifact path must be host/<nameOnDisk>",
            )
        )
    if parsed.registration_snapshot.path != binding.HOST_REGISTRATION_SNAPSHOT_PATH:
        diagnostics.append(
            _diagnostic(
                "host-binding.snapshot-path-mismatch",
                "/registrationSnapshot/path",
                "registration snapshot must use the fixed evidence path",
            )
        )
    if (
        parsed.binding_generation_id
        != binding.compute_host_executable_binding_generation_id(parsed)
    ):
        diagnostics.append(
            _diagnostic(
                "host-binding.generation-id-mismatch",
                "/bindingGenerationId",
                "binding generation ID does not match canonical evidence",
            )
        )
    if (
        parsed.integrity
        != binding.compute_host_executable_binding_receipt_integrity(parsed)
    ):
        diagnostics.append(
            _diagnostic(
                "host-binding.integrity-mismatch",
                "/integrity",
                "receipt integrity does not match canonical fields",
            )
        )
    return list(_sorted_diagnostics(diagnostics))


def parse_receipt_bytes(
    content: Any,
    validators: contracts.ContractValidators,
) -> binding.HostExecutableBindingParseResult:
    """Strictly parse one complete canonical receipt without partial success."""

    if not isinstance(content, bytes):
        return _failure(
            [
                _diagnostic(
                    "host-binding.bytes-required",
                    "",
                    "receipt input must be exact bytes",
                )
            ]
        )
    try:
        data = json.loads(content.decode("utf-8"))
    except UnicodeDecodeError:
        return _failure(
            [_diagnostic("host-binding.utf8-invalid", "", "receipt is not valid UTF-8")]
        )
    except json.JSONDecodeError:
        return _failure(
            [
                _diagnostic(
                    "host-binding.json-invalid",
                    "",
                    "receipt is not one JSON value",
                )
            ]
        )
    diagnostics = validate_receipt_data(data, validators)
    if diagnostics:
        return _failure(diagnostics)
    assert isinstance(data, dict)
    receipt = _receipt_from_data(data)
    canonical = binding.render_host_executable_binding_receipt(receipt).encode(
        "utf-8"
    )
    if canonical != content:
        return _failure(
            [
                _diagnostic(
                    "host-binding.canonical-bytes-mismatch",
                    "",
                    "receipt bytes are not in fixed canonical form",
                )
            ]
        )
    return binding.HostExecutableBindingParseResult(receipt, ())
