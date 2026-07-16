"""Canonical handoff for one static Host factory-registration snapshot."""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from typing import Any, Iterable

from tools import check_package_contracts as contracts


HOST_REGISTRATION_SNAPSHOT_NAME = "asharia.static-factory-registration-snapshot.json"
HOST_REGISTRATION_SNAPSHOT_SCHEMA = "com.asharia.static-factory-registration-snapshot"
HOST_REGISTRATION_SNAPSHOT_SCHEMA_VERSION = 2

_GENERATION_ID_PATTERN = re.compile(r"^sha256-[0-9a-f]{64}$")
_SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


@dataclass(frozen=True, order=True)
class StaticContributionRegistration:
    """One typed contribution identity observed from a static factory."""

    contribution_id: str
    kind: str
    cardinality: str


@dataclass(frozen=True, order=True)
class StaticFactoryRegistration:
    """One owning factory and its typed contribution registrations."""

    package_id: str
    package_version: str
    module_id: str
    factory_id: str
    provider_entry_point: str
    contributions: tuple[StaticContributionRegistration, ...]


@dataclass(frozen=True)
class HostRegistrationSnapshot:
    """Canonical owning registration evidence from one generated Host."""

    generation_id: str
    host_activation_blueprint_sha256: str
    registrations: tuple[StaticFactoryRegistration, ...]


@dataclass(frozen=True)
class HostRegistrationSnapshotResult:
    """Atomic parse result: either one complete snapshot or diagnostics."""

    snapshot: HostRegistrationSnapshot | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.snapshot is not None and not self.diagnostics


def _utf8_key(value: str) -> bytes:
    return value.encode("utf-8")


def _registration_key(
    registration: StaticFactoryRegistration,
) -> tuple[bytes, bytes, bytes, bytes, bytes]:
    return (
        _utf8_key(registration.package_id),
        _utf8_key(registration.package_version),
        _utf8_key(registration.module_id),
        _utf8_key(registration.factory_id),
        _utf8_key(registration.provider_entry_point),
    )


def _contribution_key(
    contribution: StaticContributionRegistration,
) -> tuple[bytes, bytes, bytes]:
    return (
        _utf8_key(contribution.contribution_id),
        _utf8_key(contribution.kind),
        _utf8_key(contribution.cardinality),
    )


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
        manifest_path=HOST_REGISTRATION_SNAPSHOT_NAME,
        pointer=pointer,
        message=message,
    )


def _failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> HostRegistrationSnapshotResult:
    return HostRegistrationSnapshotResult(
        snapshot=None,
        diagnostics=tuple(sorted(diagnostics, key=_diagnostic_sort_key)),
    )


def validate_contribution_cardinality_consistency(
    snapshot: HostRegistrationSnapshot,
) -> list[contracts.Diagnostic]:
    """Require one stable cardinality for each logical contribution kind."""

    diagnostics: list[contracts.Diagnostic] = []
    cardinality_by_kind: dict[str, str] = {}
    observed_contribution_ids: set[tuple[str, str]] = set()
    for registration_index, registration in enumerate(snapshot.registrations):
        for contribution_index, contribution in enumerate(
            registration.contributions
        ):
            contribution_key = (
                registration.package_id,
                contribution.contribution_id,
            )
            if contribution_key in observed_contribution_ids:
                continue
            observed_contribution_ids.add(contribution_key)
            previous = cardinality_by_kind.get(contribution.kind)
            if previous is not None and previous != contribution.cardinality:
                diagnostics.append(
                    _diagnostic(
                        "host.registration.contribution-cardinality-conflict",
                        (
                            f"/registrations/{registration_index}/contributions/"
                            f"{contribution_index}/cardinality"
                        ),
                        "one contribution kind declares conflicting cardinalities",
                    )
                )
            else:
                cardinality_by_kind[contribution.kind] = contribution.cardinality
    return sorted(diagnostics, key=_diagnostic_sort_key)


def _contribution_data(
    contribution: StaticContributionRegistration,
) -> dict[str, str]:
    return {
        "id": contribution.contribution_id,
        "kind": contribution.kind,
        "cardinality": contribution.cardinality,
    }


def _registration_data(
    registration: StaticFactoryRegistration,
) -> dict[str, Any]:
    return {
        "packageId": registration.package_id,
        "packageVersion": registration.package_version,
        "moduleId": registration.module_id,
        "factoryId": registration.factory_id,
        "providerEntryPoint": registration.provider_entry_point,
        "contributions": [
            _contribution_data(contribution)
            for contribution in sorted(
                registration.contributions,
                key=_contribution_key,
            )
        ],
    }


def host_registration_snapshot_to_data(
    snapshot: HostRegistrationSnapshot,
) -> dict[str, Any]:
    """Return the fixed-field-order JSON-compatible snapshot representation."""

    return {
        "schema": HOST_REGISTRATION_SNAPSHOT_SCHEMA,
        "schemaVersion": HOST_REGISTRATION_SNAPSHOT_SCHEMA_VERSION,
        "generationId": snapshot.generation_id,
        "hostActivationBlueprintSha256": (
            snapshot.host_activation_blueprint_sha256
        ),
        "registrations": [
            _registration_data(registration)
            for registration in sorted(
                snapshot.registrations,
                key=_registration_key,
            )
        ],
    }


def render_host_registration_snapshot(snapshot: HostRegistrationSnapshot) -> str:
    """Render canonical fixed-order JSON with LF and one final newline."""

    return json.dumps(
        host_registration_snapshot_to_data(snapshot),
        ensure_ascii=False,
        indent=2,
    ) + "\n"


def _contribution_from_data(
    value: dict[str, str],
) -> StaticContributionRegistration:
    return StaticContributionRegistration(
        contribution_id=value["id"],
        kind=value["kind"],
        cardinality=value["cardinality"],
    )


def _registration_from_data(value: dict[str, Any]) -> StaticFactoryRegistration:
    return StaticFactoryRegistration(
        package_id=value["packageId"],
        package_version=value["packageVersion"],
        module_id=value["moduleId"],
        factory_id=value["factoryId"],
        provider_entry_point=value["providerEntryPoint"],
        contributions=tuple(
            _contribution_from_data(contribution)
            for contribution in value["contributions"]
        ),
    )


def parse_host_registration_snapshot_bytes(
    content: Any,
    validators: contracts.ContractValidators,
    *,
    expected_generation_id: Any,
    expected_host_activation_blueprint_sha256: Any,
) -> HostRegistrationSnapshotResult:
    """Parse, canonicalize, and exactly bind one Host verification response."""

    diagnostics: list[contracts.Diagnostic] = []
    if (
        not isinstance(expected_generation_id, str)
        or _GENERATION_ID_PATTERN.fullmatch(expected_generation_id) is None
    ):
        diagnostics.append(
            _diagnostic(
                "host.registration.expected-generation-invalid",
                "/generationId",
                "expected generationId must be one lowercase SHA-256 generation ID",
            )
        )
    if (
        not isinstance(expected_host_activation_blueprint_sha256, str)
        or _SHA256_PATTERN.fullmatch(expected_host_activation_blueprint_sha256)
        is None
    ):
        diagnostics.append(
            _diagnostic(
                "host.registration.expected-blueprint-invalid",
                "/hostActivationBlueprintSha256",
                "expected Host Activation Blueprint digest must be lowercase SHA-256",
            )
        )
    if not isinstance(content, bytes):
        diagnostics.append(
            _diagnostic(
                "host.registration.bytes-required",
                "",
                "registration snapshot input must be exact bytes",
            )
        )
    if diagnostics:
        return _failure(diagnostics)
    assert isinstance(content, bytes)
    assert isinstance(expected_generation_id, str)
    assert isinstance(expected_host_activation_blueprint_sha256, str)

    try:
        text = content.decode("utf-8")
    except UnicodeDecodeError:
        return _failure(
            [
                _diagnostic(
                    "host.registration.utf8-invalid",
                    "",
                    "registration snapshot is not valid UTF-8",
                )
            ]
        )
    try:
        data = json.loads(text)
    except json.JSONDecodeError:
        return _failure(
            [
                _diagnostic(
                    "host.registration.json-invalid",
                    "",
                    "registration snapshot is not one complete JSON value",
                )
            ]
        )

    schema_diagnostics = contracts.validate_manifest_data(
        data,
        HOST_REGISTRATION_SNAPSHOT_NAME,
        validators,
    )
    if schema_diagnostics:
        return _failure(schema_diagnostics)
    assert isinstance(data, dict)

    registrations = tuple(
        _registration_from_data(value) for value in data["registrations"]
    )
    canonical_registrations = tuple(sorted(registrations, key=_registration_key))
    if registrations != canonical_registrations:
        return _failure(
            [
                _diagnostic(
                    "host.registration.order-invalid",
                    "/registrations",
                    "registrations must use canonical UTF-8 byte order",
                )
            ]
        )
    contribution_order_diagnostics: list[contracts.Diagnostic] = []
    for index, registration in enumerate(registrations):
        canonical_contributions = tuple(
            sorted(registration.contributions, key=_contribution_key)
        )
        if registration.contributions != canonical_contributions:
            contribution_order_diagnostics.append(
                _diagnostic(
                    "host.registration.contribution-order-invalid",
                    f"/registrations/{index}/contributions",
                    "contributions must use canonical UTF-8 byte order",
                )
            )
    if contribution_order_diagnostics:
        return _failure(contribution_order_diagnostics)

    snapshot = HostRegistrationSnapshot(
        generation_id=data["generationId"],
        host_activation_blueprint_sha256=data[
            "hostActivationBlueprintSha256"
        ],
        registrations=registrations,
    )
    cardinality_diagnostics = validate_contribution_cardinality_consistency(
        snapshot
    )
    if cardinality_diagnostics:
        return _failure(cardinality_diagnostics)
    # Exact comparison also freezes field order, whitespace, LF, and the final newline.
    if render_host_registration_snapshot(snapshot).encode("utf-8") != content:
        return _failure(
            [
                _diagnostic(
                    "host.registration.canonical-bytes-mismatch",
                    "",
                    "registration snapshot bytes are not in canonical form",
                )
            ]
        )

    if snapshot.generation_id != expected_generation_id:
        diagnostics.append(
            _diagnostic(
                "host.registration.generation-mismatch",
                "/generationId",
                "registration snapshot does not match the expected generation",
            )
        )
    if (
        snapshot.host_activation_blueprint_sha256
        != expected_host_activation_blueprint_sha256
    ):
        diagnostics.append(
            _diagnostic(
                "host.registration.blueprint-mismatch",
                "/hostActivationBlueprintSha256",
                "registration snapshot does not match the expected Host Activation Blueprint",
            )
        )
    if diagnostics:
        return _failure(diagnostics)
    return HostRegistrationSnapshotResult(snapshot=snapshot, diagnostics=())
