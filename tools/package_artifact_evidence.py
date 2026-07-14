"""Pure Package Product & Artifact Evidence v1 verification."""

from __future__ import annotations

import json
import re
import unicodedata
from dataclasses import dataclass, field, replace
from pathlib import PureWindowsPath
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import host_package_composition as composition
from tools import source_build_plan as source_build
from tools.package_candidates import PackageCandidate
from tools.package_lock_verification import LockedGraphVerificationResult


PACKAGE_ARTIFACT_MANIFEST_SCHEMA = "com.asharia.package-artifact-manifest"
PACKAGE_ARTIFACT_MANIFEST_SCHEMA_VERSION = 1
ARTIFACT_FILE_ROLES = frozenset(
    {
        "primary",
        "link-companion",
        "runtime-dependency",
        "debug-symbol",
        "metadata",
    }
)
MEDIA_TYPE_PATTERN = re.compile(
    r"^[A-Za-z0-9][A-Za-z0-9!#$&^_.+\-]*/"
    r"[A-Za-z0-9][A-Za-z0-9!#$&^_.+\-]*$"
)
_WINDOWS_INVALID_FILENAME_CHARACTERS = frozenset('<>:"\\|?*')

IntegrityRecord = source_build.IntegrityRecord


@dataclass(frozen=True, order=True)
class ArtifactFileObservation:
    """Caller-supplied immutable bytes and claimed evidence for one file."""

    path: str
    role: str
    media_type: str
    size: int
    integrity: IntegrityRecord
    content: bytes = field(repr=False)


@dataclass(frozen=True)
class ProductArtifactObservation:
    """One observed logical product in one target/configuration context."""

    package_id: str
    package_version: str
    module_id: str
    product_id: str
    target_platform: str
    configuration: str
    files: tuple[ArtifactFileObservation, ...]


@dataclass(frozen=True)
class _CollectedProductArtifactEvidence:
    """Process-local descriptor handoff from the owned streaming collector."""

    package_id: str
    package_version: str
    module_id: str
    product_id: str
    target_platform: str
    configuration: str
    files: tuple[ArtifactFileEvidence, ...]


@dataclass(frozen=True, order=True)
class ArtifactFileEvidence:
    """Verified content descriptor for one package-relative file."""

    path: str
    role: str
    media_type: str
    size: int
    integrity: IntegrityRecord


@dataclass(frozen=True)
class ProductArtifactEvidence:
    """Verified file set for one declared logical product."""

    product_id: str
    purpose: str
    files: tuple[ArtifactFileEvidence, ...]


@dataclass(frozen=True)
class ModuleArtifactEvidence:
    """Verified delivery result for one selected logical module."""

    module_id: str
    delivery_kind: str
    products: tuple[ProductArtifactEvidence, ...]


@dataclass(frozen=True)
class PackageArtifactProvenance:
    """Stable authorities used to verify one package artifact manifest."""

    host_composition_integrity: IntegrityRecord
    source_build_plan_integrity: IntegrityRecord
    product_declaration_integrity: IntegrityRecord


@dataclass(frozen=True)
class PackageArtifactManifest:
    """Canonical verified artifact evidence for one exact package."""

    package_id: str
    package_version: str
    host_kind: str
    target_platform: str
    configuration: str
    provenance: PackageArtifactProvenance
    modules: tuple[ModuleArtifactEvidence, ...]
    integrity: IntegrityRecord


@dataclass(frozen=True)
class PackageArtifactVerificationResult:
    """Atomic result for the complete selected Host package set."""

    manifests: tuple[PackageArtifactManifest, ...]
    manifest_set_integrity: IntegrityRecord | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.manifest_set_integrity is not None and not self.diagnostics


def _utf8_key(value: str) -> bytes:
    return value.encode("utf-8")


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
        manifest_path=contracts.PACKAGE_ARTIFACT_MANIFEST_NAME,
        pointer=pointer,
        message=message,
    )


def _failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> PackageArtifactVerificationResult:
    return PackageArtifactVerificationResult(
        manifests=(),
        manifest_set_integrity=None,
        diagnostics=tuple(sorted(diagnostics, key=_diagnostic_sort_key)),
    )


def _integrity_data(integrity: IntegrityRecord) -> dict[str, str]:
    return {"algorithm": integrity.algorithm, "digest": integrity.digest}


def _integrity_record(integrity: dict[str, str]) -> IntegrityRecord:
    return IntegrityRecord(integrity["algorithm"], integrity["digest"])


def _data_integrity(value: Any) -> dict[str, str]:
    canonical = json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return contracts.compute_bytes_integrity(canonical)


def _manifest_payload_data(manifest: PackageArtifactManifest) -> dict[str, Any]:
    return {
        "schema": PACKAGE_ARTIFACT_MANIFEST_SCHEMA,
        "schemaVersion": PACKAGE_ARTIFACT_MANIFEST_SCHEMA_VERSION,
        "package": {
            "id": manifest.package_id,
            "version": manifest.package_version,
        },
        "context": {
            "hostKind": manifest.host_kind,
            "targetPlatform": manifest.target_platform,
            "configuration": manifest.configuration,
        },
        "provenance": {
            "kind": "source-build",
            "hostCompositionIntegrity": _integrity_data(
                manifest.provenance.host_composition_integrity
            ),
            "sourceBuildPlanIntegrity": _integrity_data(
                manifest.provenance.source_build_plan_integrity
            ),
            "productDeclarationIntegrity": _integrity_data(
                manifest.provenance.product_declaration_integrity
            ),
        },
        "modules": [
            {
                "moduleId": module.module_id,
                "delivery": (
                    {"kind": "no-artifacts"}
                    if module.delivery_kind == "no-artifacts"
                    else {
                        "kind": "artifact-set",
                        "products": [
                            {
                                "id": product.product_id,
                                "purpose": product.purpose,
                                "files": [
                                    {
                                        "path": artifact.path,
                                        "role": artifact.role,
                                        "mediaType": artifact.media_type,
                                        "size": artifact.size,
                                        "integrity": _integrity_data(
                                            artifact.integrity
                                        ),
                                    }
                                    for artifact in product.files
                                ],
                            }
                            for product in module.products
                        ],
                    }
                ),
            }
            for module in manifest.modules
        ],
    }


def package_artifact_manifest_to_data(
    manifest: PackageArtifactManifest,
) -> dict[str, Any]:
    """Return the fixed-field-order JSON-compatible manifest representation."""

    return {
        **_manifest_payload_data(manifest),
        "integrity": _integrity_data(manifest.integrity),
    }


def render_package_artifact_manifest(manifest: PackageArtifactManifest) -> str:
    """Render canonical Package Artifact Manifest JSON with LF and final newline."""

    return json.dumps(
        package_artifact_manifest_to_data(manifest),
        ensure_ascii=False,
        indent=2,
    ) + "\n"


def compute_package_artifact_manifest_integrity(
    manifest: PackageArtifactManifest,
) -> dict[str, str]:
    """Hash canonical manifest fields except the self-describing integrity field."""

    return _data_integrity(_manifest_payload_data(manifest))


def validate_package_artifact_manifest_data(
    manifest: Any,
    validators: contracts.ContractValidators,
) -> list[contracts.Diagnostic]:
    """Validate the closed manifest shape, semantic rules, and self-integrity."""

    data = (
        package_artifact_manifest_to_data(manifest)
        if isinstance(manifest, PackageArtifactManifest)
        else manifest
    )
    diagnostics = contracts.validate_manifest_data(
        data,
        contracts.PACKAGE_ARTIFACT_MANIFEST_NAME,
        validators,
    )
    if diagnostics or not isinstance(data, dict):
        return diagnostics
    payload = {key: value for key, value in data.items() if key != "integrity"}
    if data["integrity"] != _data_integrity(payload):
        diagnostics.append(
            _diagnostic(
                "artifact.manifest.integrity-mismatch",
                "/integrity",
                "Package Artifact Manifest integrity does not match canonical fields",
            )
        )
    return sorted(diagnostics, key=_diagnostic_sort_key)


def render_package_artifact_manifest_set(
    manifests: Iterable[PackageArtifactManifest],
) -> str:
    """Render a deterministic in-memory handoff for an atomic manifest set."""

    ordered = sorted(manifests, key=lambda value: _utf8_key(value.package_id))
    return json.dumps(
        [package_artifact_manifest_to_data(manifest) for manifest in ordered],
        ensure_ascii=False,
        indent=2,
    ) + "\n"


def is_portable_artifact_path(value: Any) -> bool:
    """Return whether a manifest path has one portable filesystem spelling."""

    if not isinstance(value, str):
        return False
    if unicodedata.normalize("NFC", value) != value or "\\" in value or ":" in value:
        return False
    try:
        value.encode("utf-8")
    except UnicodeEncodeError:
        return False
    segments = value.split("/")
    if (
        not value
        or value.startswith("/")
        or any(segment in {"", ".", ".."} for segment in segments)
    ):
        return False
    if value.casefold() == contracts.PACKAGE_ARTIFACT_MANIFEST_NAME.casefold():
        return False
    if any(PureWindowsPath(segment).is_reserved() for segment in segments):
        return False
    return all(
        not segment.endswith((" ", "."))
        and not any(
            character in _WINDOWS_INVALID_FILENAME_CHARACTERS
            or ord(character) < 32
            for character in segment
        )
        for segment in segments
    )


def _candidate_product_declaration_diagnostics(
    candidate: PackageCandidate,
    validators: contracts.ContractValidators,
) -> list[contracts.Diagnostic]:
    declaration_path = f"candidate/{candidate.identity}/{contracts.PACKAGE_PRODUCTS_NAME}"
    values = (
        candidate.product_declaration,
        candidate.product_declaration_integrity,
        candidate.product_declaration_bytes,
    )
    present_count = sum(value is not None for value in values)
    if present_count == 0:
        return [
            _diagnostic(
                "artifact.product-declaration.missing",
                "/provenance/productDeclarationIntegrity",
                f"selected package '{candidate.identity}' has no product declaration",
            )
        ]
    if present_count != 3 or not (
        isinstance(candidate.product_declaration, dict)
        and isinstance(candidate.product_declaration_integrity, dict)
        and isinstance(candidate.product_declaration_bytes, bytes)
    ):
        return [
            _diagnostic(
                "artifact.product-declaration.incomplete",
                "/provenance/productDeclarationIntegrity",
                f"product declaration snapshot for '{candidate.identity}' is incomplete",
            )
        ]

    diagnostics: list[contracts.Diagnostic] = []
    if contracts.compute_bytes_integrity(
        candidate.product_declaration_bytes
    ) != candidate.product_declaration_integrity:
        diagnostics.append(
            _diagnostic(
                "artifact.product-declaration.integrity-mismatch",
                "/provenance/productDeclarationIntegrity",
                f"product declaration bytes changed for '{candidate.identity}'",
            )
        )
    try:
        parsed = json.loads(candidate.product_declaration_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        parsed = None
    if parsed != candidate.product_declaration:
        diagnostics.append(
            _diagnostic(
                "artifact.product-declaration.snapshot-mismatch",
                "/provenance/productDeclarationIntegrity",
                f"captured product declaration data and bytes differ for '{candidate.identity}'",
            )
        )
    diagnostics.extend(
        contracts.validate_package_product_declaration_binding(
            candidate.product_declaration,
            candidate.manifest,
            validators,
            declaration_path=declaration_path,
            manifest_path=f"candidate/{candidate.identity}/{contracts.PACKAGE_MANIFEST_NAME}",
        )
    )
    return diagnostics


def _verify_control_plane_inputs(
    host_plan: Any,
    source_plan: Any,
    verified_graph: Any,
    validators: contracts.ContractValidators,
) -> tuple[
    composition.HostCompositionPlan | None,
    source_build.SourceBuildPlan | None,
    dict[str, PackageCandidate],
    list[contracts.Diagnostic],
]:
    diagnostics: list[contracts.Diagnostic] = []
    if not isinstance(host_plan, composition.HostCompositionPlan):
        diagnostics.append(
            _diagnostic(
                "artifact.input.host-composition-invalid",
                "",
                "artifact verifier requires a HostCompositionPlan",
            )
        )
    else:
        diagnostics.extend(
            composition.validate_host_composition_plan_data(
                composition.host_composition_plan_to_data(host_plan),
                validators,
            )
        )

    if not isinstance(source_plan, source_build.SourceBuildPlan):
        diagnostics.append(
            _diagnostic(
                "artifact.input.source-build-plan-invalid",
                "",
                "artifact verifier requires a SourceBuildPlan",
            )
        )
    else:
        diagnostics.extend(
            source_build.validate_source_build_plan_data(source_plan, validators)
        )

    if not isinstance(verified_graph, LockedGraphVerificationResult):
        diagnostics.append(
            _diagnostic(
                "artifact.input.verified-graph-invalid",
                "",
                "artifact verifier requires a LockedGraphVerificationResult",
            )
        )
    elif not verified_graph.succeeded:
        diagnostics.append(
            _diagnostic(
                "artifact.input.verified-graph-failed",
                "",
                "artifact verifier requires a successful locked graph verification",
            )
        )

    if diagnostics:
        return None, None, {}, diagnostics
    assert isinstance(host_plan, composition.HostCompositionPlan)
    assert isinstance(source_plan, source_build.SourceBuildPlan)
    assert isinstance(verified_graph, LockedGraphVerificationResult)
    assert isinstance(verified_graph.lock, dict)

    lock_diagnostics = contracts.validate_manifest_data(
        verified_graph.lock,
        contracts.PACKAGE_LOCK_NAME,
        validators,
    )
    if lock_diagnostics:
        return None, None, {}, lock_diagnostics
    normalized_lock = contracts.normalize_lock_manifest(verified_graph.lock)
    if normalized_lock != verified_graph.lock:
        diagnostics.append(
            _diagnostic(
                "artifact.input.unverified",
                "/provenance/sourceBuildPlanIntegrity",
                "successful locked verification must provide a normalized graph",
            )
        )

    expected_host_integrity = contracts.compute_bytes_integrity(
        composition.render_host_composition_plan(host_plan).encode("utf-8")
    )
    if _integrity_data(
        source_plan.inputs.host_composition_integrity
    ) != expected_host_integrity:
        diagnostics.append(
            _diagnostic(
                "artifact.input.host-composition-stale",
                "/provenance/hostCompositionIntegrity",
                "Source Build Plan does not match the Host Composition Plan",
            )
        )
    if (
        source_plan.host_kind != host_plan.host_kind
        or source_plan.target_platform != host_plan.target_platform
    ):
        diagnostics.append(
            _diagnostic(
                "artifact.input.host-context-mismatch",
                "/context",
                "Source Build Plan host context does not match Host Composition",
            )
        )

    candidates_by_id: dict[str, list[PackageCandidate]] = {}
    for candidate in verified_graph.selected_candidates:
        if not isinstance(candidate, PackageCandidate):
            diagnostics.append(
                _diagnostic(
                    "artifact.input.candidate-invalid",
                    "/package",
                    "verified candidate set contains an invalid record",
                )
            )
            continue
        candidates_by_id.setdefault(candidate.identity, []).append(candidate)
    lock_nodes = {node["id"]: node for node in normalized_lock["nodes"]}

    host_packages = {
        package.package.package_id: package for package in host_plan.packages
    }
    source_packages: dict[str, source_build.SourcePackageBuildBinding] = {}
    verified_candidates: dict[str, PackageCandidate] = {}
    for package in source_plan.packages:
        if package.package_id in source_packages:
            diagnostics.append(
                _diagnostic(
                    "artifact.input.duplicate-package",
                    "/package",
                    f"Source Build Plan repeats package '{package.package_id}'",
                )
            )
            continue
        source_packages[package.package_id] = package
        host_package = host_packages.get(package.package_id)
        if host_package is None:
            diagnostics.append(
                _diagnostic(
                    "artifact.input.source-build-plan-stale",
                    "/package",
                    f"Source Build Plan contains unselected package '{package.package_id}'",
                )
            )
            continue
        if package.package_version != host_package.package.package_version:
            diagnostics.append(
                _diagnostic(
                    "artifact.input.package-version-mismatch",
                    "/package/version",
                    f"package '{package.package_id}' version does not match Host Composition",
                )
            )
        source_modules = {module.module_id for module in package.modules}
        host_modules = {module.module_id for module in host_package.modules}
        if source_modules != host_modules:
            diagnostics.append(
                _diagnostic(
                    "artifact.input.module-selection-mismatch",
                    "/modules",
                    f"package '{package.package_id}' module selection is stale",
                )
            )

        matching = candidates_by_id.get(package.package_id, [])
        node = lock_nodes.get(package.package_id)
        if len(matching) != 1 or node is None:
            diagnostics.append(
                _diagnostic(
                    "artifact.input.unverified",
                    "/package",
                    f"package '{package.package_id}' has no unique verified candidate",
                )
            )
            continue
        candidate = matching[0]
        if (
            candidate.version != package.package_version
            or candidate.version != node["version"]
            or candidate.package_kind != node["packageKind"]
            or candidate.source != node["source"]
            or candidate.manifest_integrity != node["manifestIntegrity"]
            or candidate.payload_integrity != node["payloadIntegrity"]
        ):
            diagnostics.append(
                _diagnostic(
                    "artifact.input.unverified",
                    "/package",
                    f"candidate evidence no longer matches '{package.package_id}'",
                )
            )
            continue
        if package.modules:
            declaration_diagnostics = _candidate_product_declaration_diagnostics(
                candidate,
                validators,
            )
            if not declaration_diagnostics:
                assert isinstance(candidate.product_declaration, dict)
                declaration_module_ids = {
                    module["moduleId"]
                    for module in candidate.product_declaration["modules"]
                }
                for module in package.modules:
                    if module.module_id not in declaration_module_ids:
                        declaration_diagnostics.append(
                            _diagnostic(
                                "artifact.product-declaration.missing-selected-module",
                                "/modules",
                                (
                                    f"selected module '{package.package_id}:"
                                    f"{module.module_id}' is absent from its declaration"
                                ),
                            )
                        )
            diagnostics.extend(declaration_diagnostics)
            if not declaration_diagnostics:
                verified_candidates[package.package_id] = candidate

    for package_id in sorted(set(host_packages) - set(source_packages), key=_utf8_key):
        diagnostics.append(
            _diagnostic(
                "artifact.input.source-build-plan-stale",
                "/package",
                f"Source Build Plan omits selected package '{package_id}'",
            )
        )

    if diagnostics:
        return None, None, {}, diagnostics
    return host_plan, source_plan, verified_candidates, []


def _path_registration_diagnostic(
    path: str,
    owner: str,
    package_paths: dict[str, tuple[str, str]],
) -> contracts.Diagnostic | None:
    folded = path.casefold()
    previous = package_paths.get(folded)
    if previous is not None:
        previous_path, previous_owner = previous
        code = "artifact.path.duplicate" if previous_path == path else "artifact.path.collision"
        pair = sorted(
            ((previous_path, previous_owner), (path, owner)),
            key=lambda value: (_utf8_key(value[0]), _utf8_key(value[1])),
        )
        return _diagnostic(
            code,
            "/modules",
            (
                f"artifact path '{pair[0][0]}' from '{pair[0][1]}' collides with "
                f"'{pair[1][0]}' from '{pair[1][1]}'"
            ),
        )

    segments = tuple(segment.casefold() for segment in path.split("/"))
    for previous_path, previous_owner in package_paths.values():
        previous_segments = tuple(
            segment.casefold() for segment in previous_path.split("/")
        )
        shared_length = min(len(segments), len(previous_segments))
        if (
            segments[:shared_length] == previous_segments[:shared_length]
            and len(segments) != len(previous_segments)
        ):
            if len(previous_segments) < len(segments):
                ancestor = (previous_path, previous_owner)
                descendant = (path, owner)
            else:
                ancestor = (path, owner)
                descendant = (previous_path, previous_owner)
            return _diagnostic(
                "artifact.path.ancestor-collision",
                "/modules",
                (
                    f"artifact path '{ancestor[0]}' from '{ancestor[1]}' conflicts "
                    f"with descendant '{descendant[0]}' from '{descendant[1]}'"
                ),
            )

    package_paths[folded] = (path, owner)
    return None


def _validate_collected_files(
    observation: _CollectedProductArtifactEvidence,
    package_paths: dict[str, tuple[str, str]],
) -> tuple[tuple[ArtifactFileEvidence, ...], list[contracts.Diagnostic]]:
    diagnostics: list[contracts.Diagnostic] = []
    if not isinstance(observation.files, tuple) or not observation.files:
        return (), [
            _diagnostic(
                "artifact.observation.files-invalid",
                "/modules",
                f"product '{observation.module_id}:{observation.product_id}' needs files",
            )
        ]

    evidence: list[ArtifactFileEvidence] = []
    primary_count = 0
    typed_files: list[ArtifactFileEvidence] = []
    for artifact in observation.files:
        if not isinstance(artifact, ArtifactFileEvidence):
            diagnostics.append(
                _diagnostic(
                    "artifact.observation.file-invalid",
                    "/modules",
                    "product observation contains an invalid file record",
                )
            )
            continue
        typed_files.append(artifact)

    for artifact in sorted(
        typed_files,
        key=lambda value: (
            0 if isinstance(value.path, str) else 1,
            _utf8_key(value.path) if isinstance(value.path, str) else b"",
            value.role if isinstance(value.role, str) else "",
            value.media_type if isinstance(value.media_type, str) else "",
        ),
    ):
        file_diagnostic_count = len(diagnostics)
        if not is_portable_artifact_path(artifact.path):
            diagnostics.append(
                _diagnostic(
                    "artifact.path.invalid",
                    "/modules",
                    f"artifact path '{artifact.path}' is not a portable relative path",
                )
            )
        else:
            owner = f"{observation.module_id}:{observation.product_id}"
            path_diagnostic = _path_registration_diagnostic(
                artifact.path,
                owner,
                package_paths,
            )
            if path_diagnostic is not None:
                diagnostics.append(path_diagnostic)

        if not isinstance(artifact.role, str) or artifact.role not in ARTIFACT_FILE_ROLES:
            diagnostics.append(
                _diagnostic(
                    "artifact.role.invalid",
                    "/modules",
                    f"artifact file role '{artifact.role}' is not supported",
                )
            )
        elif artifact.role == "primary":
            primary_count += 1
        if not isinstance(artifact.media_type, str) or not MEDIA_TYPE_PATTERN.fullmatch(
            artifact.media_type
        ):
            diagnostics.append(
                _diagnostic(
                    "artifact.media-type.invalid",
                    "/modules",
                    f"artifact media type '{artifact.media_type}' is invalid",
                )
            )
        if (
            isinstance(artifact.size, bool)
            or not isinstance(artifact.size, int)
            or artifact.size < 0
        ):
            diagnostics.append(
                _diagnostic(
                    "artifact.size.invalid",
                    "/modules",
                    "artifact size must be a non-negative integer",
                )
            )
        if not isinstance(artifact.integrity, IntegrityRecord):
            diagnostics.append(
                _diagnostic(
                    "artifact.integrity.invalid",
                    "/modules",
                    f"artifact '{artifact.path}' has no structured integrity",
                )
            )
            continue
        if (
            artifact.integrity.algorithm != "sha256"
            or re.fullmatch(r"[0-9a-f]{64}", artifact.integrity.digest) is None
        ):
            diagnostics.append(
                _diagnostic(
                    "artifact.integrity.invalid",
                    "/modules",
                    f"artifact '{artifact.path}' integrity is not canonical SHA-256",
                )
            )
        if len(diagnostics) == file_diagnostic_count:
            evidence.append(
                ArtifactFileEvidence(
                    path=artifact.path,
                    role=artifact.role,
                    media_type=artifact.media_type,
                    size=artifact.size,
                    integrity=artifact.integrity,
                )
            )

    if primary_count != 1:
        diagnostics.append(
            _diagnostic(
                "artifact.primary.cardinality",
                "/modules",
                (
                    f"product '{observation.module_id}:{observation.product_id}' "
                    "must contain exactly one primary file"
                ),
            )
        )
    return (
        tuple(sorted(evidence, key=lambda value: (_utf8_key(value.path), value.role))),
        diagnostics,
    )


def _collect_exact_byte_evidence(
    observation: ProductArtifactObservation,
) -> tuple[_CollectedProductArtifactEvidence | None, list[contracts.Diagnostic]]:
    if not isinstance(observation.files, tuple) or not observation.files:
        return None, [
            _diagnostic(
                "artifact.observation.files-invalid",
                "/modules",
                f"product '{observation.module_id}:{observation.product_id}' needs files",
            )
        ]

    diagnostics: list[contracts.Diagnostic] = []
    evidence: list[ArtifactFileEvidence] = []
    for artifact in observation.files:
        if not isinstance(artifact, ArtifactFileObservation):
            diagnostics.append(
                _diagnostic(
                    "artifact.observation.file-invalid",
                    "/modules",
                    "product observation contains an invalid file record",
                )
            )
            continue
        if not isinstance(artifact.content, bytes):
            diagnostics.append(
                _diagnostic(
                    "artifact.content.invalid",
                    "/modules",
                    "artifact content must be immutable bytes",
                )
            )
            continue
        if (
            isinstance(artifact.size, bool)
            or not isinstance(artifact.size, int)
            or artifact.size < 0
        ):
            diagnostics.append(
                _diagnostic(
                    "artifact.size.invalid",
                    "/modules",
                    "artifact size must be a non-negative integer",
                )
            )
            continue
        if artifact.size != len(artifact.content):
            diagnostics.append(
                _diagnostic(
                    "artifact.size.mismatch",
                    "/modules",
                    f"artifact '{artifact.path}' size does not match its bytes",
                )
            )
            continue
        if not isinstance(artifact.integrity, IntegrityRecord):
            diagnostics.append(
                _diagnostic(
                    "artifact.integrity.invalid",
                    "/modules",
                    f"artifact '{artifact.path}' has no structured integrity",
                )
            )
            continue
        actual_integrity = contracts.compute_bytes_integrity(artifact.content)
        if _integrity_data(artifact.integrity) != actual_integrity:
            diagnostics.append(
                _diagnostic(
                    "artifact.integrity.mismatch",
                    "/modules",
                    f"artifact '{artifact.path}' digest does not match its bytes",
                )
            )
            continue
        evidence.append(
            ArtifactFileEvidence(
                path=artifact.path,
                role=artifact.role,
                media_type=artifact.media_type,
                size=artifact.size,
                integrity=artifact.integrity,
            )
        )

    if diagnostics:
        return None, diagnostics
    return (
        _CollectedProductArtifactEvidence(
            package_id=observation.package_id,
            package_version=observation.package_version,
            module_id=observation.module_id,
            product_id=observation.product_id,
            target_platform=observation.target_platform,
            configuration=observation.configuration,
            files=tuple(evidence),
        ),
        [],
    )


def _verify_collected_with_inputs(
    verified_source_plan: source_build.SourceBuildPlan,
    candidates_by_id: dict[str, PackageCandidate],
    observation_values: tuple[_CollectedProductArtifactEvidence, ...],
    validators: contracts.ContractValidators,
) -> PackageArtifactVerificationResult:
    diagnostics: list[contracts.Diagnostic] = []
    observation_diagnostics: list[contracts.Diagnostic] = []
    for observation in observation_values:
        for field_name, value in (
            ("packageId", observation.package_id),
            ("packageVersion", observation.package_version),
            ("moduleId", observation.module_id),
            ("productId", observation.product_id),
            ("targetPlatform", observation.target_platform),
            ("configuration", observation.configuration),
        ):
            if not isinstance(value, str) or not value:
                observation_diagnostics.append(
                    _diagnostic(
                        "artifact.observation.identity-invalid",
                        "/modules",
                        f"artifact observation {field_name} must be a non-empty string",
                    )
                )
    if observation_diagnostics:
        return _failure(observation_diagnostics)

    observations_by_key: dict[
        tuple[str, str, str, str], list[_CollectedProductArtifactEvidence]
    ] = {}
    for observation in observation_values:
        key = (
            observation.package_id,
            observation.package_version,
            observation.module_id,
            observation.product_id,
        )
        observations_by_key.setdefault(key, []).append(observation)

    expected_keys: set[tuple[str, str, str, str]] = set()
    manifests: list[PackageArtifactManifest] = []
    plan_integrity = verified_source_plan.integrity
    host_integrity = verified_source_plan.inputs.host_composition_integrity

    for package in sorted(
        verified_source_plan.packages,
        key=lambda value: _utf8_key(value.package_id),
    ):
        candidate = candidates_by_id.get(package.package_id)
        if candidate is None:
            if package.modules:
                diagnostics.append(
                    _diagnostic(
                        "artifact.product-declaration.missing",
                        "/provenance/productDeclarationIntegrity",
                        f"selected package '{package.package_id}' has no declaration",
                    )
                )
            continue
        assert isinstance(candidate.product_declaration, dict)
        assert isinstance(candidate.product_declaration_integrity, dict)
        declaration_modules = {
            module["moduleId"]: module
            for module in contracts.normalize_package_product_declaration(
                candidate.product_declaration
            )["modules"]
        }
        module_evidence: list[ModuleArtifactEvidence] = []
        package_paths: dict[str, tuple[str, str]] = {}

        for source_module in sorted(
            package.modules,
            key=lambda value: _utf8_key(value.module_id),
        ):
            declared_module = declaration_modules[source_module.module_id]
            delivery = declared_module["delivery"]
            products: list[ProductArtifactEvidence] = []
            if delivery["kind"] == "artifact-set":
                for product in delivery["products"]:
                    key = (
                        package.package_id,
                        package.package_version,
                        source_module.module_id,
                        product["id"],
                    )
                    expected_keys.add(key)
                    matching = observations_by_key.get(key, [])
                    if len(matching) != 1:
                        code = (
                            "artifact.product.missing"
                            if not matching
                            else "artifact.product.duplicate"
                        )
                        diagnostics.append(
                            _diagnostic(
                                code,
                                "/modules",
                                (
                                    f"declared product '{package.package_id}:"
                                    f"{source_module.module_id}:{product['id']}' must have "
                                    "exactly one observation"
                                ),
                            )
                        )
                        continue
                    observation = matching[0]
                    if (
                        observation.target_platform
                        != verified_source_plan.target_platform
                        or observation.configuration
                        != verified_source_plan.configuration
                    ):
                        diagnostics.append(
                            _diagnostic(
                                "artifact.observation.context-mismatch",
                                "/context",
                                (
                                    f"observation for '{package.package_id}:"
                                    f"{source_module.module_id}:{product['id']}' has a "
                                    "different target/configuration"
                                ),
                            )
                        )
                    files, file_diagnostics = _validate_collected_files(
                        observation,
                        package_paths,
                    )
                    diagnostics.extend(file_diagnostics)
                    products.append(
                        ProductArtifactEvidence(
                            product_id=product["id"],
                            purpose=product["purpose"],
                            files=files,
                        )
                    )
            module_evidence.append(
                ModuleArtifactEvidence(
                    module_id=source_module.module_id,
                    delivery_kind=delivery["kind"],
                    products=tuple(products),
                )
            )

        declaration_integrity = _integrity_record(
            candidate.product_declaration_integrity
        )
        manifest = PackageArtifactManifest(
            package_id=package.package_id,
            package_version=package.package_version,
            host_kind=verified_source_plan.host_kind,
            target_platform=verified_source_plan.target_platform,
            configuration=verified_source_plan.configuration,
            provenance=PackageArtifactProvenance(
                host_composition_integrity=host_integrity,
                source_build_plan_integrity=plan_integrity,
                product_declaration_integrity=declaration_integrity,
            ),
            modules=tuple(module_evidence),
            integrity=IntegrityRecord("sha256", "0" * 64),
        )
        manifest = replace(
            manifest,
            integrity=_integrity_record(
                compute_package_artifact_manifest_integrity(manifest)
            ),
        )
        manifests.append(manifest)

    for key in sorted(
        set(observations_by_key) - expected_keys,
        key=lambda value: tuple(_utf8_key(part) for part in value),
    ):
        diagnostics.append(
            _diagnostic(
                "artifact.product.unknown",
                "/modules",
                f"artifact observation references undeclared product '{':'.join(key)}'",
            )
        )

    if diagnostics:
        return _failure(diagnostics)
    ordered_manifests = tuple(
        sorted(manifests, key=lambda value: _utf8_key(value.package_id))
    )
    output_diagnostics: list[contracts.Diagnostic] = []
    for manifest in ordered_manifests:
        output_diagnostics.extend(
            validate_package_artifact_manifest_data(manifest, validators)
        )
    if output_diagnostics:
        return _failure(output_diagnostics)
    set_integrity = _integrity_record(
        contracts.compute_bytes_integrity(
            render_package_artifact_manifest_set(ordered_manifests).encode("utf-8")
        )
    )
    return PackageArtifactVerificationResult(
        manifests=ordered_manifests,
        manifest_set_integrity=set_integrity,
        diagnostics=(),
    )


def _verify_collected_package_artifacts(
    host_plan: Any,
    source_plan: Any,
    verified_graph: Any,
    observations: Iterable[_CollectedProductArtifactEvidence],
    validators: contracts.ContractValidators,
) -> PackageArtifactVerificationResult:
    """Verify descriptors already rehashed from an owned staging generation."""

    (
        verified_host_plan,
        verified_source_plan,
        candidates_by_id,
        diagnostics,
    ) = _verify_control_plane_inputs(
        host_plan,
        source_plan,
        verified_graph,
        validators,
    )
    if diagnostics:
        return _failure(diagnostics)
    assert verified_host_plan is not None
    assert verified_source_plan is not None

    try:
        observation_values = tuple(observations)
    except TypeError:
        return _failure(
            [
                _diagnostic(
                    "artifact.observation.input-invalid",
                    "/modules",
                    "collected artifact evidence must be an iterable snapshot",
                )
            ]
        )
    if any(
        not isinstance(observation, _CollectedProductArtifactEvidence)
        for observation in observation_values
    ):
        return _failure(
            [
                _diagnostic(
                    "artifact.observation.input-invalid",
                    "/modules",
                    "collected artifact evidence must use the owned typed handoff",
                )
            ]
        )
    return _verify_collected_with_inputs(
        verified_source_plan,
        candidates_by_id,
        observation_values,
        validators,
    )


def verify_package_artifacts(
    host_plan: Any,
    source_plan: Any,
    verified_graph: Any,
    observations: Iterable[ProductArtifactObservation],
    validators: contracts.ContractValidators,
) -> PackageArtifactVerificationResult:
    """Verify exact bytes into canonical per-package manifests without IO."""

    (
        verified_host_plan,
        verified_source_plan,
        candidates_by_id,
        diagnostics,
    ) = _verify_control_plane_inputs(
        host_plan,
        source_plan,
        verified_graph,
        validators,
    )
    if diagnostics:
        return _failure(diagnostics)
    assert verified_host_plan is not None
    assert verified_source_plan is not None

    try:
        observation_values = tuple(observations)
    except TypeError:
        return _failure(
            [
                _diagnostic(
                    "artifact.observation.input-invalid",
                    "/modules",
                    "artifact observations must be an iterable snapshot",
                )
            ]
        )
    if any(
        not isinstance(observation, ProductArtifactObservation)
        for observation in observation_values
    ):
        return _failure(
            [
                _diagnostic(
                    "artifact.observation.input-invalid",
                    "/modules",
                    "every artifact observation must use the typed immutable contract",
                )
            ]
        )

    collected: list[_CollectedProductArtifactEvidence] = []
    observation_diagnostics: list[contracts.Diagnostic] = []
    for observation in observation_values:
        evidence, evidence_diagnostics = _collect_exact_byte_evidence(observation)
        observation_diagnostics.extend(evidence_diagnostics)
        if evidence is not None:
            collected.append(evidence)
    if observation_diagnostics:
        return _failure(observation_diagnostics)

    return _verify_collected_with_inputs(
        verified_source_plan,
        candidates_by_id,
        tuple(collected),
        validators,
    )
