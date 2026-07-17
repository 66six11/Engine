"""Shared typed fixtures for Bootstrap Host adapter tests."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from tools import bootstrap_current_host
from tools import bootstrap_session as bootstrap
from tools import check_package_contracts as contracts
from tools import effective_session
from tools import engine_distribution_repair_verifier as distribution_verifier
from tools import host_binding_generation_verifier
from tools import host_executable_binding as binding
from tools import host_process
from tools import host_registration_snapshot
from tools import static_composition_root
from tools.tests import host_template_test_support as template_support


PROJECT_ID = "9f7a31a0-0b63-4d4c-9f18-bd9a0d2e9c21"


@dataclass(frozen=True)
class HostAdapterFixture:
    project_root: Path
    request: bootstrap.ProjectOpenRequestV1
    plan: effective_session.EffectiveSessionPlan
    inspection: bootstrap.ProjectOpenInspectionV1
    static_composition: static_composition_root.StaticCompositionRootGeneration
    verified_binding: host_binding_generation_verifier.VerifiedHostBindingGeneration

    def observe(
        self,
        validators: contracts.ContractValidators,
    ) -> bootstrap.CurrentImageObservationV1:
        return bootstrap_current_host.observe_current_host_image(
            self.inspection,
            self.static_composition,
            self.verified_binding,
            validators,
        )


def _verified_binding(
    root: Path,
    static_composition: static_composition_root.StaticCompositionRootGeneration,
    validators: contracts.ContractValidators,
    artifact_bytes: bytes,
) -> host_binding_generation_verifier.VerifiedHostBindingGeneration:
    manifest = static_composition.manifest
    receipt = binding.create_host_executable_binding_receipt(
        inputs=binding.HostExecutableBindingInputs(
            static_composition=binding.GeneratedInputIdentity(
                manifest.generation_id,
                binding.IntegrityRecord(
                    manifest.integrity.algorithm,
                    manifest.integrity.digest,
                ),
            ),
            host_template=binding.GeneratedInputIdentity(
                "sha256-" + "b" * 64,
                binding.IntegrityRecord("sha256", "c" * 64),
            ),
            host_activation_blueprint_integrity=binding.IntegrityRecord(
                manifest.inputs.host_activation_blueprint_integrity.algorithm,
                manifest.inputs.host_activation_blueprint_integrity.digest,
            ),
        ),
        host=binding.HostIdentity(
            manifest.engine_generation_id,
            manifest.host_kind,
            manifest.target_platform,
        ),
        build=binding.HostBuildIdentity(
            manifest.configuration,
            binding.BuildGeneratorEvidence(
                manifest.generator_name,
                manifest.generator_multi_config,
            ),
            binding.ConfiguredCompilerEvidence(
                "CXX",
                manifest.compiler_id,
                manifest.compiler_version,
                None,
            ),
        ),
        target=binding.HostTargetEvidence(
            "asharia-generated-host",
            "EXECUTABLE",
            "asharia-generated-host.exe",
            "bin/Debug/asharia-generated-host.exe",
            2,
            6,
            1,
            0,
        ),
        artifact=binding.BoundFileEvidence(
            "host/asharia-generated-host.exe",
            binding.HOST_EXECUTABLE_MEDIA_TYPE,
            len(artifact_bytes),
            binding.IntegrityRecord("sha256", "d" * 64),
        ),
        registration_snapshot=binding.BoundFileEvidence(
            binding.HOST_REGISTRATION_SNAPSHOT_PATH,
            binding.HOST_REGISTRATION_SNAPSHOT_MEDIA_TYPE,
            2,
            binding.IntegrityRecord("sha256", "e" * 64),
        ),
    )
    diagnostics = binding.validate_host_executable_binding_receipt_data(
        receipt, validators
    )
    if diagnostics:
        raise AssertionError([value.render() for value in diagnostics])
    generation_path = root / "bindings" / receipt.binding_generation_id
    artifact_path = generation_path / receipt.artifact.path
    artifact_path.parent.mkdir(parents=True)
    artifact_path.write_bytes(artifact_bytes)
    snapshot = host_registration_snapshot.HostRegistrationSnapshot(
        manifest.generation_id,
        manifest.inputs.host_activation_blueprint_integrity.digest,
        (),
    )
    return host_binding_generation_verifier.VerifiedHostBindingGeneration(
        receipt,
        snapshot,
        generation_path,
    )


def make_fixture(
    root: Path,
    validators: contracts.ContractValidators,
) -> HostAdapterFixture:
    root = root.resolve()
    project_root = root / "project"
    project_root.mkdir()
    fingerprint = effective_session.SessionIntegrity("sha256", "6" * 64)
    engine_generation_id = "sha256-" + "a" * 64
    distribution = {
        "context": {
            "targetPlatform": "com.asharia.platform.windows",
            "configuration": "Debug",
        }
    }
    graph = effective_session.VerifiedResolvedGraph(
        distribution=distribution,
        project={},
        lock={},
        selected_candidates=(),
        engine_generation_id=engine_generation_id,
        distribution_manifest_integrity=fingerprint,
        project_manifest_integrity=fingerprint,
        locked_graph_integrity=fingerprint,
        candidate_bindings_integrity=fingerprint,
    )
    plan = effective_session.EffectiveSessionPlan(
        verified_graph=graph,
        host_profile_path="profiles/asset-worker/asharia.host-profile.json",
        host_profile={
            "hostKind": "asset-worker",
            "targetPlatform": "com.asharia.platform.windows",
        },
        host_profile_bytes=b"{}\n",
        host_profile_integrity=fingerprint,
        session_fingerprint=fingerprint,
    )
    session = effective_session.EffectiveSessionResult(
        effective_session.EffectiveSessionState.READY,
        plan,
        (),
    )
    inspection = bootstrap.ProjectOpenInspectionV1(
        package_snapshot=bootstrap.ProjectPackageSnapshotV1(
            project_root,
            {},
            b"{}\n",
            {},
            b"{}\n",
        ),
        effective_session=session,
        failure_owners=(),
        diagnostics=(),
    )
    static_composition = template_support.composition_generation(
        validators,
        provider_fixture=template_support.PROJECT_BOOTSTRAP_PROVIDER_FIXTURE,
        tool_provider_fixture=(
            template_support.RESTRICTED_SENTINEL_PROVIDER_FIXTURE
        ),
        session=plan,
    )
    verified_binding = _verified_binding(
        root,
        static_composition,
        validators,
        b"synthetic-host",
    )
    verified_distribution = distribution_verifier.VerifiedInstalledDistribution(
        engine_generation_id=engine_generation_id,
        generation_root=root / "distribution",
        manifest=distribution,
        manifest_bytes=b"{}\n",
        manifest_integrity={"algorithm": "sha256", "digest": "f" * 64},
    )
    request = bootstrap.ProjectOpenRequestV1(
        project_root,
        verified_distribution,
        effective_session.HostProfileSnapshot(
            plan.host_profile_path,
            plan.host_profile,
            plan.host_profile_bytes,
        ),
    )
    return HostAdapterFixture(
        project_root,
        request,
        plan,
        inspection,
        static_composition,
        verified_binding,
    )


def completed(
    return_code: int,
    stdout: bytes = b"",
    stderr: bytes = b"",
    limit_exceeded: str | None = None,
) -> host_process.BoundedHostProcessResult:
    return host_process.BoundedHostProcessResult(
        return_code,
        stdout,
        stderr,
        len(stdout),
        len(stderr),
        limit_exceeded,
    )


def summary(**changes: object) -> bytes:
    data: dict[str, object] = {
        "schema": "com.asharia.project-bootstrap-summary",
        "schemaVersion": 1,
        "projectName": "Bootstrap Project",
        "projectId": PROJECT_ID,
        "assetSourceRootCount": 2,
    }
    data.update(changes)
    return (json.dumps(data, ensure_ascii=False, indent=2) + "\n").encode(
        "utf-8"
    )


__all__ = [
    "HostAdapterFixture",
    "PROJECT_ID",
    "completed",
    "make_fixture",
    "summary",
]
