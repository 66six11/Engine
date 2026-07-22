"""Read-only deep verification for one installed Engine Distribution generation.

This boundary diagnoses immutable installed bytes. It does not repair, download,
select, activate, build, restart, or load any Engine or project component.
"""

from __future__ import annotations

import copy
import hashlib
import json
import os
import re
import stat
import unicodedata
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import package_artifact_evidence as artifact_evidence
from tools import package_artifact_publication as artifact_publication
from tools import product_payload_policy
from tools import stable_file_identity


_HASH_CHUNK_SIZE = 1024 * 1024
_GENERATION_ID_PATTERN = re.compile(r"^sha256-[0-9a-f]{64}$")
_ARTIFACTS_DIRECTORY_NAME = "artifacts"


class DistributionHealthState(Enum):
    """Installed generation health; action policy belongs to another owner."""

    HEALTHY = "Healthy"
    REPAIR_REQUIRED = "RepairRequired"


@dataclass(frozen=True)
class InstalledDistributionVerificationRequest:
    """External trust anchor and explicit installed generation location."""

    generation_root: Path = field(repr=False)
    expected_engine_generation_id: str


@dataclass(frozen=True)
class VerifiedInstalledDistribution:
    """Process-local handoff shape emitted by complete deep verification."""

    engine_generation_id: str
    generation_root: Path = field(repr=False)
    manifest: dict[str, Any]
    manifest_bytes: bytes = field(repr=False)
    manifest_integrity: dict[str, str]


@dataclass(frozen=True)
class DistributionHealthReport:
    """Read-only observation of one requested installed generation."""

    state: DistributionHealthState
    expected_engine_generation_id: str
    observed_engine_generation_id: str | None
    verified_distribution: VerifiedInstalledDistribution | None
    findings: tuple[contracts.Diagnostic, ...]


@dataclass(frozen=True)
class InstalledDistributionVerificationResult:
    """Atomic API result; malformed requests do not produce health reports."""

    report: DistributionHealthReport | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return (
            self.report is not None
            and self.report.state is DistributionHealthState.HEALTHY
            and self.report.verified_distribution is not None
            and not self.report.findings
            and not self.diagnostics
        )


@dataclass(frozen=True)
class _FileFingerprint:
    device: int
    inode: int
    kind: int
    size: int
    modified_ns: int
    changed_ns: int


def _utf8_key(value: str) -> bytes:
    return value.encode("utf-8", errors="surrogatepass")


def _diagnostic_sort_key(
    diagnostic: contracts.Diagnostic,
) -> tuple[str, str, str, str]:
    return (
        diagnostic.manifest_path,
        diagnostic.pointer,
        diagnostic.code,
        diagnostic.message,
    )


def _diagnostic(
    code: str,
    pointer: str,
    message: str,
    *,
    manifest_path: str = contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME,
) -> contracts.Diagnostic:
    return contracts.Diagnostic(
        code=code,
        manifest_path=manifest_path,
        pointer=pointer,
        message=message,
    )


def _request_failure(
    diagnostic: contracts.Diagnostic,
) -> InstalledDistributionVerificationResult:
    return InstalledDistributionVerificationResult(
        report=None,
        diagnostics=(diagnostic,),
    )


def _report(
    expected_generation_id: str,
    findings: Iterable[contracts.Diagnostic],
    *,
    observed_generation_id: str | None = None,
    verified_distribution: VerifiedInstalledDistribution | None = None,
) -> InstalledDistributionVerificationResult:
    ordered = tuple(sorted(findings, key=_diagnostic_sort_key))
    state = (
        DistributionHealthState.HEALTHY
        if not ordered and verified_distribution is not None
        else DistributionHealthState.REPAIR_REQUIRED
    )
    return InstalledDistributionVerificationResult(
        report=DistributionHealthReport(
            state=state,
            expected_engine_generation_id=expected_generation_id,
            observed_engine_generation_id=observed_generation_id,
            verified_distribution=(
                verified_distribution
                if state is DistributionHealthState.HEALTHY
                else None
            ),
            findings=ordered,
        ),
        diagnostics=(),
    )


def _fingerprint(status: os.stat_result) -> _FileFingerprint:
    return _FileFingerprint(
        device=status.st_dev,
        inode=status.st_ino,
        kind=stable_file_identity.file_kind(status),
        size=status.st_size,
        modified_ns=status.st_mtime_ns,
        changed_ns=stable_file_identity.changed_ns(status),
    )


def _is_link_or_reparse(status: os.stat_result) -> bool:
    if stat.S_ISLNK(status.st_mode):
        return True
    reparse_attribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    return bool(getattr(status, "st_file_attributes", 0) & reparse_attribute)


def inspect_installed_engine_distribution_root(
    root: Path,
) -> tuple[Path | None, contracts.Diagnostic | None]:
    """Resolve one installed generation root without crossing links/reparse points."""

    absolute = stable_file_identity.extended_path(root)
    current = Path(absolute.anchor)
    for component in absolute.parts[1:]:
        current /= component
        try:
            status = current.lstat()
        except FileNotFoundError:
            return None, _diagnostic(
                "distribution.repair.root-missing",
                "",
                "requested Engine Distribution generation root does not exist",
            )
        except OSError as error:
            return None, _diagnostic(
                "distribution.repair.root-invalid",
                "",
                f"could not inspect requested generation root ({error.__class__.__name__})",
            )
        if _is_link_or_reparse(status):
            return None, _diagnostic(
                "distribution.repair.root-link",
                "",
                "requested generation root crosses a link/reparse point",
            )
        if not stat.S_ISDIR(status.st_mode):
            return None, _diagnostic(
                "distribution.repair.root-invalid",
                "",
                "requested generation root contains a non-directory component",
            )
    try:
        return absolute.resolve(strict=True), None
    except OSError as error:
        return None, _diagnostic(
            "distribution.repair.root-invalid",
            "",
            f"could not resolve requested generation root ({error.__class__.__name__})",
        )


def _read_regular_bytes(
    path: Path,
    relative: str,
    code_prefix: str,
    pointer: str,
) -> tuple[bytes | None, contracts.Diagnostic | None]:
    try:
        status = path.lstat()
    except FileNotFoundError:
        return None, _diagnostic(
            f"{code_prefix}-missing",
            pointer,
            f"required file '{relative}' is missing",
        )
    except OSError as error:
        return None, _diagnostic(
            f"{code_prefix}-read-failed",
            pointer,
            f"could not inspect '{relative}' ({error.__class__.__name__})",
        )
    if _is_link_or_reparse(status):
        return None, _diagnostic(
            f"{code_prefix}-link",
            pointer,
            f"required file '{relative}' is a link/reparse point",
        )
    if not stat.S_ISREG(status.st_mode):
        return None, _diagnostic(
            f"{code_prefix}-type",
            pointer,
            f"required path '{relative}' is not a regular file",
        )
    try:
        with path.open("rb") as source:
            before = _fingerprint(os.fstat(source.fileno()))
            contents = source.read()
            after = _fingerprint(os.fstat(source.fileno()))
    except OSError as error:
        return None, _diagnostic(
            f"{code_prefix}-read-failed",
            pointer,
            f"could not read '{relative}' ({error.__class__.__name__})",
        )
    if before != after or before != _fingerprint(status) or len(contents) != before.size:
        return None, _diagnostic(
            f"{code_prefix}-drift",
            pointer,
            f"required file '{relative}' changed while it was being read",
        )
    return contents, None


def _hash_regular_file(
    path: Path,
    relative: str,
    code_prefix: str,
    pointer: str,
) -> tuple[int | None, dict[str, str] | None, contracts.Diagnostic | None]:
    try:
        status = path.lstat()
    except FileNotFoundError:
        return None, None, _diagnostic(
            f"{code_prefix}-missing",
            pointer,
            f"required file '{relative}' is missing",
        )
    except OSError as error:
        return None, None, _diagnostic(
            f"{code_prefix}-read-failed",
            pointer,
            f"could not inspect '{relative}' ({error.__class__.__name__})",
        )
    if _is_link_or_reparse(status):
        return None, None, _diagnostic(
            f"{code_prefix}-link",
            pointer,
            f"required file '{relative}' is a link/reparse point",
        )
    if not stat.S_ISREG(status.st_mode):
        return None, None, _diagnostic(
            f"{code_prefix}-type",
            pointer,
            f"required path '{relative}' is not a regular file",
        )

    digest = hashlib.sha256()
    size = 0
    try:
        with path.open("rb") as source:
            before = _fingerprint(os.fstat(source.fileno()))
            while chunk := source.read(_HASH_CHUNK_SIZE):
                size += len(chunk)
                digest.update(chunk)
            after = _fingerprint(os.fstat(source.fileno()))
    except OSError as error:
        return None, None, _diagnostic(
            f"{code_prefix}-read-failed",
            pointer,
            f"could not hash '{relative}' ({error.__class__.__name__})",
        )
    if before != after or before != _fingerprint(status) or size != before.size:
        return None, None, _diagnostic(
            f"{code_prefix}-drift",
            pointer,
            f"required file '{relative}' changed while it was being hashed",
        )
    return size, {"algorithm": "sha256", "digest": digest.hexdigest()}, None


def _scan_regular_tree(
    root: Path,
) -> tuple[dict[str, _FileFingerprint], set[str], list[contracts.Diagnostic]]:
    files: dict[str, _FileFingerprint] = {}
    directories: set[str] = set()
    findings: list[contracts.Diagnostic] = []
    folded_paths: dict[str, str] = {}

    def register(relative: str) -> None:
        if unicodedata.normalize("NFC", relative) != relative:
            findings.append(
                _diagnostic(
                    "distribution.repair.layout-path-not-nfc",
                    "",
                    f"installed path '{relative}' is not Unicode NFC",
                )
            )
        previous = folded_paths.get(relative.casefold())
        if previous is not None and previous != relative:
            findings.append(
                _diagnostic(
                    "distribution.repair.layout-case-collision",
                    "",
                    f"installed paths '{previous}' and '{relative}' collide by case",
                )
            )
        else:
            folded_paths[relative.casefold()] = relative

    def visit(directory: Path, prefix: str) -> None:
        try:
            with os.scandir(directory) as iterator:
                entries = sorted(iterator, key=lambda entry: os.fsencode(entry.name))
        except OSError as error:
            findings.append(
                _diagnostic(
                    "distribution.repair.layout-scan-failed",
                    "",
                    f"could not scan installed directory '{prefix or '.'}' ({error.__class__.__name__})",
                )
            )
            return
        for entry in entries:
            relative = f"{prefix}/{entry.name}" if prefix else entry.name
            register(relative)
            try:
                status = entry.stat(follow_symlinks=False)
            except OSError as error:
                findings.append(
                    _diagnostic(
                        "distribution.repair.layout-entry-invalid",
                        "",
                        f"could not inspect installed entry '{relative}' ({error.__class__.__name__})",
                    )
                )
                continue
            if _is_link_or_reparse(status):
                findings.append(
                    _diagnostic(
                        "distribution.repair.layout-link",
                        "",
                        f"installed entry '{relative}' is a link/reparse point",
                    )
                )
            elif stat.S_ISDIR(status.st_mode):
                directories.add(relative)
                visit(Path(entry.path), relative)
            elif stat.S_ISREG(status.st_mode):
                files[relative] = _fingerprint(status)
            else:
                findings.append(
                    _diagnostic(
                        "distribution.repair.layout-special-entry",
                        "",
                        f"installed entry '{relative}' is not a regular file or directory",
                    )
                )

    visit(root, "")
    return files, directories, findings


def _parents(relative: str) -> set[str]:
    parts = relative.split("/")[:-1]
    return {"/".join(parts[:length]) for length in range(1, len(parts) + 1)}


def _python_payload_owner_and_pointer(
    manifest: dict[str, Any],
    relative: str,
) -> tuple[str, str]:
    for index, file in enumerate(manifest["editorImage"]["files"]):
        if relative == file["path"]:
            return "Editor Image", f"/editorImage/files/{index}"
    for index, package in enumerate(manifest["bundledPackages"]):
        root = package["root"]
        if relative == root or relative.startswith(f"{root}/"):
            return (
                f"bundled package '{package['id']}'",
                f"/bundledPackages/{index}",
            )
    for index, reference in enumerate(manifest["packageArtifacts"]):
        root = (
            f"{_ARTIFACTS_DIRECTORY_NAME}/{reference['artifactGenerationId']}"
            f"/packages/{reference['package']['id']}"
        )
        if relative == root or relative.startswith(f"{root}/"):
            return (
                f"artifact package '{reference['package']['id']}'",
                f"/packageArtifacts/{index}",
            )
    for index, profile in enumerate(manifest["hostProfiles"]):
        if relative == profile["path"]:
            return "Host Profile", f"/hostProfiles/{index}"
    return "installed Distribution generation", ""


def _python_product_payload_findings(
    manifest: dict[str, Any],
    matches: Iterable[product_payload_policy.ForbiddenPythonProductPayload],
) -> list[contracts.Diagnostic]:
    findings: list[contracts.Diagnostic] = []
    for match in matches:
        owner, pointer = _python_payload_owner_and_pointer(manifest, match.path)
        findings.append(
            _diagnostic(
                "distribution.repair.python-payload-forbidden",
                pointer,
                (
                    f"{owner} contains forbidden Python product payload "
                    f"'{match.path}' ({match.reason}); Python is repository-only tooling"
                ),
            )
        )
    return findings


def _manifest_trust_findings(
    generation_root: Path,
    expected_generation_id: str,
    validators: contracts.ContractValidators,
) -> tuple[dict[str, Any] | None, bytes | None, str | None, list[contracts.Diagnostic]]:
    findings: list[contracts.Diagnostic] = []
    manifest_relative = contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME
    contents, read_finding = _read_regular_bytes(
        generation_root / manifest_relative,
        manifest_relative,
        "distribution.repair.manifest",
        "",
    )
    if read_finding is not None:
        return None, None, None, [read_finding]
    assert contents is not None
    if contents.startswith(b"\xef\xbb\xbf"):
        return None, contents, None, [
            _diagnostic(
                "distribution.repair.manifest-encoding",
                "",
                "Distribution Manifest is not UTF-8 without BOM",
            )
        ]
    try:
        manifest = json.loads(contents.decode("utf-8"))
    except UnicodeDecodeError:
        return None, contents, None, [
            _diagnostic(
                "distribution.repair.manifest-encoding",
                "",
                "Distribution Manifest is not UTF-8 without BOM",
            )
        ]
    except json.JSONDecodeError:
        return None, contents, None, [
            _diagnostic(
                "distribution.repair.manifest-json",
                "",
                "Distribution Manifest is not valid JSON",
            )
        ]
    if not isinstance(manifest, dict):
        return None, contents, None, [
            _diagnostic(
                "distribution.repair.manifest-contract",
                "",
                "Distribution Manifest root must be an object",
            )
        ]

    observed = manifest.get("engineGenerationId")
    observed_generation_id = observed if isinstance(observed, str) else None
    contract_diagnostics = contracts.validate_manifest_data(
        manifest,
        manifest_relative,
        validators,
    )
    for diagnostic in contract_diagnostics:
        findings.append(
            _diagnostic(
                "distribution.repair.manifest-contract",
                diagnostic.pointer,
                f"Distribution Manifest contract failed ({diagnostic.code})",
            )
        )
    if contract_diagnostics:
        return manifest, contents, observed_generation_id, findings

    canonical_bytes = contracts.render_normalized_engine_distribution_manifest(
        manifest
    ).encode("utf-8")
    if contents != canonical_bytes:
        findings.append(
            _diagnostic(
                "distribution.repair.manifest-noncanonical",
                "",
                "Distribution Manifest bytes are not the canonical v1 representation",
            )
        )
    content_generation_id = contracts.compute_engine_generation_id(manifest)
    if observed_generation_id != expected_generation_id:
        findings.append(
            _diagnostic(
                "distribution.repair.manifest-generation-id-mismatch",
                "/engineGenerationId",
                "declared EngineGenerationId does not match the externally expected generation",
            )
        )
    if content_generation_id != expected_generation_id:
        findings.append(
            _diagnostic(
                "distribution.repair.manifest-content-id-mismatch",
                "/engineGenerationId",
                "canonical Distribution inventory does not derive the externally expected generation",
            )
        )
    return manifest, contents, observed_generation_id, findings


def _verify_editor_files(
    root: Path,
    manifest: dict[str, Any],
) -> list[contracts.Diagnostic]:
    findings: list[contracts.Diagnostic] = []
    for index, file in enumerate(manifest["editorImage"]["files"]):
        relative = file["path"]
        size, integrity, finding = _hash_regular_file(
            root.joinpath(*relative.split("/")),
            relative,
            "distribution.repair.editor-file",
            f"/editorImage/files/{index}",
        )
        if finding is not None:
            findings.append(finding)
        elif size != file["size"] or integrity != file["integrity"]:
            findings.append(
                _diagnostic(
                    "distribution.repair.editor-integrity",
                    f"/editorImage/files/{index}",
                    f"Editor Image file '{relative}' does not match declared size/integrity",
                )
            )
    return findings


def _verify_bundled_packages(
    root: Path,
    manifest: dict[str, Any],
    validators: contracts.ContractValidators,
    actual_files: dict[str, _FileFingerprint],
    actual_directories: set[str],
) -> list[contracts.Diagnostic]:
    findings: list[contracts.Diagnostic] = []
    for index, package in enumerate(manifest["bundledPackages"]):
        root_relative = package["root"]
        package_root = root.joinpath(*root_relative.split("/"))
        pointer = f"/bundledPackages/{index}"
        manifest_relative = f"{root_relative}/{contracts.PACKAGE_MANIFEST_NAME}"
        exact_bytes, read_finding = _read_regular_bytes(
            package_root / contracts.PACKAGE_MANIFEST_NAME,
            manifest_relative,
            "distribution.repair.package-manifest",
            pointer,
        )
        if read_finding is not None:
            findings.append(read_finding)
        else:
            assert exact_bytes is not None
            if contracts.compute_bytes_integrity(exact_bytes) != package["manifestIntegrity"]:
                findings.append(
                    _diagnostic(
                        "distribution.repair.package-manifest-integrity",
                        pointer,
                        f"bundled package '{package['id']}' author manifest integrity differs",
                    )
                )
            try:
                package_data = json.loads(exact_bytes.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                findings.append(
                    _diagnostic(
                        "distribution.repair.package-manifest-invalid",
                        pointer,
                        f"bundled package '{package['id']}' author manifest is not UTF-8 JSON",
                    )
                )
            else:
                package_diagnostics = contracts.validate_manifest_data(
                    package_data,
                    manifest_relative,
                    validators,
                )
                if package_diagnostics:
                    for diagnostic in package_diagnostics:
                        findings.append(
                            _diagnostic(
                                "distribution.repair.package-contract",
                                diagnostic.pointer,
                                f"bundled package '{package['id']}' contract failed ({diagnostic.code})",
                                manifest_path=manifest_relative,
                            )
                        )
                elif (
                    package_data["id"] != package["id"]
                    or package_data["version"] != package["version"]
                    or package_data["packageKind"] != package["packageKind"]
                ):
                    findings.append(
                        _diagnostic(
                            "distribution.repair.package-identity",
                            pointer,
                            f"bundled package '{package['id']}' inventory identity differs from its author manifest",
                        )
                    )

        excluded = sorted(
            name
            for name in contracts.PACKAGE_TREE_ROOT_EXCLUDES
            if f"{root_relative}/{name}" in actual_directories
            or f"{root_relative}/{name}" in actual_files
            or any(
                relative.startswith(f"{root_relative}/{name}/")
                for relative in actual_files
            )
        )
        if excluded:
            findings.append(
                _diagnostic(
                    "distribution.repair.package-excluded-content",
                    pointer,
                    f"bundled package '{package['id']}' contains excluded publication roots {excluded}",
                )
            )
        try:
            payload_integrity = contracts.compute_package_tree_integrity(package_root)
        except (contracts.PackageTreeIntegrityError, OSError) as error:
            findings.append(
                _diagnostic(
                    "distribution.repair.package-payload-invalid",
                    pointer,
                    f"bundled package '{package['id']}' payload could not be verified ({error.__class__.__name__})",
                )
            )
        else:
            if payload_integrity != package["payloadIntegrity"]:
                findings.append(
                    _diagnostic(
                        "distribution.repair.package-payload-integrity",
                        pointer,
                        f"bundled package '{package['id']}' payload integrity differs",
                    )
                )
    return findings


def _verify_artifact_generations(
    root: Path,
    manifest: dict[str, Any],
    validators: contracts.ContractValidators,
    actual_python_paths: frozenset[str],
) -> tuple[list[contracts.Diagnostic], list[str]]:
    findings: list[contracts.Diagnostic] = []
    artifact_roots: list[str] = []
    references_by_generation: dict[str, list[tuple[int, dict[str, Any]]]] = {}
    for index, reference in enumerate(manifest["packageArtifacts"]):
        references_by_generation.setdefault(reference["artifactGenerationId"], []).append(
            (index, reference)
        )

    for generation_id in sorted(references_by_generation, key=_utf8_key):
        relative_root = f"{_ARTIFACTS_DIRECTORY_NAME}/{generation_id}"
        artifact_roots.append(relative_root)
        result = artifact_publication.verify_published_package_artifact_generation(
            root.joinpath(*relative_root.split("/")),
            generation_id,
            validators,
        )
        if not result.succeeded:
            for diagnostic in result.diagnostics:
                if diagnostic.code == "artifact.python-payload-forbidden":
                    if not _artifact_python_diagnostic_matches_actual_path(
                        diagnostic,
                        relative_root,
                        actual_python_paths,
                    ):
                        findings.append(
                            _diagnostic(
                                "distribution.repair.python-payload-forbidden",
                                "/packageArtifacts",
                                diagnostic.message,
                            )
                        )
                    continue
                findings.append(
                    _diagnostic(
                        "distribution.repair.artifact-invalid",
                        "/packageArtifacts",
                        f"artifact generation '{generation_id}' failed deep verification ({diagnostic.code})",
                    )
                )
            continue
        verified = result.verified_generation
        assert verified is not None
        manifests_by_key = {
            (
                value.package_id,
                value.host_kind,
                value.target_platform,
                value.configuration,
            ): value
            for value in verified.manifests
        }
        expected_keys: set[tuple[str, str, str, str]] = set()
        for index, reference in references_by_generation[generation_id]:
            context = reference["context"]
            package = reference["package"]
            key = (
                package["id"],
                context["hostKind"],
                context["targetPlatform"],
                context["configuration"],
            )
            expected_keys.add(key)
            artifact_manifest = manifests_by_key.get(key)
            pointer = f"/packageArtifacts/{index}"
            expected_path = (
                f"{relative_root}/packages/{package['id']}/"
                f"{contracts.PACKAGE_ARTIFACT_MANIFEST_NAME}"
            )
            if artifact_manifest is None:
                findings.append(
                    _diagnostic(
                        "distribution.repair.artifact-reference-missing",
                        pointer,
                        f"artifact generation '{generation_id}' does not contain the referenced package context",
                    )
                )
                continue
            if reference["manifestPath"] != expected_path:
                findings.append(
                    _diagnostic(
                        "distribution.repair.artifact-manifest-path",
                        pointer,
                        f"artifact reference for '{package['id']}' does not use its canonical generation path",
                    )
                )
            if artifact_manifest.package_version != package["version"]:
                findings.append(
                    _diagnostic(
                        "distribution.repair.artifact-package-version",
                        pointer,
                        f"artifact package '{package['id']}' version differs from Distribution inventory",
                    )
                )
            exact_manifest_bytes = artifact_evidence.render_package_artifact_manifest(
                artifact_manifest
            ).encode("utf-8")
            if contracts.compute_bytes_integrity(exact_manifest_bytes) != reference[
                "manifestIntegrity"
            ]:
                findings.append(
                    _diagnostic(
                        "distribution.repair.artifact-manifest-integrity",
                        pointer,
                        f"artifact package '{package['id']}' manifest integrity differs from Distribution inventory",
                    )
                )
        extra_keys = sorted(set(manifests_by_key) - expected_keys)
        if extra_keys:
            findings.append(
                _diagnostic(
                    "distribution.repair.artifact-reference-extra",
                    "/packageArtifacts",
                    f"artifact generation '{generation_id}' contains unreferenced package contexts",
                )
            )
    return findings, artifact_roots


def _artifact_python_diagnostic_matches_actual_path(
    diagnostic: contracts.Diagnostic,
    relative_root: str,
    actual_python_paths: frozenset[str],
) -> bool:
    """Deduplicate only the exact package/path already reported by the tree scan."""

    packages_prefix = f"{relative_root}/packages/"
    for actual_path in actual_python_paths:
        if not actual_path.startswith(packages_prefix):
            continue
        package_and_relative = actual_path[len(packages_prefix) :]
        package_id, separator, package_relative = package_and_relative.partition("/")
        if not separator:
            continue
        if (
            f"package '{package_id}'" in diagnostic.message
            and f"'{package_relative}'" in diagnostic.message
        ):
            return True
    return False


def _verify_host_profiles(
    root: Path,
    manifest: dict[str, Any],
    validators: contracts.ContractValidators,
) -> list[contracts.Diagnostic]:
    findings: list[contracts.Diagnostic] = []
    for index, profile in enumerate(manifest["hostProfiles"]):
        relative = profile["path"]
        pointer = f"/hostProfiles/{index}"
        contents, read_finding = _read_regular_bytes(
            root.joinpath(*relative.split("/")),
            relative,
            "distribution.repair.profile",
            pointer,
        )
        if read_finding is not None:
            findings.append(read_finding)
            continue
        assert contents is not None
        if contracts.compute_bytes_integrity(contents) != profile["integrity"]:
            findings.append(
                _diagnostic(
                    "distribution.repair.profile-integrity",
                    pointer,
                    f"Host Profile '{relative}' integrity differs",
                )
            )
        try:
            profile_data = json.loads(contents.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            findings.append(
                _diagnostic(
                    "distribution.repair.profile-invalid",
                    pointer,
                    f"Host Profile '{relative}' is not UTF-8 JSON",
                )
            )
            continue
        profile_diagnostics = contracts.validate_manifest_data(
            profile_data,
            relative,
            validators,
        )
        if profile_diagnostics:
            for diagnostic in profile_diagnostics:
                findings.append(
                    _diagnostic(
                        "distribution.repair.profile-contract",
                        diagnostic.pointer,
                        f"Host Profile '{relative}' contract failed ({diagnostic.code})",
                        manifest_path=relative,
                    )
                )
            continue
        if (
            profile_data["hostKind"] != profile["hostKind"]
            or profile_data["targetPlatform"] != profile["targetPlatform"]
        ):
            findings.append(
                _diagnostic(
                    "distribution.repair.profile-context",
                    pointer,
                    f"Host Profile '{relative}' context differs from Distribution inventory",
                )
            )
    return findings


def _verify_closed_distribution_layout(
    manifest: dict[str, Any],
    actual_files: dict[str, _FileFingerprint],
    actual_directories: set[str],
    artifact_roots: list[str],
) -> list[contracts.Diagnostic]:
    findings: list[contracts.Diagnostic] = []
    allowed_exact = {contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME}
    allowed_exact.update(file["path"] for file in manifest["editorImage"]["files"])
    allowed_exact.update(profile["path"] for profile in manifest["hostProfiles"])
    package_roots = [package["root"] for package in manifest["bundledPackages"]]

    for relative in sorted(actual_files, key=_utf8_key):
        if relative in allowed_exact:
            continue
        if any(relative.startswith(f"{package_root}/") for package_root in package_roots):
            continue
        if any(relative.startswith(f"{artifact_root}/") for artifact_root in artifact_roots):
            continue
        findings.append(
            _diagnostic(
                "distribution.repair.layout-extra-file",
                "",
                f"installed generation contains undeclared file '{relative}'",
            )
        )

    expected_directories: set[str] = set()
    for relative in actual_files:
        expected_directories.update(_parents(relative))
    extra_directories = sorted(actual_directories - expected_directories, key=_utf8_key)
    missing_directories = sorted(expected_directories - actual_directories, key=_utf8_key)
    if extra_directories or missing_directories:
        findings.append(
            _diagnostic(
                "distribution.repair.layout-directory-mismatch",
                "",
                "installed generation contains missing or empty undeclared directories",
            )
        )
    return findings


def verify_installed_engine_distribution(
    request: Any,
    validators: contracts.ContractValidators,
) -> InstalledDistributionVerificationResult:
    """Deeply verify an installed generation without mutating filesystem state."""

    if not isinstance(request, InstalledDistributionVerificationRequest):
        return _request_failure(
            _diagnostic(
                "distribution.repair.request-invalid",
                "",
                "request must use InstalledDistributionVerificationRequest",
            )
        )
    if not isinstance(request.generation_root, Path):
        return _request_failure(
            _diagnostic(
                "distribution.repair.request-root-invalid",
                "",
                "generation root must use an explicit pathlib.Path",
            )
        )
    if not isinstance(request.expected_engine_generation_id, str) or not _GENERATION_ID_PATTERN.fullmatch(
        request.expected_engine_generation_id
    ):
        return _request_failure(
            _diagnostic(
                "distribution.repair.request-generation-id-invalid",
                "/expectedEngineGenerationId",
                "expected EngineGenerationId must be sha256- followed by 64 lowercase hex digits",
            )
        )

    expected_generation_id = request.expected_engine_generation_id
    generation_root, root_finding = inspect_installed_engine_distribution_root(
        request.generation_root
    )
    if root_finding is not None:
        return _report(expected_generation_id, (root_finding,))
    assert generation_root is not None
    if generation_root.name != expected_generation_id:
        return _report(
            expected_generation_id,
            (
                _diagnostic(
                    "distribution.repair.root-generation-id-mismatch",
                    "",
                    "generation root basename does not match the externally expected generation",
                ),
            ),
        )

    manifest, manifest_bytes, observed_generation_id, trust_findings = (
        _manifest_trust_findings(
            generation_root,
            expected_generation_id,
            validators,
        )
    )
    if trust_findings or manifest is None or manifest_bytes is None:
        return _report(
            expected_generation_id,
            trust_findings,
            observed_generation_id=observed_generation_id,
        )

    actual_files, actual_directories, findings = _scan_regular_tree(generation_root)
    python_matches = product_payload_policy.find_forbidden_python_product_payloads(
        actual_files
    )
    findings.extend(_python_product_payload_findings(manifest, python_matches))
    findings.extend(_verify_editor_files(generation_root, manifest))
    findings.extend(
        _verify_bundled_packages(
            generation_root,
            manifest,
            validators,
            actual_files,
            actual_directories,
        )
    )
    artifact_findings, artifact_roots = _verify_artifact_generations(
        generation_root,
        manifest,
        validators,
        frozenset(match.path for match in python_matches),
    )
    findings.extend(artifact_findings)
    findings.extend(_verify_host_profiles(generation_root, manifest, validators))
    findings.extend(
        _verify_closed_distribution_layout(
            manifest,
            actual_files,
            actual_directories,
            artifact_roots,
        )
    )

    verified = None
    if not findings:
        verified = VerifiedInstalledDistribution(
            engine_generation_id=expected_generation_id,
            generation_root=stable_file_identity.standard_path(generation_root),
            manifest=copy.deepcopy(manifest),
            manifest_bytes=bytes(manifest_bytes),
            manifest_integrity=contracts.compute_bytes_integrity(manifest_bytes),
        )
    return _report(
        expected_generation_id,
        findings,
        observed_generation_id=observed_generation_id,
        verified_distribution=verified,
    )
