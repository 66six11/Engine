"""Assemble one Host executable binding from already verified evidence."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import host_artifact_collection
from tools import host_binding_inputs
from tools import host_cmake_toolchain
from tools import host_executable_binding as binding
from tools import host_executable_template as host_template
from tools import host_registration_cross_verifier as cross_verifier
from tools import host_registration_snapshot
from tools import static_composition_root as composition


@dataclass(frozen=True)
class HostExecutableBindingAssembly:
    """Canonical receipt and the exact canonical snapshot bytes it binds."""

    receipt: binding.HostExecutableBindingReceiptV1
    snapshot: host_registration_snapshot.HostRegistrationSnapshot
    snapshot_bytes: bytes


@dataclass(frozen=True)
class HostExecutableBindingAssemblyResult:
    assembly: HostExecutableBindingAssembly | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.assembly is not None and not self.diagnostics


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
        code,
        binding.HOST_EXECUTABLE_BINDING_RECEIPT_NAME,
        pointer,
        message,
    )


def _failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> HostExecutableBindingAssemblyResult:
    unique = {
        (value.manifest_path, value.pointer, value.code, value.message): value
        for value in diagnostics
    }
    return HostExecutableBindingAssemblyResult(
        None, tuple(sorted(unique.values(), key=_diagnostic_sort_key))
    )


def _integrity(value: composition.IntegrityRecord) -> binding.IntegrityRecord:
    return binding.IntegrityRecord(value.algorithm, value.digest)


def assemble_host_executable_binding(
    composition_generation: Any,
    template_generation: Any,
    configured_target: Any,
    artifact: Any,
    snapshot: Any,
    validators: contracts.ContractValidators,
) -> HostExecutableBindingAssemblyResult:
    """Cross-check typed handoffs and create one canonical receipt."""

    diagnostics: list[contracts.Diagnostic] = []
    if not isinstance(
        composition_generation, composition.StaticCompositionRootGeneration
    ):
        diagnostics.append(
            _diagnostic(
                "host-binding.input.composition-invalid",
                "/inputs/staticComposition",
                "assembly requires one complete static-composition generation",
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
                "assembly requires one complete Host Template generation",
            )
        )
    else:
        diagnostics.extend(
            host_template.validate_windows_development_host_template_generation(
                template_generation, validators
            )
        )
    if not isinstance(
        configured_target,
        host_cmake_toolchain.HostCMakeConfiguredTargetEvidence,
    ):
        diagnostics.append(
            _diagnostic(
                "host-binding.cmake.evidence-invalid",
                "/target/fileApi",
                "assembly requires same-index target and compiler evidence",
            )
        )
    if not isinstance(artifact, host_artifact_collection.CollectedHostArtifact):
        diagnostics.append(
            _diagnostic(
                "host-binding.artifact.evidence-invalid",
                "/artifact",
                "assembly requires one collected Host artifact",
            )
        )
    if diagnostics:
        return _failure(diagnostics)
    assert isinstance(
        composition_generation, composition.StaticCompositionRootGeneration
    )
    assert isinstance(
        template_generation,
        host_template.WindowsDevelopmentHostTemplateGenerationV1,
    )
    assert isinstance(
        configured_target,
        host_cmake_toolchain.HostCMakeConfiguredTargetEvidence,
    )
    assert isinstance(artifact, host_artifact_collection.CollectedHostArtifact)

    diagnostics.extend(
        host_binding_inputs.composition_template_diagnostics(
            composition_generation,
            template_generation,
        )
    )
    diagnostics.extend(
        host_binding_inputs.configured_target_diagnostics(
            composition_generation,
            template_generation,
            configured_target,
        )
    )
    cross = cross_verifier.verify_host_registration_cross_binding(
        composition_generation,
        snapshot,
        validators,
    )
    diagnostics.extend(cross.diagnostics)
    if diagnostics:
        return _failure(diagnostics)
    assert cross.verified is not None

    verified_snapshot = host_registration_snapshot.HostRegistrationSnapshot(
        cross.verified.generation_id,
        cross.verified.host_activation_blueprint_sha256,
        cross.verified.registrations,
    )
    snapshot_bytes = (
        host_registration_snapshot.render_host_registration_snapshot(
            verified_snapshot
        ).encode("utf-8")
    )
    snapshot_integrity = contracts.compute_bytes_integrity(snapshot_bytes)
    composition_manifest = composition_generation.manifest
    template_manifest = template_generation.manifest
    target = configured_target.target
    compiler = configured_target.configured_compiler
    receipt = binding.create_host_executable_binding_receipt(
        inputs=binding.HostExecutableBindingInputs(
            binding.GeneratedInputIdentity(
                composition_manifest.generation_id,
                _integrity(composition_manifest.integrity),
            ),
            binding.GeneratedInputIdentity(
                template_manifest.generation_id,
                _integrity(template_manifest.integrity),
            ),
            _integrity(
                composition_manifest.inputs.host_activation_blueprint_integrity
            ),
        ),
        host=binding.HostIdentity(
            composition_manifest.engine_generation_id,
            composition_manifest.host_kind,
            composition_manifest.target_platform,
        ),
        build=binding.HostBuildIdentity(
            composition_manifest.configuration,
            binding.BuildGeneratorEvidence(
                configured_target.generator.name,
                configured_target.generator.multi_config,
            ),
            binding.ConfiguredCompilerEvidence(
                compiler.language,
                compiler.compiler_id,
                compiler.compiler_version,
                compiler.target,
            ),
        ),
        target=binding.HostTargetEvidence(
            target.target_name,
            target.target_type,
            target.name_on_disk,
            target.artifact_relative_path,
            target.codemodel_major,
            target.codemodel_minor,
            configured_target.toolchains_major,
            configured_target.toolchains_minor,
        ),
        artifact=binding.BoundFileEvidence(
            artifact.publication_path,
            artifact.media_type,
            artifact.size,
            binding.IntegrityRecord(
                artifact.integrity.algorithm,
                artifact.integrity.digest,
            ),
        ),
        registration_snapshot=binding.BoundFileEvidence(
            binding.HOST_REGISTRATION_SNAPSHOT_PATH,
            binding.HOST_REGISTRATION_SNAPSHOT_MEDIA_TYPE,
            len(snapshot_bytes),
            binding.IntegrityRecord(
                snapshot_integrity["algorithm"],
                snapshot_integrity["digest"],
            ),
        ),
    )
    diagnostics.extend(
        binding.validate_host_executable_binding_receipt_data(
            receipt, validators
        )
    )
    if diagnostics:
        return _failure(diagnostics)
    return HostExecutableBindingAssemblyResult(
        HostExecutableBindingAssembly(
            receipt,
            verified_snapshot,
            snapshot_bytes,
        ),
        (),
    )


__all__ = [
    "HostExecutableBindingAssembly",
    "HostExecutableBindingAssemblyResult",
    "assemble_host_executable_binding",
]
