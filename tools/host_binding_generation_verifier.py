"""Read-only deep verification for a published Host binding generation."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import host_artifact_io
from tools import host_binding_generation_io
from tools import host_binding_inputs
from tools import host_executable_binding as binding
from tools import host_executable_template as host_template
from tools import host_registration_cross_verifier
from tools import host_registration_snapshot
from tools import static_composition_root as composition


_MAX_RECEIPT_BYTES = 1024 * 1024
_MAX_SNAPSHOT_BYTES = 4 * 1024 * 1024


@dataclass(frozen=True)
class VerifiedHostBindingGeneration:
    receipt: binding.HostExecutableBindingReceiptV1
    snapshot: host_registration_snapshot.HostRegistrationSnapshot
    generation_path: Path = field(repr=False)


@dataclass(frozen=True)
class HostBindingGenerationVerificationResult:
    verified: VerifiedHostBindingGeneration | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.verified is not None and not self.diagnostics


def _diagnostic(code: str, pointer: str, message: str) -> contracts.Diagnostic:
    return contracts.Diagnostic(
        code,
        binding.HOST_EXECUTABLE_BINDING_RECEIPT_NAME,
        pointer,
        message,
    )


def _failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> HostBindingGenerationVerificationResult:
    unique = {
        (value.manifest_path, value.pointer, value.code, value.message): value
        for value in diagnostics
    }
    ordered = sorted(
        unique.values(),
        key=lambda value: (
            value.manifest_path,
            value.pointer,
            value.code,
            value.message,
        ),
    )
    return HostBindingGenerationVerificationResult(None, tuple(ordered))


def _integrity_key(value: Any) -> tuple[str, str]:
    return value.algorithm, value.digest


def verify_host_binding_generation_tree(
    generation_path: Any,
    composition_generation: Any,
    template_generation: Any,
    validators: contracts.ContractValidators,
    *,
    require_generation_directory_name: bool,
) -> HostBindingGenerationVerificationResult:
    """Deeply verify one closed tree, including streamed executable bytes."""

    diagnostics: list[contracts.Diagnostic] = []
    if not isinstance(generation_path, Path):
        diagnostics.append(
            _diagnostic(
                "host-binding.publication.path-invalid",
                "/publication",
                "generation path must use pathlib.Path",
            )
        )
    if not isinstance(
        composition_generation, composition.StaticCompositionRootGeneration
    ):
        diagnostics.append(
            _diagnostic(
                "host-binding.input.composition-invalid",
                "/inputs/staticComposition",
                "verification requires one complete static-composition generation",
            )
        )
    else:
        diagnostics.extend(
            composition.validate_static_composition_root_generation(
                composition_generation, validators
            )
        )
    if not isinstance(
        template_generation,
        host_template.WindowsDevelopmentHostTemplateGenerationV1,
    ):
        diagnostics.append(
            _diagnostic(
                "host-binding.input.template-invalid",
                "/inputs/hostTemplate",
                "verification requires one complete Host Template generation",
            )
        )
    else:
        diagnostics.extend(
            host_template.validate_windows_development_host_template_generation(
                template_generation, validators
            )
        )
    if diagnostics:
        return _failure(diagnostics)
    assert isinstance(generation_path, Path)
    assert isinstance(
        composition_generation, composition.StaticCompositionRootGeneration
    )
    assert isinstance(
        template_generation,
        host_template.WindowsDevelopmentHostTemplateGenerationV1,
    )

    try:
        root = host_binding_generation_io.resolve_existing_generation_root(
            generation_path
        )
        receipt_bytes = host_binding_generation_io.read_stable_small_regular(
            root / binding.HOST_EXECUTABLE_BINDING_RECEIPT_NAME,
            _MAX_RECEIPT_BYTES,
            "/receipt",
        )
        parsed = binding.parse_host_executable_binding_receipt_bytes(
            receipt_bytes, validators
        )
        if not parsed.succeeded:
            return _failure(parsed.diagnostics)
        assert parsed.receipt is not None
        receipt = parsed.receipt
        if (
            require_generation_directory_name
            and root.name != receipt.binding_generation_id
        ):
            return _failure(
                (
                    _diagnostic(
                        "host-binding.publication.generation-path-mismatch",
                        "/bindingGenerationId",
                        "generation directory must equal the binding generation ID",
                    ),
                )
            )
        expected_files = (
            binding.HOST_EXECUTABLE_BINDING_RECEIPT_NAME,
            receipt.artifact.path,
            receipt.registration_snapshot.path,
        )
        expected_directories = ("host", "evidence")
        host_binding_generation_io.validate_closed_generation_layout(
            root,
            expected_files,
            expected_directories,
        )
        diagnostics.extend(
            host_binding_inputs.receipt_input_diagnostics(
                receipt, composition_generation, template_generation
            )
        )
        snapshot_bytes = host_binding_generation_io.read_stable_small_regular(
            root.joinpath(*receipt.registration_snapshot.path.split("/")),
            _MAX_SNAPSHOT_BYTES,
            "/registrationSnapshot",
        )
        snapshot_integrity = contracts.compute_bytes_integrity(snapshot_bytes)
        if (
            len(snapshot_bytes) != receipt.registration_snapshot.size
            or (
                snapshot_integrity["algorithm"],
                snapshot_integrity["digest"],
            )
            != (
                receipt.registration_snapshot.integrity.algorithm,
                receipt.registration_snapshot.integrity.digest,
            )
        ):
            diagnostics.append(
                _diagnostic(
                    "host-binding.publication.snapshot-integrity-mismatch",
                    "/registrationSnapshot",
                    "published snapshot bytes differ from the receipt",
                )
            )
        parsed_snapshot = (
            host_registration_snapshot.parse_host_registration_snapshot_bytes(
                snapshot_bytes,
                validators,
                expected_generation_id=composition_generation.manifest.generation_id,
                expected_host_activation_blueprint_sha256=(
                    composition_generation.manifest.inputs.host_activation_blueprint_integrity.digest
                ),
            )
        )
        diagnostics.extend(parsed_snapshot.diagnostics)
        if parsed_snapshot.snapshot is not None:
            diagnostics.extend(
                host_registration_cross_verifier.verify_host_registration_cross_binding(
                    composition_generation,
                    parsed_snapshot.snapshot,
                    validators,
                ).diagnostics
            )
        artifact_path = root.joinpath(*receipt.artifact.path.split("/"))
        observed_artifact = host_artifact_io.observe_host_artifact(artifact_path)
        diagnostics.extend(observed_artifact.diagnostics)
        if observed_artifact.observation is not None and (
            observed_artifact.observation.size != receipt.artifact.size
            or _integrity_key(observed_artifact.observation.integrity)
            != _integrity_key(receipt.artifact.integrity)
        ):
            diagnostics.append(
                _diagnostic(
                    "host-binding.publication.artifact-integrity-mismatch",
                    "/artifact",
                    "published Host bytes differ from the receipt",
                )
            )
        closing = host_binding_generation_io.close_generation_observation(
            root,
            receipt_path=(
                root / binding.HOST_EXECUTABLE_BINDING_RECEIPT_NAME
            ),
            receipt_limit=_MAX_RECEIPT_BYTES,
            snapshot_path=root.joinpath(
                *receipt.registration_snapshot.path.split("/")
            ),
            snapshot_limit=_MAX_SNAPSHOT_BYTES,
            artifact_path=artifact_path,
            expected_files=expected_files,
            expected_directories=expected_directories,
        )
        diagnostics.extend(closing.artifact.diagnostics)
        if (
            closing.receipt_bytes != receipt_bytes
            or closing.snapshot_bytes != snapshot_bytes
            or closing.artifact.observation != observed_artifact.observation
        ):
            diagnostics.append(
                _diagnostic(
                    "host-binding.publication.evidence-drift",
                    "/publication",
                    "binding generation changed during deep verification",
                )
            )
        if diagnostics or parsed_snapshot.snapshot is None:
            return _failure(diagnostics)
        return HostBindingGenerationVerificationResult(
            VerifiedHostBindingGeneration(receipt, parsed_snapshot.snapshot, root),
            (),
        )
    except host_binding_generation_io.HostBindingGenerationIOError as error:
        return _failure((_diagnostic(error.code, error.pointer, error.message),))


def verify_published_host_binding_generation(
    generation_path: Any,
    composition_generation: Any,
    template_generation: Any,
    validators: contracts.ContractValidators,
) -> HostBindingGenerationVerificationResult:
    """Verify one committed generation whose directory is content-derived."""

    return verify_host_binding_generation_tree(
        generation_path,
        composition_generation,
        template_generation,
        validators,
        require_generation_directory_name=True,
    )


__all__ = [
    "HostBindingGenerationVerificationResult",
    "VerifiedHostBindingGeneration",
    "verify_host_binding_generation_tree",
    "verify_published_host_binding_generation",
]
