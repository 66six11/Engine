"""Cross-check generated static providers against observed Host registrations.

This is post-build identity evidence only. Canonical registration order is not
factory activation order and does not prove lifecycle execution.
"""

from __future__ import annotations

from dataclasses import dataclass, replace
from typing import Any, Iterable, Protocol, TypeVar

from tools import check_package_contracts as contracts
from tools import host_registration_snapshot as registration_snapshot
from tools import static_composition_root as composition_root


HOST_REGISTRATION_CROSS_VERIFICATION_NAME = (
    "host-registration-cross-verification"
)


@dataclass(frozen=True)
class VerifiedHostRegistrationSet:
    """Canonical owning registrations proven equal across both handoffs."""

    generation_id: str
    host_activation_blueprint_sha256: str
    registrations: tuple[
        registration_snapshot.StaticFactoryRegistration, ...
    ]


@dataclass(frozen=True)
class HostRegistrationCrossVerificationResult:
    """Atomic result: one complete verified set or stable diagnostics."""

    verified: VerifiedHostRegistrationSet | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.verified is not None and not self.diagnostics


RegistrationIdentity = tuple[str, str, str, str]


@dataclass(frozen=True)
class _ExpectedContribution:
    contribution_id: str
    kind: str


@dataclass(frozen=True)
class _ExpectedRegistration:
    package_id: str
    package_version: str
    module_id: str
    factory_id: str
    provider_entry_point: str
    contributions: tuple[_ExpectedContribution, ...]


class _RegistrationLike(Protocol):
    package_id: str
    package_version: str
    module_id: str
    factory_id: str
    provider_entry_point: str


_RegistrationValue = TypeVar("_RegistrationValue", bound=_RegistrationLike)


class _ContributionLike(Protocol):
    contribution_id: str
    kind: str


_ContributionValue = TypeVar("_ContributionValue", bound=_ContributionLike)


def _utf8_key(value: str) -> bytes:
    return value.encode("utf-8")


def _identity_key(
    value: _RegistrationLike,
) -> RegistrationIdentity:
    return (
        value.package_id,
        value.package_version,
        value.module_id,
        value.factory_id,
    )


def _identity_sort_key(
    value: RegistrationIdentity,
) -> tuple[bytes, bytes, bytes, bytes]:
    return (
        _utf8_key(value[0]),
        _utf8_key(value[1]),
        _utf8_key(value[2]),
        _utf8_key(value[3]),
    )


def _registration_sort_key(
    value: _RegistrationLike,
) -> tuple[bytes, bytes, bytes, bytes, bytes]:
    return (
        *_identity_sort_key(_identity_key(value)),
        _utf8_key(value.provider_entry_point),
    )


def _contribution_sort_key(
    value: _ContributionLike,
) -> tuple[bytes, bytes]:
    return (
        _utf8_key(value.contribution_id),
        _utf8_key(value.kind),
    )


def _observed_contribution_sort_key(
    value: registration_snapshot.StaticContributionRegistration,
) -> tuple[bytes, bytes, bytes]:
    return (
        *_contribution_sort_key(value),
        _utf8_key(value.cardinality),
    )


def _canonical_observed_registration(
    value: registration_snapshot.StaticFactoryRegistration,
) -> registration_snapshot.StaticFactoryRegistration:
    return replace(
        value,
        contributions=tuple(
            sorted(
                value.contributions,
                key=_observed_contribution_sort_key,
            )
        ),
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
        manifest_path=HOST_REGISTRATION_CROSS_VERIFICATION_NAME,
        pointer=pointer,
        message=message,
    )


def _failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> HostRegistrationCrossVerificationResult:
    unique = {
        (value.manifest_path, value.pointer, value.code, value.message): value
        for value in diagnostics
    }
    return HostRegistrationCrossVerificationResult(
        verified=None,
        diagnostics=tuple(sorted(unique.values(), key=_diagnostic_sort_key)),
    )


def _identity_text(identity: RegistrationIdentity) -> str:
    package_id, package_version, module_id, factory_id = identity
    return (
        f"package='{package_id}' version='{package_version}' "
        f"module='{module_id}' factory='{factory_id}'"
    )


def _expected_registrations(
    manifest: composition_root.StaticCompositionRootManifest,
) -> tuple[_ExpectedRegistration, ...]:
    values = (
        _ExpectedRegistration(
            package_id=provider.package_id,
            package_version=provider.package_version,
            module_id=provider.module_id,
            factory_id=factory.factory_id,
            provider_entry_point=provider.entry_point.function,
            contributions=tuple(
                sorted(
                    (
                        _ExpectedContribution(
                            contribution_id=contribution.contribution_id,
                            kind=contribution.contribution_kind,
                        )
                        for contribution in factory.contributions
                    ),
                    key=_contribution_sort_key,
                )
            ),
        )
        for provider in manifest.providers
        for factory in provider.factories
    )
    return tuple(sorted(values, key=_registration_sort_key))


def _group_by_identity(
    values: tuple[_RegistrationValue, ...],
) -> dict[RegistrationIdentity, tuple[_RegistrationValue, ...]]:
    grouped: dict[RegistrationIdentity, list[_RegistrationValue]] = {}
    for value in values:
        grouped.setdefault(_identity_key(value), []).append(value)
    return {
        identity: tuple(sorted(group, key=_registration_sort_key))
        for identity, group in grouped.items()
    }


def _group_contributions_by_id(
    values: tuple[_ContributionValue, ...],
) -> dict[str, tuple[_ContributionValue, ...]]:
    grouped: dict[str, list[_ContributionValue]] = {}
    for value in values:
        grouped.setdefault(value.contribution_id, []).append(value)
    return {
        contribution_id: tuple(sorted(group, key=_contribution_sort_key))
        for contribution_id, group in grouped.items()
    }


def _validate_composition(
    value: Any,
    validators: contracts.ContractValidators,
) -> tuple[
    composition_root.StaticCompositionRootManifest | None,
    list[contracts.Diagnostic],
]:
    if isinstance(value, composition_root.StaticCompositionRootGeneration):
        return value.manifest, (
            composition_root.validate_static_composition_root_generation(
                value, validators
            )
        )
    if isinstance(value, composition_root.StaticCompositionRootManifest):
        return (
            value,
            composition_root.validate_static_composition_root_manifest_data(
                value, validators
            ),
        )
    return None, [
        _diagnostic(
            "host-binding.registration.composition-invalid",
            "/composition",
            "registration cross-check requires one verified "
            "static-composition manifest or generation",
        )
    ]


def verify_host_registration_cross_binding(
    composition: Any,
    snapshot: Any,
    validators: contracts.ContractValidators,
) -> HostRegistrationCrossVerificationResult:
    """Prove exact equality of expected factories and selected contributions."""

    manifest, diagnostics = _validate_composition(composition, validators)
    if not isinstance(snapshot, registration_snapshot.HostRegistrationSnapshot):
        diagnostics.append(
            _diagnostic(
                "host-binding.registration.snapshot-invalid",
                "/snapshot",
                "registration cross-check requires one verified "
                "Host registration snapshot",
            )
        )
        return _failure(diagnostics)
    diagnostics.extend(
        contracts.validate_manifest_data(
            registration_snapshot.host_registration_snapshot_to_data(snapshot),
            registration_snapshot.HOST_REGISTRATION_SNAPSHOT_NAME,
            validators,
        )
    )
    diagnostics.extend(
        registration_snapshot.validate_contribution_cardinality_consistency(
            snapshot
        )
    )
    if manifest is None:
        return _failure(diagnostics)

    if snapshot.generation_id != manifest.generation_id:
        diagnostics.append(
            _diagnostic(
                "host-binding.registration.generation-mismatch",
                "/snapshot/generationId",
                "observed registrations do not belong to the "
                "static-composition generation",
            )
        )
    blueprint_sha256 = (
        manifest.inputs.host_activation_blueprint_integrity.digest
    )
    if snapshot.host_activation_blueprint_sha256 != blueprint_sha256:
        diagnostics.append(
            _diagnostic(
                "host-binding.registration.blueprint-mismatch",
                "/snapshot/hostActivationBlueprintSha256",
                "observed registrations do not belong to the expected "
                "Host Activation Blueprint",
            )
        )

    expected = _expected_registrations(manifest)
    observed = tuple(
        sorted(
            (
                _canonical_observed_registration(value)
                for value in snapshot.registrations
            ),
            key=_registration_sort_key,
        )
    )
    expected_by_identity = _group_by_identity(expected)
    observed_by_identity = _group_by_identity(observed)
    all_identities = sorted(
        expected_by_identity.keys() | observed_by_identity.keys(),
        key=_identity_sort_key,
    )
    for identity in all_identities:
        expected_values = expected_by_identity.get(identity, ())
        observed_values = observed_by_identity.get(identity, ())
        identity_text = _identity_text(identity)
        if len(expected_values) > 1:
            diagnostics.append(
                _diagnostic(
                    "host-binding.registration.expected-duplicate",
                    "/composition/providers",
                    f"expected registration identity is duplicated: {identity_text}",
                )
            )
        if len(observed_values) > 1:
            diagnostics.append(
                _diagnostic(
                    "host-binding.registration.observed-duplicate",
                    "/snapshot/registrations",
                    f"observed registration identity is duplicated: {identity_text}",
                )
            )
        # Duplicate ownership is ambiguous; do not guess which provider to pair.
        if len(expected_values) > 1 or len(observed_values) > 1:
            continue
        if not observed_values:
            diagnostics.append(
                _diagnostic(
                    "host-binding.registration.missing",
                    "/snapshot/registrations",
                    f"expected registration was not observed: {identity_text}",
                )
            )
            continue
        if not expected_values:
            diagnostics.append(
                _diagnostic(
                    "host-binding.registration.extra",
                    "/snapshot/registrations",
                    f"unexpected registration was observed: {identity_text}",
                )
            )
            continue
        expected_provider = expected_values[0].provider_entry_point
        observed_provider = observed_values[0].provider_entry_point
        if observed_provider != expected_provider:
            diagnostics.append(
                _diagnostic(
                    "host-binding.registration.provider-mismatch",
                    "/snapshot/registrations",
                    f"registration provider mismatch for {identity_text}: "
                    f"expected='{expected_provider}' observed='{observed_provider}'",
                )
            )

        expected_contributions = expected_values[0].contributions
        observed_contributions = observed_values[0].contributions
        expected_by_contribution_id = _group_contributions_by_id(
            expected_contributions
        )
        observed_by_contribution_id = _group_contributions_by_id(
            observed_contributions
        )
        all_contribution_ids = sorted(
            (
                expected_by_contribution_id.keys()
                | observed_by_contribution_id.keys()
            ),
            key=_utf8_key,
        )
        for contribution_id in all_contribution_ids:
            expected_contribution_values = (
                expected_by_contribution_id.get(contribution_id, ())
            )
            observed_contribution_values = (
                observed_by_contribution_id.get(contribution_id, ())
            )
            contribution_text = (
                f"{identity_text} contribution='{contribution_id}'"
            )
            if len(expected_contribution_values) > 1:
                diagnostics.append(
                    _diagnostic(
                        (
                            "host-binding.registration."
                            "expected-contribution-duplicate"
                        ),
                        "/composition/providers",
                        (
                            "expected contribution identity is duplicated: "
                            f"{contribution_text}"
                        ),
                    )
                )
            if len(observed_contribution_values) > 1:
                diagnostics.append(
                    _diagnostic(
                        (
                            "host-binding.registration."
                            "observed-contribution-duplicate"
                        ),
                        "/snapshot/registrations",
                        (
                            "observed contribution identity is duplicated: "
                            f"{contribution_text}"
                        ),
                    )
                )
            if (
                len(expected_contribution_values) > 1
                or len(observed_contribution_values) > 1
            ):
                continue
            if not observed_contribution_values:
                diagnostics.append(
                    _diagnostic(
                        "host-binding.registration.contribution-missing",
                        "/snapshot/registrations",
                        (
                            "expected contribution was not observed: "
                            f"{contribution_text}"
                        ),
                    )
                )
                continue
            if not expected_contribution_values:
                diagnostics.append(
                    _diagnostic(
                        "host-binding.registration.contribution-extra",
                        "/snapshot/registrations",
                        (
                            "unexpected contribution was observed: "
                            f"{contribution_text}"
                        ),
                    )
                )
                continue
            expected_kind = expected_contribution_values[0].kind
            observed_kind = observed_contribution_values[0].kind
            if observed_kind != expected_kind:
                diagnostics.append(
                    _diagnostic(
                        "host-binding.registration.contribution-kind-mismatch",
                        "/snapshot/registrations",
                        (
                            "contribution kind mismatch for "
                            f"{contribution_text}: expected='{expected_kind}' "
                            f"observed='{observed_kind}'"
                        ),
                    )
                )

    if diagnostics:
        return _failure(diagnostics)
    return HostRegistrationCrossVerificationResult(
        verified=VerifiedHostRegistrationSet(
            generation_id=manifest.generation_id,
            host_activation_blueprint_sha256=blueprint_sha256,
            registrations=observed,
        ),
        diagnostics=(),
    )
