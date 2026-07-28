"""Read-only catalog projection from one verified Engine Distribution.

The Engine Distribution Manifest remains the only persistent bundled-package
inventory.  This adapter captures that verified handoff, asks the existing
strict loader for fresh package evidence, and returns only an in-memory
snapshot suitable for deterministic resolution.
"""

from __future__ import annotations

import copy
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import engine_distribution_repair_verifier as distribution_verifier
from tools import package_candidate_discovery as discovery
from tools.package_candidates import PackageCandidate


_GENERATION_ID_PATTERN = re.compile(r"^sha256-[0-9a-f]{64}$")
_WINDOWS_ABSOLUTE_PATH_PATTERN = re.compile(
    r"(?:[A-Za-z]:[\\/]|\\\\(?:\?\\)?[^\\\s]+[\\/])"
)
_POSIX_ABSOLUTE_PATH_PATTERN = re.compile(
    r"(?:^|[\s'\"(])/(?:[^/\s'\")]+/)*[^/\s'\")]+"
)


@dataclass(frozen=True)
class EngineDistributionPackageCatalogEntry:
    """Immutable logical metadata for one bundled catalog candidate."""

    identity: str
    version: str
    package_kind: str
    availability: str
    logical_root: str


@dataclass(frozen=True)
class EngineDistributionPackageCatalogSnapshot:
    """Process-local catalog detached from the mutable verified handoff."""

    engine_generation_id: str
    entries: tuple[EngineDistributionPackageCatalogEntry, ...]
    _distribution_manifest_bytes: bytes = field(repr=False)
    _candidate_snapshot: tuple[PackageCandidate, ...] = field(repr=False)

    @property
    def candidates(self) -> tuple[PackageCandidate, ...]:
        """Return an isolated copy of the exact candidates captured for this catalog."""

        return copy.deepcopy(self._candidate_snapshot)

    @property
    def distribution_manifest(self) -> dict[str, Any]:
        """Return an isolated copy of the captured canonical Distribution Manifest."""

        value = json.loads(self._distribution_manifest_bytes.decode("utf-8"))
        assert isinstance(value, dict)
        return value


@dataclass(frozen=True)
class EngineDistributionPackageCatalogDiagnostic:
    """Stable catalog failure containing only logical Distribution context."""

    code: str
    message: str
    engine_generation_id: str = "<invalid-generation>"
    identity: str = ""
    logical_root: str = ""
    location: str = ""

    def render(self) -> str:
        context = f"engine-distribution:{self.engine_generation_id}"
        if self.logical_root:
            context += f"/{self.logical_root}"
        if self.identity:
            context += f" ({self.identity})"
        if self.location:
            context += f"/{self.location.lstrip('/')}"
        return f"{context}: [{self.code}] {self.message}"


@dataclass(frozen=True)
class EngineDistributionPackageCatalogResult:
    """Atomic result: one complete snapshot or deterministic diagnostics."""

    snapshot: EngineDistributionPackageCatalogSnapshot | None
    diagnostics: tuple[EngineDistributionPackageCatalogDiagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.snapshot is not None and not self.diagnostics


@dataclass(frozen=True)
class _CapturedHandoff:
    engine_generation_id: str
    generation_root: Path
    manifest: dict[str, Any]
    manifest_bytes: bytes


def _utf8_key(value: str) -> bytes:
    return value.encode("utf-8", errors="surrogatepass")


def _diagnostic_sort_key(
    value: EngineDistributionPackageCatalogDiagnostic,
) -> tuple[bytes, bytes, bytes, str, str, str]:
    return (
        _utf8_key(value.engine_generation_id),
        _utf8_key(value.logical_root),
        _utf8_key(value.identity),
        value.location,
        value.code,
        value.message,
    )


def _ordered_diagnostics(
    values: Iterable[EngineDistributionPackageCatalogDiagnostic],
) -> tuple[EngineDistributionPackageCatalogDiagnostic, ...]:
    unique = {
        (
            value.engine_generation_id,
            value.logical_root,
            value.identity,
            value.location,
            value.code,
            value.message,
        ): value
        for value in values
    }
    return tuple(sorted(unique.values(), key=_diagnostic_sort_key))


def _failure(
    diagnostics: Iterable[EngineDistributionPackageCatalogDiagnostic],
) -> EngineDistributionPackageCatalogResult:
    return EngineDistributionPackageCatalogResult(
        snapshot=None,
        diagnostics=_ordered_diagnostics(diagnostics),
    )


def _handoff_diagnostic(
    code: str,
    message: str,
    generation_id: object,
    *,
    location: str = "",
) -> EngineDistributionPackageCatalogDiagnostic:
    del generation_id
    return EngineDistributionPackageCatalogDiagnostic(
        code=code,
        message=message,
        # No handoff field is trusted until every captured evidence binding passes.
        engine_generation_id="<invalid-generation>",
        location=location,
    )


def _handoff_contract_code(value: contracts.Diagnostic) -> str:
    if value.code == "distribution.package.duplicate-id":
        return "catalog.handoff.inventory.duplicate-identity-version"
    if value.code == "distribution.package.multiple-versions":
        return "catalog.handoff.inventory.multiple-versions"
    if (
        value.pointer.startswith("/bundledPackages/")
        and value.pointer.endswith("/root")
        and value.code.startswith("distribution.path.")
    ):
        return "catalog.handoff.inventory.root-collision"
    return "catalog.handoff.manifest-contract-invalid"


def _capture_verified_handoff(
    verified_distribution: object,
    validators: contracts.ContractValidators,
) -> tuple[
    _CapturedHandoff | None,
    tuple[EngineDistributionPackageCatalogDiagnostic, ...],
]:
    if not isinstance(
        verified_distribution,
        distribution_verifier.VerifiedInstalledDistribution,
    ):
        return None, (
            _handoff_diagnostic(
                "catalog.handoff.invalid",
                "catalog input must be a VerifiedInstalledDistribution handoff",
                None,
            ),
        )

    generation_id = verified_distribution.engine_generation_id
    diagnostics: list[EngineDistributionPackageCatalogDiagnostic] = []
    generation_root = verified_distribution.generation_root
    root_is_absolute_path = isinstance(generation_root, Path) and generation_root.is_absolute()
    if not root_is_absolute_path:
        diagnostics.append(
            _handoff_diagnostic(
                "catalog.handoff.generation-root-invalid",
                "verified handoff must contain an absolute pathlib.Path generation root",
                generation_id,
                location="generationRoot",
            )
        )
    else:
        inspected_root, root_finding = (
            distribution_verifier.inspect_installed_engine_distribution_root(
                generation_root
            )
        )
        if root_finding is not None or inspected_root is None:
            diagnostics.append(
                _handoff_diagnostic(
                    "catalog.handoff.generation-root-invalid",
                    (
                        "verified handoff generation root no longer satisfies "
                        "the installed-root contract"
                    ),
                    generation_id,
                    location="generationRoot",
                )
            )

    if not isinstance(generation_id, str) or not _GENERATION_ID_PATTERN.fullmatch(
        generation_id
    ):
        diagnostics.append(
            _handoff_diagnostic(
                "catalog.handoff.generation-id-invalid",
                "verified handoff contains an invalid EngineGenerationId",
                generation_id,
                location="engineGenerationId",
            )
        )
    elif root_is_absolute_path and generation_root.name != generation_id:
        diagnostics.append(
            _handoff_diagnostic(
                "catalog.handoff.generation-root-id-mismatch",
                "verified handoff generation root basename differs from EngineGenerationId",
                generation_id,
                location="generationRoot",
            )
        )

    try:
        manifest = copy.deepcopy(verified_distribution.manifest)
        manifest_integrity = copy.deepcopy(
            verified_distribution.manifest_integrity
        )
    except (MemoryError, RecursionError, RuntimeError, TypeError, ValueError):
        diagnostics.append(
            _handoff_diagnostic(
                "catalog.handoff.capture-failed",
                "verified handoff evidence could not be captured atomically",
                generation_id,
            )
        )
        return None, _ordered_diagnostics(diagnostics)

    raw_manifest_bytes = verified_distribution.manifest_bytes
    if type(raw_manifest_bytes) is not bytes:
        diagnostics.append(
            _handoff_diagnostic(
                "catalog.handoff.manifest-bytes-invalid",
                "verified handoff must contain exact Distribution Manifest bytes",
                generation_id,
                location="manifestBytes",
            )
        )
        return None, _ordered_diagnostics(diagnostics)
    manifest_bytes = bytes(raw_manifest_bytes)

    if type(manifest) is not dict:
        diagnostics.append(
            _handoff_diagnostic(
                "catalog.handoff.manifest-invalid",
                "verified handoff Distribution Manifest must be an object",
                generation_id,
                location="manifest",
            )
        )
        return None, _ordered_diagnostics(diagnostics)

    try:
        parsed_manifest = json.loads(manifest_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        diagnostics.append(
            _handoff_diagnostic(
                "catalog.handoff.manifest-bytes-invalid",
                "verified handoff Distribution Manifest bytes are not UTF-8 JSON",
                generation_id,
                location="manifestBytes",
            )
        )
        return None, _ordered_diagnostics(diagnostics)

    if type(parsed_manifest) is not dict:
        diagnostics.append(
            _handoff_diagnostic(
                "catalog.handoff.manifest-bytes-invalid",
                "verified handoff Distribution Manifest bytes must encode an object",
                generation_id,
                location="manifestBytes",
            )
        )
        return None, _ordered_diagnostics(diagnostics)

    if manifest != parsed_manifest:
        diagnostics.append(
            _handoff_diagnostic(
                "catalog.handoff.manifest-snapshot-mismatch",
                "captured Distribution Manifest data does not match its exact bytes",
                generation_id,
                location="manifest",
            )
        )

    contract_diagnostics = contracts.validate_manifest_data(
        parsed_manifest,
        contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME,
        validators,
    )
    diagnostics.extend(
        _handoff_diagnostic(
            _handoff_contract_code(value),
            f"captured Distribution Manifest violates [{value.code}]",
            generation_id,
            location=value.pointer or "manifest",
        )
        for value in contract_diagnostics
    )
    if contract_diagnostics:
        return None, _ordered_diagnostics(diagnostics)

    canonical_bytes = contracts.render_normalized_engine_distribution_manifest(
        parsed_manifest
    ).encode("utf-8")
    if manifest_bytes != canonical_bytes:
        diagnostics.append(
            _handoff_diagnostic(
                "catalog.handoff.manifest-noncanonical",
                "captured Distribution Manifest bytes are not canonical v1 bytes",
                generation_id,
                location="manifestBytes",
            )
        )
    if contracts.compute_bytes_integrity(manifest_bytes) != manifest_integrity:
        diagnostics.append(
            _handoff_diagnostic(
                "catalog.handoff.manifest-integrity-mismatch",
                "captured Distribution Manifest bytes do not match handoff integrity",
                generation_id,
                location="manifestIntegrity",
            )
        )
    if parsed_manifest["engineGenerationId"] != generation_id:
        diagnostics.append(
            _handoff_diagnostic(
                "catalog.handoff.generation-id-mismatch",
                "handoff EngineGenerationId differs from captured Distribution inventory",
                generation_id,
                location="engineGenerationId",
            )
        )
    if diagnostics:
        return None, _ordered_diagnostics(diagnostics)

    assert isinstance(generation_root, Path)
    return (
        _CapturedHandoff(
            engine_generation_id=generation_id,
            generation_root=generation_root,
            manifest=parsed_manifest,
            manifest_bytes=manifest_bytes,
        ),
        (),
    )


def _inventory_key(value: dict[str, Any]) -> tuple[bytes, str, bytes]:
    return (
        _utf8_key(value["id"]),
        value["version"],
        _utf8_key(value["root"]),
    )


def _catalog_diagnostic(
    code: str,
    message: str,
    captured: _CapturedHandoff,
    package: dict[str, Any] | None = None,
    *,
    location: str = "",
) -> EngineDistributionPackageCatalogDiagnostic:
    return EngineDistributionPackageCatalogDiagnostic(
        code=code,
        message=message,
        engine_generation_id=captured.engine_generation_id,
        identity=package["id"] if package is not None else "",
        logical_root=package["root"] if package is not None else "",
        location=location,
    )


def _discovery_diagnostics(
    values: Iterable[discovery.CandidateDiscoveryDiagnostic],
    captured: _CapturedHandoff,
    inventory_by_source: dict[str, dict[str, Any]],
) -> tuple[EngineDistributionPackageCatalogDiagnostic, ...]:
    def safe_message(value: discovery.CandidateDiscoveryDiagnostic) -> str:
        message = value.message if isinstance(value.message, str) else ""
        root_text = str(captured.generation_root)
        leaks_root = bool(root_text) and root_text.casefold() in message.casefold()
        if (
            not message
            or leaks_root
            or _WINDOWS_ABSOLUTE_PATH_PATTERN.search(message)
            or _POSIX_ABSOLUTE_PATH_PATTERN.search(message)
        ):
            return "strict bundled-package candidate loading failed"
        return f"strict bundled-package candidate loading failed: {message}"

    def safe_location(value: object) -> str:
        if not isinstance(value, str):
            return ""
        if (
            not value
            or value.startswith(("/", "\\"))
            or "\\" in value
            or _WINDOWS_ABSOLUTE_PATH_PATTERN.search(value)
            or any(part in {"", ".", ".."} for part in value.split("/"))
        ):
            return ""
        return value

    diagnostics = []
    for value in values:
        package = inventory_by_source.get(value.source_key)
        diagnostics.append(
            _catalog_diagnostic(
                value.code,
                safe_message(value),
                captured,
                package,
                location=safe_location(value.location),
            )
        )
    return _ordered_diagnostics(diagnostics)


def _candidate_mismatch_diagnostics(
    candidate: PackageCandidate,
    package: dict[str, Any],
    captured: _CapturedHandoff,
) -> list[EngineDistributionPackageCatalogDiagnostic]:
    diagnostics: list[EngineDistributionPackageCatalogDiagnostic] = []

    def mismatch(code: str, field_name: str) -> None:
        diagnostics.append(
            _catalog_diagnostic(
                code,
                f"loaded candidate {field_name} differs from Distribution inventory",
                captured,
                package,
                location=field_name,
            )
        )

    if candidate.identity != package["id"]:
        mismatch("catalog.inventory.identity-mismatch", "id")
    if candidate.version != package["version"]:
        mismatch("catalog.inventory.version-mismatch", "version")
    if candidate.package_kind != package["packageKind"]:
        mismatch("catalog.inventory.kind-mismatch", "packageKind")
    if candidate.origin != f"engine-distribution:{package['root']}":
        mismatch("catalog.inventory.root-mismatch", "root")
    if candidate.source != {"kind": "engine-distribution"}:
        mismatch("catalog.inventory.source-mismatch", "source")
    if candidate.manifest_integrity != package["manifestIntegrity"]:
        mismatch("catalog.inventory.manifest-integrity-mismatch", "manifestIntegrity")
    if candidate.payload_integrity != package["payloadIntegrity"]:
        mismatch("catalog.inventory.payload-integrity-mismatch", "payloadIntegrity")

    expected_path = captured.generation_root.joinpath(*package["root"].split("/"))
    if not isinstance(candidate.payload_location, Path):
        mismatch("catalog.inventory.root-mismatch", "root")
    else:
        try:
            if candidate.payload_location.resolve(strict=True) != expected_path.resolve(
                strict=True
            ):
                mismatch("catalog.inventory.root-mismatch", "root")
        except (OSError, RuntimeError):
            diagnostics.append(
                _catalog_diagnostic(
                    "catalog.source.changed",
                    "bundled package root changed after strict candidate loading",
                    captured,
                    package,
                    location="root",
                )
            )
    return diagnostics


def derive_engine_distribution_package_catalog(
    verified_distribution: object,
    validators: contracts.ContractValidators,
) -> EngineDistributionPackageCatalogResult:
    """Derive one deterministic in-memory catalog from a verified Distribution."""

    captured, handoff_diagnostics = _capture_verified_handoff(
        verified_distribution,
        validators,
    )
    if captured is None:
        return _failure(handoff_diagnostics)

    inventory = tuple(
        sorted(captured.manifest["bundledPackages"], key=_inventory_key)
    )
    inventory_by_source = {
        f"engine-distribution:{package['root']}": package for package in inventory
    }
    locations = tuple(
        discovery.EngineDistributedCandidateLocation(
            distribution_root=captured.generation_root,
            relative_path=package["root"],
        )
        for package in inventory
    )
    loaded = discovery.load_package_candidates(locations, validators)
    if not loaded.succeeded:
        return _failure(
            _discovery_diagnostics(
                loaded.diagnostics,
                captured,
                inventory_by_source,
            )
        )

    diagnostics: list[EngineDistributionPackageCatalogDiagnostic] = []
    candidates_by_origin: dict[str, list[PackageCandidate]] = {}
    inventory_by_identity = {package["id"]: package for package in inventory}
    for candidate in loaded.candidates:
        associated_origin = candidate.origin
        if associated_origin not in inventory_by_source:
            identity_match = inventory_by_identity.get(candidate.identity)
            if identity_match is not None:
                associated_origin = f"engine-distribution:{identity_match['root']}"
        candidates_by_origin.setdefault(associated_origin, []).append(candidate)
        if associated_origin not in inventory_by_source:
            diagnostics.append(
                _catalog_diagnostic(
                    "catalog.inventory.unexpected-candidate",
                    "strict loader returned a candidate absent from Distribution inventory",
                    captured,
                )
            )

    ordered_candidates: list[PackageCandidate] = []
    entries: list[EngineDistributionPackageCatalogEntry] = []
    for package in inventory:
        source_key = f"engine-distribution:{package['root']}"
        matches = candidates_by_origin.get(source_key, [])
        if len(matches) != 1:
            diagnostics.append(
                _catalog_diagnostic(
                    (
                        "catalog.inventory.candidate-missing"
                        if not matches
                        else "catalog.inventory.candidate-duplicate"
                    ),
                    "Distribution inventory entry must produce exactly one candidate",
                    captured,
                    package,
                )
            )
            continue
        candidate = matches[0]
        diagnostics.extend(
            _candidate_mismatch_diagnostics(candidate, package, captured)
        )
        ordered_candidates.append(candidate)
        entries.append(
            EngineDistributionPackageCatalogEntry(
                identity=package["id"],
                version=package["version"],
                package_kind=package["packageKind"],
                availability=package["availability"],
                logical_root=package["root"],
            )
        )

    if diagnostics:
        return _failure(diagnostics)

    return EngineDistributionPackageCatalogResult(
        snapshot=EngineDistributionPackageCatalogSnapshot(
            engine_generation_id=captured.engine_generation_id,
            entries=tuple(entries),
            _distribution_manifest_bytes=bytes(captured.manifest_bytes),
            _candidate_snapshot=copy.deepcopy(tuple(ordered_candidates)),
        ),
        diagnostics=(),
    )


__all__ = [
    "EngineDistributionPackageCatalogDiagnostic",
    "EngineDistributionPackageCatalogEntry",
    "EngineDistributionPackageCatalogResult",
    "EngineDistributionPackageCatalogSnapshot",
    "derive_engine_distribution_package_catalog",
]
