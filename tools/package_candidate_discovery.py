"""Strict explicit-source Package Candidate Discovery loader."""

from __future__ import annotations

import copy
import json
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, TypeAlias

from tools import check_package_contracts as contracts
from tools.package_candidates import PackageCandidate


@dataclass(frozen=True)
class EngineDistributedCandidateLocation:
    """One inventory-selected package root under an Engine Distribution."""

    distribution_root: Path
    relative_path: str


@dataclass(frozen=True)
class ProjectEmbeddedCandidateLocation:
    """One package root relative to a trusted project root."""

    project_root: Path
    relative_path: str


@dataclass(frozen=True)
class LocalCandidateLocation:
    """One logical workspace source mapped to an adapter-local payload root."""

    source_id: str
    payload_root: Path


CandidateLocation: TypeAlias = (
    EngineDistributedCandidateLocation
    | ProjectEmbeddedCandidateLocation
    | LocalCandidateLocation
)


@dataclass(frozen=True)
class CandidateDiscoveryDiagnostic:
    """A deterministic discovery failure without adapter-local absolute paths."""

    code: str
    message: str
    source_key: str = "<invalid>"
    location: str = ""

    def render(self) -> str:
        rendered_location = self.source_key
        if self.location:
            rendered_location = f"{rendered_location}/{self.location}"
        return f"{rendered_location}: [{self.code}] {self.message}"


@dataclass(frozen=True)
class CandidateDiscoveryResult:
    """Atomic discovery result: all valid candidates or stable diagnostics."""

    candidates: tuple[PackageCandidate, ...]
    diagnostics: tuple[CandidateDiscoveryDiagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return not self.diagnostics


@dataclass(frozen=True)
class _PreparedLocation:
    source_key: str
    source: dict[str, Any]
    base_root: Path | None
    relative_path: str | None
    payload_root: Path | None


@dataclass(frozen=True)
class _ResolvedLocation:
    source_key: str
    source: dict[str, Any]
    payload_root: Path
    physical_key: tuple[Any, ...]


def _diagnostic_sort_key(
    diagnostic: CandidateDiscoveryDiagnostic,
) -> tuple[bytes, str, str, str]:
    return (
        diagnostic.source_key.encode("utf-8"),
        diagnostic.code,
        diagnostic.location,
        diagnostic.message,
    )


def _source_validation_lock(source: Any) -> dict[str, Any]:
    reference = {
        "id": "com.asharia.discovery.placeholder",
        "version": "0.0.0",
        "packageKind": "installable-capability",
    }
    node = {
        **reference,
        "source": source,
        "dependencies": [],
    }
    if isinstance(source, dict) and source.get("kind") != "engine-distribution":
        node["manifestIntegrity"] = {"algorithm": "sha256", "digest": "0" * 64}
        node["payloadIntegrity"] = {"algorithm": "sha256", "digest": "0" * 64}
    return {
        "schema": "com.asharia.package-lock",
        "schemaVersion": 2,
        "resolver": {"version": "0.0.0", "policyVersion": 1},
        "inputs": {
            "engine": {
                "distributionId": "com.asharia.distribution.placeholder",
                "engineApiVersion": "0.0.0",
                "engineGenerationId": f"sha256-{'0' * 64}",
            },
            "projectManifestIntegrity": {"algorithm": "sha256", "digest": "0" * 64},
        },
        "roots": {"directPackages": [reference], "directFeatureSets": []},
        "nodes": [node],
    }


def _source_contract_diagnostics(
    source: dict[str, Any],
    source_key: str,
    validators: contracts.ContractValidators,
) -> list[CandidateDiscoveryDiagnostic]:
    diagnostics = contracts.validate_manifest_data(
        _source_validation_lock(source),
        "candidate-source",
        validators,
    )
    result: list[CandidateDiscoveryDiagnostic] = []
    prefix = "/nodes/0/source"
    for diagnostic in diagnostics:
        pointer = diagnostic.pointer
        if pointer.startswith(prefix):
            pointer = pointer[len(prefix) :]
        result.append(
            CandidateDiscoveryDiagnostic(
                code=diagnostic.code,
                source_key=source_key,
                location=pointer.lstrip("/") or "source",
                message="source descriptor violates the package source contract",
            )
        )
    return result


def _invalid_source_key(location: CandidateLocation) -> str:
    def safe_fragment(value: object, fallback: str) -> str:
        if not isinstance(value, str):
            return fallback
        try:
            value.encode("utf-8")
        except UnicodeEncodeError:
            return fallback
        return value

    if isinstance(location, EngineDistributedCandidateLocation):
        return "engine-distribution:<invalid-relative-path>"
    if isinstance(location, ProjectEmbeddedCandidateLocation):
        return "project-embedded:<invalid-relative-path>"
    if isinstance(location, LocalCandidateLocation):
        source_id = safe_fragment(location.source_id, "<invalid-source-id>")
        return f"local:{source_id}"
    return "<invalid>"


def _prepare_location(
    location: object,
    validators: contracts.ContractValidators,
) -> tuple[_PreparedLocation | None, list[CandidateDiscoveryDiagnostic]]:
    if isinstance(location, EngineDistributedCandidateLocation):
        source = {"kind": "engine-distribution"}
        root = location.distribution_root
        base_root = root
        relative_path = location.relative_path
        payload_root = None
    elif isinstance(location, ProjectEmbeddedCandidateLocation):
        source = {"kind": "project-embedded", "relativePath": location.relative_path}
        root = location.project_root
        base_root = root
        relative_path = location.relative_path
        payload_root = None
    elif isinstance(location, LocalCandidateLocation):
        source = {"kind": "local", "sourceId": location.source_id}
        root = location.payload_root
        base_root = None
        relative_path = None
        payload_root = root
    else:
        return None, [
            CandidateDiscoveryDiagnostic(
                code="discovery.input.invalid",
                message="candidate location has an unsupported type",
            )
        ]

    invalid_key = _invalid_source_key(location)
    diagnostics = _source_contract_diagnostics(source, invalid_key, validators)
    if not isinstance(root, Path):
        diagnostics.append(
            CandidateDiscoveryDiagnostic(
                code="discovery.input.invalid",
                source_key=invalid_key,
                location="root",
                message="adapter-local root must be a pathlib.Path",
            )
        )
    if diagnostics:
        return None, diagnostics

    if isinstance(location, EngineDistributedCandidateLocation):
        source_key = f"engine-distribution:{location.relative_path}"
    elif isinstance(location, ProjectEmbeddedCandidateLocation):
        source_key = f"project-embedded:{location.relative_path}"
    else:
        source_key = f"local:{location.source_id}"
    return (
        _PreparedLocation(
            source_key=source_key,
            source=source,
            base_root=base_root,
            relative_path=relative_path,
            payload_root=payload_root,
        ),
        [],
    )


def _is_link(path: Path) -> bool:
    is_junction = getattr(path, "is_junction", None)
    return path.is_symlink() or bool(is_junction is not None and is_junction())


def _directory_diagnostic(
    path: Path,
    source_key: str,
    location: str,
) -> CandidateDiscoveryDiagnostic | None:
    try:
        if _is_link(path):
            return CandidateDiscoveryDiagnostic(
                code="discovery.source.link",
                source_key=source_key,
                location=location,
                message="package source path cannot contain a link or junction",
            )
        if not path.exists():
            return CandidateDiscoveryDiagnostic(
                code="discovery.source.unavailable",
                source_key=source_key,
                location=location,
                message="package source path is unavailable",
            )
        if not path.is_dir():
            return CandidateDiscoveryDiagnostic(
                code="discovery.source.not-directory",
                source_key=source_key,
                location=location,
                message="package source path must be a directory",
            )
    except OSError:
        return CandidateDiscoveryDiagnostic(
            code="discovery.source.unavailable",
            source_key=source_key,
            location=location,
            message="package source path is unavailable",
        )
    return None


def _physical_key(path: Path) -> tuple[Any, ...]:
    stat = path.stat()
    if stat.st_ino:
        return ("file-id", stat.st_dev, stat.st_ino)
    return ("path", os.path.normcase(str(path)))


def _resolve_location(
    location: _PreparedLocation,
) -> tuple[_ResolvedLocation | None, list[CandidateDiscoveryDiagnostic]]:
    if location.payload_root is not None:
        diagnostic = _directory_diagnostic(
            location.payload_root,
            location.source_key,
            "",
        )
        if diagnostic is not None:
            return None, [diagnostic]
        try:
            resolved_root = location.payload_root.resolve(strict=True)
            physical_key = _physical_key(resolved_root)
        except (OSError, RuntimeError):
            return None, [
                CandidateDiscoveryDiagnostic(
                    code="discovery.source.unavailable",
                    source_key=location.source_key,
                    message="package source path is unavailable",
                )
            ]
        return (
            _ResolvedLocation(
                source_key=location.source_key,
                source=location.source,
                payload_root=resolved_root,
                physical_key=physical_key,
            ),
            [],
        )

    assert location.base_root is not None
    assert location.relative_path is not None
    diagnostic = _directory_diagnostic(location.base_root, location.source_key, "")
    if diagnostic is not None:
        return None, [diagnostic]

    current = location.base_root
    traversed: list[str] = []
    for part in location.relative_path.split("/"):
        traversed.append(part)
        current = current / part
        diagnostic = _directory_diagnostic(
            current,
            location.source_key,
            "/".join(traversed),
        )
        if diagnostic is not None:
            return None, [diagnostic]

    try:
        resolved_base = location.base_root.resolve(strict=True)
        resolved_root = current.resolve(strict=True)
        resolved_root.relative_to(resolved_base)
        physical_key = _physical_key(resolved_root)
    except ValueError:
        return None, [
            CandidateDiscoveryDiagnostic(
                code="discovery.source.path-escape",
                source_key=location.source_key,
                location=location.relative_path,
                message="package source path escapes its trusted root",
            )
        ]
    except (OSError, RuntimeError):
        return None, [
            CandidateDiscoveryDiagnostic(
                code="discovery.source.unavailable",
                source_key=location.source_key,
                location=location.relative_path,
                message="package source path is unavailable",
            )
        ]
    return (
        _ResolvedLocation(
            source_key=location.source_key,
            source=location.source,
            payload_root=resolved_root,
            physical_key=physical_key,
        ),
        [],
    )


def _manifest_diagnostics(
    manifest: Any,
    source_key: str,
    validators: contracts.ContractValidators,
) -> list[CandidateDiscoveryDiagnostic]:
    diagnostics = contracts.validate_manifest_data(
        manifest,
        contracts.PACKAGE_MANIFEST_NAME,
        validators,
    )
    return [
        CandidateDiscoveryDiagnostic(
            code=diagnostic.code,
            source_key=source_key,
            location=(
                f"{contracts.PACKAGE_MANIFEST_NAME}{diagnostic.pointer}"
                if diagnostic.pointer
                else contracts.PACKAGE_MANIFEST_NAME
            ),
            message=diagnostic.message,
        )
        for diagnostic in diagnostics
    ]


def _descriptor_diagnostics(
    descriptor: Any,
    manifest: Any,
    source_key: str,
    validators: contracts.ContractValidators,
) -> list[CandidateDiscoveryDiagnostic]:
    diagnostics = contracts.validate_package_source_build_binding(
        descriptor,
        manifest,
        validators,
    )
    return [
        CandidateDiscoveryDiagnostic(
            code=diagnostic.code,
            source_key=source_key,
            location=(
                f"{contracts.PACKAGE_SOURCE_BUILD_NAME}{diagnostic.pointer}"
                if diagnostic.pointer
                else contracts.PACKAGE_SOURCE_BUILD_NAME
            ),
            message=diagnostic.message,
        )
        for diagnostic in diagnostics
    ]


def _product_declaration_diagnostics(
    declaration: Any,
    manifest: Any,
    source_key: str,
    validators: contracts.ContractValidators,
) -> list[CandidateDiscoveryDiagnostic]:
    diagnostics = contracts.validate_package_product_declaration_binding(
        declaration,
        manifest,
        validators,
    )
    return [
        CandidateDiscoveryDiagnostic(
            code=diagnostic.code,
            source_key=source_key,
            location=(
                f"{contracts.PACKAGE_PRODUCTS_NAME}{diagnostic.pointer}"
                if diagnostic.pointer
                else contracts.PACKAGE_PRODUCTS_NAME
            ),
            message=diagnostic.message,
        )
        for diagnostic in diagnostics
    ]


def _read_optional_contract_bytes(path: Path) -> tuple[str, bytes | None]:
    try:
        if not path.exists():
            return "absent", None
        if _is_link(path) or not path.is_file():
            return "invalid", None
        return "regular", path.read_bytes()
    except OSError:
        return "invalid", None


def _tree_integrity_diagnostic(
    failure: contracts.PackageTreeIntegrityError,
    source_key: str,
) -> CandidateDiscoveryDiagnostic:
    code_by_failure = {
        "link": "discovery.payload.link",
        "path-not-nfc": "discovery.payload.path-not-nfc",
        "path-not-portable": "discovery.payload.path-not-portable",
        "case-collision": "discovery.payload.case-collision",
        "non-regular": "discovery.payload.non-regular",
    }
    code = code_by_failure.get(failure.code, "discovery.source.changed")
    message = (
        str(failure)
        if failure.code in code_by_failure
        else "package source changed while candidate evidence was collected"
    )
    return CandidateDiscoveryDiagnostic(
        code=code,
        source_key=source_key,
        location=failure.relative_path,
        message=message,
    )


def _load_candidate(
    location: _ResolvedLocation,
    validators: contracts.ContractValidators,
) -> tuple[PackageCandidate | None, list[CandidateDiscoveryDiagnostic]]:
    manifest_path = location.payload_root / contracts.PACKAGE_MANIFEST_NAME
    try:
        if _is_link(manifest_path):
            return None, [
                CandidateDiscoveryDiagnostic(
                    code="discovery.payload.link",
                    source_key=location.source_key,
                    location=contracts.PACKAGE_MANIFEST_NAME,
                    message="package payload cannot contain a link or junction",
                )
            ]
        if not manifest_path.exists():
            return None, [
                CandidateDiscoveryDiagnostic(
                    code="discovery.manifest.missing",
                    source_key=location.source_key,
                    location=contracts.PACKAGE_MANIFEST_NAME,
                    message=f"package root must contain '{contracts.PACKAGE_MANIFEST_NAME}'",
                )
            ]
        if not manifest_path.is_file():
            return None, [
                CandidateDiscoveryDiagnostic(
                    code="discovery.manifest.unreadable",
                    source_key=location.source_key,
                    location=contracts.PACKAGE_MANIFEST_NAME,
                    message="author manifest must be a regular file",
                )
            ]
        manifest_bytes = manifest_path.read_bytes()
    except OSError:
        return None, [
            CandidateDiscoveryDiagnostic(
                code="discovery.manifest.unreadable",
                source_key=location.source_key,
                location=contracts.PACKAGE_MANIFEST_NAME,
                message="author manifest could not be read",
            )
        ]

    try:
        manifest = json.loads(manifest_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None, [
            CandidateDiscoveryDiagnostic(
                code="contract.manifest.json",
                source_key=location.source_key,
                location=contracts.PACKAGE_MANIFEST_NAME,
                message="author manifest must be valid UTF-8 JSON",
            )
        ]

    diagnostics = _manifest_diagnostics(manifest, location.source_key, validators)
    if diagnostics:
        return None, diagnostics
    if not isinstance(manifest, dict) or manifest.get("packageKind") not in {
        "installable-capability",
        "feature-set",
    }:
        return None, [
            CandidateDiscoveryDiagnostic(
                code="discovery.manifest.unsupported-kind",
                source_key=location.source_key,
                location=contracts.PACKAGE_MANIFEST_NAME,
                message="author manifest must describe an installable package or Feature Set",
            )
        ]

    descriptor_path = location.payload_root / contracts.PACKAGE_SOURCE_BUILD_NAME
    descriptor_state, descriptor_bytes = _read_optional_contract_bytes(
        descriptor_path
    )
    if descriptor_state == "invalid":
        return None, [
            CandidateDiscoveryDiagnostic(
                code="discovery.build-descriptor.unreadable",
                source_key=location.source_key,
                location=contracts.PACKAGE_SOURCE_BUILD_NAME,
                message="source build descriptor must be a readable regular file",
            )
        ]

    descriptor: dict[str, Any] | None = None
    if descriptor_bytes is not None:
        try:
            parsed_descriptor = json.loads(descriptor_bytes.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            return None, [
                CandidateDiscoveryDiagnostic(
                    code="contract.manifest.json",
                    source_key=location.source_key,
                    location=contracts.PACKAGE_SOURCE_BUILD_NAME,
                    message="source build descriptor must be valid UTF-8 JSON",
                )
            ]
        descriptor_diagnostics = _descriptor_diagnostics(
            parsed_descriptor,
            manifest,
            location.source_key,
            validators,
        )
        if descriptor_diagnostics:
            return None, descriptor_diagnostics
        assert isinstance(parsed_descriptor, dict)
        descriptor = parsed_descriptor

    product_declaration_path = location.payload_root / contracts.PACKAGE_PRODUCTS_NAME
    product_declaration_state, product_declaration_bytes = (
        _read_optional_contract_bytes(product_declaration_path)
    )
    if product_declaration_state == "invalid":
        return None, [
            CandidateDiscoveryDiagnostic(
                code="discovery.product-declaration.unreadable",
                source_key=location.source_key,
                location=contracts.PACKAGE_PRODUCTS_NAME,
                message="product declaration must be a readable regular file",
            )
        ]

    product_declaration: dict[str, Any] | None = None
    if product_declaration_bytes is not None:
        try:
            parsed_product_declaration = json.loads(
                product_declaration_bytes.decode("utf-8")
            )
        except (UnicodeDecodeError, json.JSONDecodeError):
            return None, [
                CandidateDiscoveryDiagnostic(
                    code="contract.manifest.json",
                    source_key=location.source_key,
                    location=contracts.PACKAGE_PRODUCTS_NAME,
                    message="product declaration must be valid UTF-8 JSON",
                )
            ]
        product_diagnostics = _product_declaration_diagnostics(
            parsed_product_declaration,
            manifest,
            location.source_key,
            validators,
        )
        if product_diagnostics:
            return None, product_diagnostics
        assert isinstance(parsed_product_declaration, dict)
        product_declaration = parsed_product_declaration

    manifest_integrity = contracts.compute_bytes_integrity(manifest_bytes)
    try:
        payload_integrity = contracts.compute_package_tree_integrity(location.payload_root)
    except contracts.PackageTreeIntegrityError as failure:
        return None, [_tree_integrity_diagnostic(failure, location.source_key)]
    except OSError:
        return None, [
            CandidateDiscoveryDiagnostic(
                code="discovery.payload.unreadable",
                source_key=location.source_key,
                message="package payload could not be read",
            )
        ]

    try:
        final_manifest_bytes = manifest_path.read_bytes()
    except OSError:
        final_manifest_bytes = None
    if final_manifest_bytes != manifest_bytes:
        return None, [
            CandidateDiscoveryDiagnostic(
                code="discovery.source.changed",
                source_key=location.source_key,
                location=contracts.PACKAGE_MANIFEST_NAME,
                message="package source changed while candidate evidence was collected",
            )
        ]

    final_descriptor_state, final_descriptor_bytes = _read_optional_contract_bytes(
        descriptor_path
    )
    if (
        final_descriptor_state != descriptor_state
        or final_descriptor_bytes != descriptor_bytes
    ):
        return None, [
            CandidateDiscoveryDiagnostic(
                code="discovery.source.changed",
                source_key=location.source_key,
                location=contracts.PACKAGE_SOURCE_BUILD_NAME,
                message="package source changed while candidate evidence was collected",
            )
        ]

    final_product_state, final_product_bytes = _read_optional_contract_bytes(
        product_declaration_path
    )
    if (
        final_product_state != product_declaration_state
        or final_product_bytes != product_declaration_bytes
    ):
        return None, [
            CandidateDiscoveryDiagnostic(
                code="discovery.source.changed",
                source_key=location.source_key,
                location=contracts.PACKAGE_PRODUCTS_NAME,
                message="package source changed while candidate evidence was collected",
            )
        ]

    return (
        PackageCandidate(
            identity=manifest["id"],
            version=manifest["version"],
            package_kind=manifest["packageKind"],
            origin=location.source_key,
            source=copy.deepcopy(location.source),
            manifest_integrity=manifest_integrity,
            payload_integrity=payload_integrity,
            manifest=copy.deepcopy(manifest),
            build_descriptor=copy.deepcopy(descriptor),
            build_descriptor_integrity=(
                contracts.compute_bytes_integrity(descriptor_bytes)
                if descriptor_bytes is not None
                else None
            ),
            build_descriptor_bytes=descriptor_bytes,
            product_declaration=copy.deepcopy(product_declaration),
            product_declaration_integrity=(
                contracts.compute_bytes_integrity(product_declaration_bytes)
                if product_declaration_bytes is not None
                else None
            ),
            product_declaration_bytes=product_declaration_bytes,
            payload_location=location.payload_root,
        ),
        [],
    )


def load_package_candidates(
    locations: Iterable[CandidateLocation],
    validators: contracts.ContractValidators,
) -> CandidateDiscoveryResult:
    """Load exact package roots as one deterministic, atomic candidate batch."""

    try:
        location_values = tuple(locations)
    except TypeError:
        diagnostic = CandidateDiscoveryDiagnostic(
            code="discovery.input.invalid",
            message="candidate locations must be iterable",
        )
        return CandidateDiscoveryResult((), (diagnostic,))

    diagnostics: list[CandidateDiscoveryDiagnostic] = []
    prepared_locations: list[_PreparedLocation] = []
    for location in location_values:
        prepared, failures = _prepare_location(location, validators)
        diagnostics.extend(failures)
        if prepared is not None:
            prepared_locations.append(prepared)

    locations_by_key: dict[str, list[_PreparedLocation]] = {}
    for location in prepared_locations:
        locations_by_key.setdefault(location.source_key, []).append(location)

    unique_locations: list[_PreparedLocation] = []
    for source_key in sorted(locations_by_key, key=lambda value: value.encode("utf-8")):
        same_key = locations_by_key[source_key]
        if len(same_key) > 1:
            diagnostics.append(
                CandidateDiscoveryDiagnostic(
                    code="discovery.source.duplicate",
                    source_key=source_key,
                    message=f"source key '{source_key}' is configured more than once",
                )
            )
        else:
            unique_locations.append(same_key[0])

    resolved_locations: list[_ResolvedLocation] = []
    for location in unique_locations:
        resolved, failures = _resolve_location(location)
        diagnostics.extend(failures)
        if resolved is not None:
            resolved_locations.append(resolved)

    locations_by_physical_key: dict[tuple[Any, ...], list[_ResolvedLocation]] = {}
    for location in resolved_locations:
        locations_by_physical_key.setdefault(location.physical_key, []).append(location)

    aliased_source_keys: set[str] = set()
    for same_root in locations_by_physical_key.values():
        if len(same_root) < 2:
            continue
        source_keys = sorted(
            (location.source_key for location in same_root),
            key=lambda value: value.encode("utf-8"),
        )
        aliased_source_keys.update(source_keys)
        diagnostics.append(
            CandidateDiscoveryDiagnostic(
                code="discovery.source.alias",
                source_key=source_keys[0],
                message=(
                    "source keys "
                    + ", ".join(f"'{source_key}'" for source_key in source_keys)
                    + " resolve to the same payload root"
                ),
            )
        )

    candidates: list[PackageCandidate] = []
    for location in sorted(
        resolved_locations,
        key=lambda value: value.source_key.encode("utf-8"),
    ):
        if location.source_key in aliased_source_keys:
            continue
        candidate, failures = _load_candidate(location, validators)
        diagnostics.extend(failures)
        if candidate is not None:
            candidates.append(candidate)

    sorted_diagnostics = tuple(sorted(diagnostics, key=_diagnostic_sort_key))
    if sorted_diagnostics:
        return CandidateDiscoveryResult((), sorted_diagnostics)
    return CandidateDiscoveryResult(tuple(candidates), ())
