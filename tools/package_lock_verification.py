"""Read-only verification and reuse of an existing Asharia package lock graph."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools.package_candidates import PackageCandidate


@dataclass(frozen=True)
class LockedGraphVerificationResult:
    """Atomic result: either one reusable exact graph or stable diagnostics."""

    lock: dict[str, Any] | None
    selected_candidates: tuple[PackageCandidate, ...]
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.lock is not None and not self.diagnostics


def _diagnostic_sort_key(
    diagnostic: contracts.Diagnostic,
) -> tuple[str, str, str, str]:
    return (
        diagnostic.manifest_path,
        diagnostic.pointer,
        diagnostic.code,
        diagnostic.message,
    )


def _failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> LockedGraphVerificationResult:
    return LockedGraphVerificationResult(
        lock=None,
        selected_candidates=(),
        diagnostics=tuple(sorted(diagnostics, key=_diagnostic_sort_key)),
    )


def _lock_diagnostic(
    code: str,
    pointer: str,
    message: str,
) -> contracts.Diagnostic:
    return contracts.Diagnostic(
        code=code,
        manifest_path=contracts.PACKAGE_LOCK_NAME,
        pointer=pointer,
        message=message,
    )


def _stable_json(value: Any) -> str | None:
    try:
        return json.dumps(value, sort_keys=True, separators=(",", ":"))
    except (TypeError, ValueError):
        return None


def _validate_build_descriptor_snapshot(
    candidate: PackageCandidate,
    validators: contracts.ContractValidators,
) -> list[contracts.Diagnostic]:
    descriptor_path = f"candidate/{candidate.identity}/{contracts.PACKAGE_SOURCE_BUILD_NAME}"
    values = (
        candidate.build_descriptor,
        candidate.build_descriptor_integrity,
        candidate.build_descriptor_bytes,
    )
    present_count = sum(value is not None for value in values)
    diagnostics: list[contracts.Diagnostic] = []
    if present_count not in {0, 3}:
        diagnostics.append(
            contracts.Diagnostic(
                code="lock.candidate.build-descriptor-incomplete",
                manifest_path=descriptor_path,
                pointer="",
                message="candidate build descriptor snapshot must be wholly present or absent",
            )
        )
        return diagnostics

    root = candidate.payload_location
    if not isinstance(root, Path):
        return diagnostics
    on_disk_path = root / contracts.PACKAGE_SOURCE_BUILD_NAME
    try:
        on_disk_bytes = on_disk_path.read_bytes() if on_disk_path.is_file() else None
    except OSError:
        on_disk_bytes = None

    if present_count == 0:
        if on_disk_path.exists():
            diagnostics.append(
                contracts.Diagnostic(
                    code="lock.candidate.build-descriptor-mismatch",
                    manifest_path=descriptor_path,
                    pointer="",
                    message="payload contains a build descriptor absent from candidate snapshot",
                )
            )
        return diagnostics

    assert isinstance(candidate.build_descriptor, dict)
    assert isinstance(candidate.build_descriptor_integrity, dict)
    assert isinstance(candidate.build_descriptor_bytes, bytes)
    if contracts.compute_bytes_integrity(
        candidate.build_descriptor_bytes
    ) != candidate.build_descriptor_integrity:
        diagnostics.append(
            contracts.Diagnostic(
                code="lock.candidate.build-descriptor-integrity-mismatch",
                manifest_path=descriptor_path,
                pointer="",
                message="candidate build descriptor bytes do not match their integrity",
            )
        )
    try:
        parsed = json.loads(candidate.build_descriptor_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        parsed = None
    if parsed != candidate.build_descriptor:
        diagnostics.append(
            contracts.Diagnostic(
                code="lock.candidate.build-descriptor-snapshot-mismatch",
                manifest_path=descriptor_path,
                pointer="",
                message="candidate build descriptor data does not match captured bytes",
            )
        )
    diagnostics.extend(
        contracts.validate_package_source_build_binding(
            candidate.build_descriptor,
            candidate.manifest,
            validators,
            descriptor_path=descriptor_path,
            manifest_path=f"candidate/{candidate.identity}/{contracts.PACKAGE_MANIFEST_NAME}",
        )
    )
    if on_disk_bytes != candidate.build_descriptor_bytes:
        diagnostics.append(
            contracts.Diagnostic(
                code="lock.candidate.build-descriptor-mismatch",
                manifest_path=descriptor_path,
                pointer="",
                message="payload build descriptor bytes changed after discovery",
            )
        )
    return diagnostics


def _validate_product_declaration_snapshot(
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
    diagnostics: list[contracts.Diagnostic] = []
    if present_count not in {0, 3}:
        return [
            contracts.Diagnostic(
                code="lock.candidate.product-declaration-incomplete",
                manifest_path=declaration_path,
                pointer="",
                message="candidate product declaration snapshot must be wholly present or absent",
            )
        ]

    root = candidate.payload_location
    if not isinstance(root, Path):
        return diagnostics
    on_disk_path = root / contracts.PACKAGE_PRODUCTS_NAME
    try:
        on_disk_bytes = on_disk_path.read_bytes() if on_disk_path.is_file() else None
    except OSError:
        on_disk_bytes = None

    if present_count == 0:
        if on_disk_path.exists():
            diagnostics.append(
                contracts.Diagnostic(
                    code="lock.candidate.product-declaration-mismatch",
                    manifest_path=declaration_path,
                    pointer="",
                    message=(
                        "payload contains a product declaration absent from candidate snapshot"
                    ),
                )
            )
        return diagnostics

    assert isinstance(candidate.product_declaration, dict)
    assert isinstance(candidate.product_declaration_integrity, dict)
    assert isinstance(candidate.product_declaration_bytes, bytes)
    if contracts.compute_bytes_integrity(
        candidate.product_declaration_bytes
    ) != candidate.product_declaration_integrity:
        diagnostics.append(
            contracts.Diagnostic(
                code="lock.candidate.product-declaration-integrity-mismatch",
                manifest_path=declaration_path,
                pointer="",
                message="candidate product declaration bytes do not match their integrity",
            )
        )
    try:
        parsed = json.loads(candidate.product_declaration_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        parsed = None
    if parsed != candidate.product_declaration:
        diagnostics.append(
            contracts.Diagnostic(
                code="lock.candidate.product-declaration-snapshot-mismatch",
                manifest_path=declaration_path,
                pointer="",
                message="candidate product declaration data does not match captured bytes",
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
    if on_disk_bytes != candidate.product_declaration_bytes:
        diagnostics.append(
            contracts.Diagnostic(
                code="lock.candidate.product-declaration-mismatch",
                manifest_path=declaration_path,
                pointer="",
                message="payload product declaration bytes changed after discovery",
            )
        )
    return diagnostics


def _validate_factory_declaration_snapshot(
    candidate: PackageCandidate,
    validators: contracts.ContractValidators,
) -> list[contracts.Diagnostic]:
    declaration_path = f"candidate/{candidate.identity}/{contracts.PACKAGE_FACTORIES_NAME}"
    values = (
        candidate.factory_declaration,
        candidate.factory_declaration_integrity,
        candidate.factory_declaration_bytes,
    )
    present_count = sum(value is not None for value in values)
    diagnostics: list[contracts.Diagnostic] = []
    if present_count not in {0, 3}:
        return [
            contracts.Diagnostic(
                code="lock.candidate.factory-declaration-incomplete",
                manifest_path=declaration_path,
                pointer="",
                message="candidate factory declaration snapshot must be wholly present or absent",
            )
        ]

    root = candidate.payload_location
    if not isinstance(root, Path):
        return diagnostics
    on_disk_path = root / contracts.PACKAGE_FACTORIES_NAME
    try:
        on_disk_bytes = on_disk_path.read_bytes() if on_disk_path.is_file() else None
    except OSError:
        on_disk_bytes = None

    if present_count == 0:
        if on_disk_path.exists():
            diagnostics.append(
                contracts.Diagnostic(
                    code="lock.candidate.factory-declaration-mismatch",
                    manifest_path=declaration_path,
                    pointer="",
                    message=(
                        "payload contains a factory declaration absent from candidate snapshot"
                    ),
                )
            )
        return diagnostics

    assert isinstance(candidate.factory_declaration, dict)
    assert isinstance(candidate.factory_declaration_integrity, dict)
    assert isinstance(candidate.factory_declaration_bytes, bytes)
    if contracts.compute_bytes_integrity(
        candidate.factory_declaration_bytes
    ) != candidate.factory_declaration_integrity:
        diagnostics.append(
            contracts.Diagnostic(
                code="lock.candidate.factory-declaration-integrity-mismatch",
                manifest_path=declaration_path,
                pointer="",
                message="candidate factory declaration bytes do not match their integrity",
            )
        )
    try:
        parsed = json.loads(candidate.factory_declaration_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        parsed = None
    if parsed != candidate.factory_declaration:
        diagnostics.append(
            contracts.Diagnostic(
                code="lock.candidate.factory-declaration-snapshot-mismatch",
                manifest_path=declaration_path,
                pointer="",
                message="candidate factory declaration data does not match captured bytes",
            )
        )
    diagnostics.extend(
        contracts.validate_package_factory_declaration_binding(
            candidate.factory_declaration,
            candidate.manifest,
            validators,
            declaration_path=declaration_path,
            manifest_path=f"candidate/{candidate.identity}/{contracts.PACKAGE_MANIFEST_NAME}",
        )
    )
    if on_disk_bytes != candidate.factory_declaration_bytes:
        diagnostics.append(
            contracts.Diagnostic(
                code="lock.candidate.factory-declaration-mismatch",
                manifest_path=declaration_path,
                pointer="",
                message="payload factory declaration bytes changed after discovery",
            )
        )
    return diagnostics


def _bind_locked_candidates(
    lock: dict[str, Any],
    distribution: dict[str, Any],
    candidates: tuple[PackageCandidate, ...],
    validators: contracts.ContractValidators,
) -> tuple[tuple[PackageCandidate, ...], list[contracts.Diagnostic]]:
    inventory = {
        package["id"]: package for package in distribution["bundledPackages"]
    }
    candidates_by_source: dict[str, list[PackageCandidate]] = {}
    for candidate in candidates:
        source_key = _stable_json(candidate.source)
        if source_key is not None:
            candidates_by_source.setdefault(source_key, []).append(candidate)

    selected: list[PackageCandidate] = []
    diagnostics: list[contracts.Diagnostic] = []
    for candidate in candidates:
        source_kind = (
            candidate.source.get("kind")
            if isinstance(candidate.source, dict)
            else None
        )
        inventory_package = inventory.get(candidate.identity)
        if inventory_package is not None and source_kind != "engine-distribution":
            diagnostics.append(
                _lock_diagnostic(
                    "lock.engine.distribution-shadowed",
                    "/nodes",
                    (
                        f"project-owned source cannot shadow Engine Distribution package "
                        f"'{candidate.identity}'"
                    ),
                )
            )
        elif inventory_package is None and source_kind == "engine-distribution":
            diagnostics.append(
                _lock_diagnostic(
                    "lock.engine.package-not-distributed",
                    "/nodes",
                    (
                        f"candidate '{candidate.identity}' is not present in the current "
                        "Engine Distribution inventory"
                    ),
                )
            )

    for node_index, node in enumerate(lock["nodes"]):
        identity = node["id"]
        node_pointer = f"/nodes/{node_index}"
        source_key = _stable_json(node["source"])
        matching = candidates_by_source.get(source_key or "", [])
        if node["source"]["kind"] == "engine-distribution":
            matching = [
                candidate for candidate in matching if candidate.identity == identity
            ]
        if not matching:
            diagnostics.append(
                _lock_diagnostic(
                    "lock.source.unavailable",
                    f"{node_pointer}/source",
                    f"selected source for '{identity}' is unavailable",
                )
            )
            continue
        if len(matching) > 1:
            diagnostics.append(
                _lock_diagnostic(
                    "lock.candidate.ambiguous-binding",
                    f"{node_pointer}/source",
                    f"selected source for '{identity}' maps to multiple candidates",
                )
            )
            continue

        candidate = matching[0]
        diagnostic_count = len(diagnostics)
        for attribute, pointer, actual, expected, code in (
            (
                "identity",
                "id",
                candidate.identity,
                node["id"],
                "lock.candidate.identity-mismatch",
            ),
            (
                "version",
                "version",
                candidate.version,
                node["version"],
                "lock.candidate.version-mismatch",
            ),
            (
                "package kind",
                "packageKind",
                candidate.package_kind,
                node["packageKind"],
                "lock.candidate.kind-mismatch",
            ),
        ):
            if actual != expected:
                diagnostics.append(
                    _lock_diagnostic(
                        code,
                        f"{node_pointer}/{pointer}",
                        (
                            f"selected source for '{identity}' reports {attribute} "
                            f"'{actual}' instead of locked value '{expected}'"
                        ),
                    )
                )

        candidate_path = f"candidate/{identity}/{contracts.PACKAGE_MANIFEST_NAME}"
        manifest_diagnostics = contracts.validate_manifest_data(
            candidate.manifest,
            candidate_path,
            validators,
        )
        diagnostics.extend(manifest_diagnostics)
        if not manifest_diagnostics:
            for attribute, actual, expected in (
                ("identity", candidate.identity, candidate.manifest["id"]),
                ("version", candidate.version, candidate.manifest["version"]),
                ("packageKind", candidate.package_kind, candidate.manifest["packageKind"]),
            ):
                if actual != expected:
                    diagnostics.append(
                        _lock_diagnostic(
                            "lock.candidate.metadata-mismatch",
                            node_pointer,
                            (
                                f"candidate {attribute} '{actual}' does not match its author "
                                f"manifest value '{expected}' for '{identity}'"
                            ),
                        )
                    )

        if node["source"]["kind"] == "engine-distribution":
            inventory_package = inventory.get(identity)
            if inventory_package is None:
                diagnostics.append(
                    _lock_diagnostic(
                        "lock.engine.package-not-distributed",
                        f"{node_pointer}/source",
                        f"locked package '{identity}' is absent from Engine Distribution",
                    )
                )
            elif (
                node["version"] != inventory_package["version"]
                or node["packageKind"] != inventory_package["packageKind"]
                or candidate.origin
                != f"engine-distribution:{inventory_package['root']}"
                or candidate.manifest_integrity
                != inventory_package["manifestIntegrity"]
                or candidate.payload_integrity != inventory_package["payloadIntegrity"]
            ):
                diagnostics.append(
                    _lock_diagnostic(
                        "lock.engine.distribution-candidate-mismatch",
                        node_pointer,
                        (
                            f"locked candidate '{identity}' does not match current Engine "
                            "Distribution inventory"
                        ),
                    )
                )
        else:
            if identity in inventory:
                diagnostics.append(
                    _lock_diagnostic(
                        "lock.engine.distribution-shadowed",
                        f"{node_pointer}/source",
                        (
                            f"locked project-owned source cannot shadow Engine Distribution "
                            f"package '{identity}'"
                        ),
                    )
                )
            if candidate.manifest_integrity != node["manifestIntegrity"]:
                diagnostics.append(
                    _lock_diagnostic(
                        "lock.integrity.manifest-mismatch",
                        f"{node_pointer}/manifestIntegrity",
                        f"candidate manifest evidence changed for '{identity}'",
                    )
                )
            if candidate.payload_integrity != node["payloadIntegrity"]:
                diagnostics.append(
                    _lock_diagnostic(
                        "lock.integrity.payload-mismatch",
                        f"{node_pointer}/payloadIntegrity",
                        f"candidate payload evidence changed for '{identity}'",
                    )
                )
        if not isinstance(candidate.origin, str) or not candidate.origin:
            diagnostics.append(
                _lock_diagnostic(
                    "lock.candidate.origin-invalid",
                    f"{node_pointer}/source",
                    f"candidate origin for '{identity}' must be a non-empty stable string",
                )
            )
        if not isinstance(candidate.payload_location, Path):
            diagnostics.append(
                _lock_diagnostic(
                    "lock.source.unavailable",
                    f"{node_pointer}/source",
                    f"selected source for '{identity}' has no verifiable payload location",
                )
            )
        else:
            diagnostics.extend(
                _validate_build_descriptor_snapshot(candidate, validators)
            )
            diagnostics.extend(
                _validate_product_declaration_snapshot(candidate, validators)
            )
            diagnostics.extend(
                _validate_factory_declaration_snapshot(candidate, validators)
            )

        if len(diagnostics) == diagnostic_count:
            selected.append(candidate)

    return tuple(selected), diagnostics


def verify_locked_package_graph(
    project: Any,
    distribution: Any,
    existing_lock: Any,
    candidates: Iterable[PackageCandidate],
    validators: contracts.ContractValidators,
) -> LockedGraphVerificationResult:
    """Prove that an existing exact lock graph is reusable without resolving or writing."""

    diagnostics: list[contracts.Diagnostic] = []
    diagnostics.extend(
        contracts.validate_manifest_data(
            project,
            contracts.PROJECT_MANIFEST_NAME,
            validators,
        )
    )
    diagnostics.extend(
        contracts.validate_manifest_data(
            distribution,
            contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME,
            validators,
        )
    )
    if existing_lock is None:
        diagnostics.append(
            _lock_diagnostic(
                "lock.input.missing",
                "",
                "an existing package lock is required for locked verification",
            )
        )
    else:
        diagnostics.extend(
            contracts.validate_manifest_data(
                existing_lock,
                contracts.PACKAGE_LOCK_NAME,
                validators,
            )
        )

    try:
        candidate_snapshot = tuple(candidates)
    except TypeError:
        candidate_snapshot = ()
        diagnostics.append(
            _lock_diagnostic(
                "lock.input.candidates-invalid",
                "",
                "package candidates must be iterable",
            )
        )
    if any(not isinstance(candidate, PackageCandidate) for candidate in candidate_snapshot):
        diagnostics.append(
            _lock_diagnostic(
                "lock.input.candidates-invalid",
                "",
                "every package candidate must use the shared PackageCandidate contract",
            )
        )

    if diagnostics:
        return _failure(diagnostics)

    assert isinstance(existing_lock, dict)
    normalized_lock = contracts.normalize_lock_manifest(existing_lock)
    normalized_distribution = contracts.normalize_engine_distribution_manifest(
        distribution
    )
    engine = normalized_distribution["distribution"]
    project_engine = project["engine"]
    if project_engine["distributionId"] != engine["id"]:
        diagnostics.append(
            _lock_diagnostic(
                "lock.engine.distribution-mismatch",
                "/engine/distributionId",
                (
                    f"project requires Distribution '{project_engine['distributionId']}' "
                    f"but current Distribution is '{engine['id']}'"
                ),
            )
        )
    if not contracts.version_satisfies_constraint(
        engine["engineApiVersion"], project_engine["apiVersion"]
    ):
        diagnostics.append(
            _lock_diagnostic(
                "lock.engine.api-incompatible",
                "/engine/apiVersion",
                (
                    f"Engine API '{engine['engineApiVersion']}' does not satisfy the "
                    "project requirement"
                ),
            )
        )
    expected_engine_input = {
        "distributionId": engine["id"],
        "engineApiVersion": engine["engineApiVersion"],
        "engineGenerationId": normalized_distribution["engineGenerationId"],
    }
    actual_engine_input = normalized_lock["inputs"]["engine"]
    for field, code in (
        ("distributionId", "lock.input.engine-distribution-stale"),
        ("engineApiVersion", "lock.input.engine-api-stale"),
        ("engineGenerationId", "lock.input.engine-generation-stale"),
    ):
        if actual_engine_input[field] != expected_engine_input[field]:
            diagnostics.append(
                _lock_diagnostic(
                    code,
                    f"/inputs/engine/{field}",
                    (
                        f"lock value '{actual_engine_input[field]}' does not match current "
                        f"Engine Distribution value '{expected_engine_input[field]}'"
                    ),
                )
            )
    if normalized_lock["inputs"][
        "projectManifestIntegrity"
    ] != contracts.compute_project_manifest_integrity(project):
        diagnostics.append(
            _lock_diagnostic(
                "lock.input.project-manifest-stale",
                "/inputs/projectManifestIntegrity",
                "lock does not match the normalized Project Manifest",
            )
        )
    if diagnostics:
        return _failure(diagnostics)

    selected_candidates, binding_diagnostics = _bind_locked_candidates(
        normalized_lock,
        normalized_distribution,
        candidate_snapshot,
        validators,
    )
    if binding_diagnostics:
        return _failure(binding_diagnostics)

    cross_document_diagnostics = contracts.validate_locked_result_data(
        normalized_lock,
        project,
        [candidate.manifest for candidate in selected_candidates],
        validators,
    )
    if cross_document_diagnostics:
        return _failure(cross_document_diagnostics)

    payload_roots = {
        candidate.identity: candidate.payload_location for candidate in selected_candidates
    }
    integrity_diagnostics = contracts.validate_locked_candidate_integrity(
        normalized_lock,
        payload_roots,
        distribution=normalized_distribution,
    )
    if integrity_diagnostics:
        return _failure(integrity_diagnostics)

    return LockedGraphVerificationResult(
        lock=normalized_lock,
        selected_candidates=selected_candidates,
        diagnostics=(),
    )
