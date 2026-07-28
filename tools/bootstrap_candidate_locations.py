"""Lock-derived package candidate discovery for Bootstrap project open."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from tools import bootstrap_session
from tools import check_package_contracts as contracts
from tools import engine_distribution_repair_verifier as distribution_verifier
from tools import package_candidate_discovery as discovery
from tools.package_candidates import PackageCandidate


@dataclass(frozen=True)
class BootstrapCandidateDiscoveryResultV1:
    """Atomic candidates or stable failure ownership for project open."""

    candidates: tuple[PackageCandidate, ...]
    failure_owners: tuple[
        bootstrap_session.InspectionFailureOwnerV1, ...
    ]
    diagnostics: tuple[bootstrap_session.BootstrapSessionDiagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return not self.failure_owners and not self.diagnostics


def _diagnostic(
    code: str,
    manifest_path: str,
    pointer: str,
    message: str,
) -> bootstrap_session.BootstrapSessionDiagnostic:
    return bootstrap_session.BootstrapSessionDiagnostic(
        code,
        manifest_path,
        pointer,
        message,
    )


def _ordered_diagnostics(
    values: Iterable[bootstrap_session.BootstrapSessionDiagnostic],
) -> tuple[bootstrap_session.BootstrapSessionDiagnostic, ...]:
    unique = {
        (value.manifest_path, value.pointer, value.code, value.message): value
        for value in values
    }
    return tuple(
        sorted(
            unique.values(),
            key=lambda value: (
                value.manifest_path,
                value.pointer,
                value.code,
                value.message,
            ),
        )
    )


def _result(
    *,
    candidates: Iterable[PackageCandidate] = (),
    owners: Iterable[bootstrap_session.InspectionFailureOwnerV1] = (),
    diagnostics: Iterable[bootstrap_session.BootstrapSessionDiagnostic] = (),
) -> BootstrapCandidateDiscoveryResultV1:
    return BootstrapCandidateDiscoveryResultV1(
        candidates=tuple(candidates),
        failure_owners=tuple(
            sorted(set(owners), key=lambda value: value.value)
        ),
        diagnostics=_ordered_diagnostics(diagnostics),
    )


def _local_source_map(
    local_sources: Iterable[object],
) -> tuple[
    dict[str, discovery.LocalCandidateLocation],
    list[bootstrap_session.BootstrapSessionDiagnostic],
]:
    sources: dict[str, discovery.LocalCandidateLocation] = {}
    diagnostics: list[bootstrap_session.BootstrapSessionDiagnostic] = []
    try:
        values = tuple(local_sources)
    except TypeError:
        values = ()
        diagnostics.append(
            _diagnostic(
                "bootstrap.project.local-sources-invalid",
                contracts.PACKAGE_LOCK_NAME,
                "/candidateSources",
                "local source mappings must be iterable",
            )
        )
    for index, value in enumerate(values):
        pointer = f"/candidateSources/{index}"
        if not isinstance(value, discovery.LocalCandidateLocation):
            diagnostics.append(
                _diagnostic(
                    "bootstrap.project.local-source-invalid",
                    contracts.PACKAGE_LOCK_NAME,
                    pointer,
                    "local source mapping has an unsupported type",
                )
            )
            continue
        if not isinstance(value.source_id, str) or not isinstance(
            value.payload_root, Path
        ):
            diagnostics.append(
                _diagnostic(
                    "bootstrap.project.local-source-invalid",
                    contracts.PACKAGE_LOCK_NAME,
                    pointer,
                    (
                        "local source mapping must contain a string ID and "
                        "pathlib.Path root"
                    ),
                )
            )
            continue
        if value.source_id in sources:
            diagnostics.append(
                _diagnostic(
                    "bootstrap.project.local-source-duplicate",
                    contracts.PACKAGE_LOCK_NAME,
                    pointer,
                    f"local source ID '{value.source_id}' is mapped more than once",
                )
            )
            continue
        sources[value.source_id] = value
    return sources, diagnostics


def _derive_locations(
    project_root: Path,
    verified_distribution: distribution_verifier.VerifiedInstalledDistribution,
    lock: dict[str, Any],
    local_sources: Iterable[object],
) -> tuple[
    tuple[discovery.CandidateLocation, ...],
    tuple[bootstrap_session.InspectionFailureOwnerV1, ...],
    tuple[bootstrap_session.BootstrapSessionDiagnostic, ...],
]:
    mapped_sources, diagnostics = _local_source_map(local_sources)
    owners: set[bootstrap_session.InspectionFailureOwnerV1] = set()
    if diagnostics:
        owners.add(bootstrap_session.InspectionFailureOwnerV1.PROJECT)
    locations: list[discovery.CandidateLocation] = []
    inventory = verified_distribution.manifest["bundledPackages"]

    for index, node in enumerate(lock["nodes"]):
        source = node["source"]
        kind = source["kind"]
        pointer = f"/nodes/{index}/source"
        if kind == "engine-distribution":
            matches = [
                package
                for package in inventory
                if package["id"] == node["id"]
                and package["version"] == node["version"]
                and package["packageKind"] == node["packageKind"]
            ]
            if len(matches) != 1:
                owners.add(
                    bootstrap_session.InspectionFailureOwnerV1.DISTRIBUTION
                )
                diagnostics.append(
                    _diagnostic(
                        "bootstrap.distribution.package-inventory-mismatch",
                        contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME,
                        "/bundledPackages",
                        (
                            f"locked package '{node['id']}@{node['version']}' must "
                            "match exactly one bundled Distribution package"
                        ),
                    )
                )
                continue
            locations.append(
                discovery.EngineDistributedCandidateLocation(
                    verified_distribution.generation_root,
                    matches[0]["root"],
                )
            )
        elif kind == "project-embedded":
            locations.append(
                discovery.ProjectEmbeddedCandidateLocation(
                    project_root,
                    source["relativePath"],
                )
            )
        elif kind == "local":
            source_id = source["sourceId"]
            mapped = mapped_sources.get(source_id)
            if mapped is None:
                owners.add(bootstrap_session.InspectionFailureOwnerV1.PROJECT)
                diagnostics.append(
                    _diagnostic(
                        "bootstrap.project.local-source-unmapped",
                        contracts.PACKAGE_LOCK_NAME,
                        f"{pointer}/sourceId",
                        f"locked local source '{source_id}' has no explicit mapping",
                    )
                )
                continue
            locations.append(mapped)
    return (
        tuple(locations),
        tuple(sorted(owners, key=lambda value: value.value)),
        _ordered_diagnostics(diagnostics),
    )


def _pointer_token(value: str) -> str:
    return value.replace("~", "~0").replace("/", "~1")


def _public_discovery_diagnostic(
    value: discovery.CandidateDiscoveryDiagnostic,
) -> bootstrap_session.BootstrapSessionDiagnostic:
    pointer = f"/candidateSources/{_pointer_token(value.source_key)}"
    if value.location:
        pointer += f"/{_pointer_token(value.location)}"
    return _diagnostic(
        value.code,
        (
            contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME
            if value.source_key.startswith("engine-distribution:")
            else contracts.PACKAGE_LOCK_NAME
        ),
        pointer,
        value.message,
    )


def discover_bootstrap_candidates(
    project_root: Path,
    verified_distribution: distribution_verifier.VerifiedInstalledDistribution,
    lock: dict[str, Any],
    local_sources: Iterable[object],
    validators: contracts.ContractValidators,
) -> BootstrapCandidateDiscoveryResultV1:
    """Discover only candidates named by one already-validated lock."""

    locations, owners, diagnostics = _derive_locations(
        project_root,
        verified_distribution,
        lock,
        local_sources,
    )
    if diagnostics:
        return _result(owners=owners, diagnostics=diagnostics)

    discovered = discovery.load_package_candidates(locations, validators)
    if discovered.succeeded:
        return _result(candidates=discovered.candidates)
    return _result(
        owners=(
            bootstrap_session.InspectionFailureOwnerV1.DISTRIBUTION
            if value.source_key.startswith("engine-distribution:")
            else bootstrap_session.InspectionFailureOwnerV1.PROJECT
            for value in discovered.diagnostics
        ),
        diagnostics=(
            _public_discovery_diagnostic(value)
            for value in discovered.diagnostics
        ),
    )


__all__ = [
    "BootstrapCandidateDiscoveryResultV1",
    "discover_bootstrap_candidates",
]
