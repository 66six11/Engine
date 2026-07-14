"""Publish immutable package-artifact generations from explicit quiescent roots.

This boundary records build/install/cache evidence. It does not compose an Editor
session, identify an Engine Distribution, execute a build, or activate native code.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import stat
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import package_artifact_evidence as artifacts
from tools import source_build_plan


_COPY_CHUNK_SIZE = 1024 * 1024
_STAGING_DIRECTORY_NAME = ".asharia-package-artifact-staging"
_GENERATIONS_DIRECTORY_NAME = "generations"
_PACKAGES_DIRECTORY_NAME = "packages"


@dataclass(frozen=True, order=True)
class ArtifactFileCollectionBinding:
    """Caller-owned logical binding for one exact file in an isolated root."""

    path: str
    role: str
    media_type: str


@dataclass(frozen=True)
class ProductArtifactCollectionBinding:
    """Exact file bindings for one declared logical product."""

    module_id: str
    product_id: str
    files: tuple[ArtifactFileCollectionBinding, ...]


@dataclass(frozen=True)
class PackageArtifactCollection:
    """One exact package and its caller-owned, quiescent artifact root."""

    package_id: str
    package_version: str
    artifact_root: Path = field(repr=False, compare=False)
    products: tuple[ProductArtifactCollectionBinding, ...]


@dataclass(frozen=True)
class PackageArtifactPublicationReceipt:
    """Location and canonical identity of a committed artifact generation."""

    artifact_generation_id: str
    artifact_generation_path: Path = field(repr=False)
    manifest_set_integrity: artifacts.IntegrityRecord
    manifests: tuple[artifacts.PackageArtifactManifest, ...]
    reused: bool


@dataclass(frozen=True)
class PackageArtifactPublicationResult:
    """Atomic publication result; failures never expose a new receipt."""

    receipt: PackageArtifactPublicationReceipt | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.receipt is not None and not self.diagnostics


@dataclass(frozen=True)
class _FileFingerprint:
    device: int
    inode: int
    mode: int
    size: int
    modified_ns: int
    changed_ns: int


class _PublicationFailure(Exception):
    def __init__(self, diagnostic: contracts.Diagnostic) -> None:
        super().__init__(diagnostic.message)
        self.diagnostic = diagnostic


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


def _raise(code: str, pointer: str, message: str) -> None:
    raise _PublicationFailure(_diagnostic(code, pointer, message))


def _failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> PackageArtifactPublicationResult:
    return PackageArtifactPublicationResult(
        receipt=None,
        diagnostics=tuple(sorted(diagnostics, key=_diagnostic_sort_key)),
    )


def _fingerprint(status: os.stat_result) -> _FileFingerprint:
    return _FileFingerprint(
        device=status.st_dev,
        inode=status.st_ino,
        mode=status.st_mode,
        size=status.st_size,
        modified_ns=status.st_mtime_ns,
        changed_ns=status.st_ctime_ns,
    )


def _is_link_or_reparse(status: os.stat_result) -> bool:
    if stat.S_ISLNK(status.st_mode):
        return True
    reparse_attribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    return bool(getattr(status, "st_file_attributes", 0) & reparse_attribute)


def _path_text(path: Path) -> str:
    return path.as_posix()


def _inspect_existing_directory(path: Path, label: str) -> Path:
    if not isinstance(path, Path):
        _raise(
            "artifact.collection.root-invalid",
            "/package",
            f"{label} must use an explicit pathlib.Path",
        )
    absolute = path.absolute()
    current = Path(absolute.anchor)
    for component in absolute.parts[1:]:
        current /= component
        try:
            status = current.lstat()
        except OSError as error:
            _raise(
                "artifact.collection.root-invalid",
                "/package",
                f"could not inspect {label} '{_path_text(current)}': {error}",
            )
        if _is_link_or_reparse(status):
            _raise(
                "artifact.collection.root-link",
                "/package",
                f"{label} crosses link/reparse point '{_path_text(current)}'",
            )
        if not stat.S_ISDIR(status.st_mode):
            _raise(
                "artifact.collection.root-invalid",
                "/package",
                f"{label} component '{_path_text(current)}' is not a directory",
            )
    try:
        return absolute.resolve(strict=True)
    except OSError as error:
        _raise(
            "artifact.collection.root-invalid",
            "/package",
            f"could not resolve {label} '{_path_text(absolute)}': {error}",
        )


def _paths_overlap(left: Path, right: Path) -> bool:
    try:
        left.relative_to(right)
        return True
    except ValueError:
        pass
    try:
        right.relative_to(left)
        return True
    except ValueError:
        return False


def _snapshot_collections(
    collections: Iterable[PackageArtifactCollection],
    source_plan: Any,
) -> tuple[tuple[PackageArtifactCollection, ...], list[contracts.Diagnostic]]:
    try:
        values = tuple(collections)
    except TypeError:
        return (), [
            _diagnostic(
                "artifact.collection.input-invalid",
                "/package",
                "package artifact collections must be an iterable snapshot",
            )
        ]
    diagnostics: list[contracts.Diagnostic] = []
    if any(not isinstance(value, PackageArtifactCollection) for value in values):
        return (), [
            _diagnostic(
                "artifact.collection.input-invalid",
                "/package",
                "every package artifact collection must use the typed immutable contract",
            )
        ]
    if not isinstance(source_plan, source_build_plan.SourceBuildPlan):
        return values, diagnostics

    selected = {
        package.package_id: package.package_version for package in source_plan.packages
    }
    seen_packages: set[str] = set()
    for package in values:
        if (
            not isinstance(package.package_id, str)
            or not package.package_id
            or not isinstance(package.package_version, str)
            or not package.package_version
        ):
            diagnostics.append(
                _diagnostic(
                    "artifact.collection.package-invalid",
                    "/package",
                    "collection package identity/version must be non-empty strings",
                )
            )
        else:
            if package.package_id in seen_packages:
                diagnostics.append(
                    _diagnostic(
                        "artifact.collection.package-duplicate",
                        "/package",
                        f"package '{package.package_id}' has multiple artifact roots",
                    )
                )
            seen_packages.add(package.package_id)
        if (
            isinstance(package.package_id, str)
            and isinstance(package.package_version, str)
            and selected.get(package.package_id) != package.package_version
        ):
            diagnostics.append(
                _diagnostic(
                    "artifact.collection.package-unknown",
                    "/package",
                    f"collection package '{package.package_id}@{package.package_version}' is not selected",
                )
            )
        if not isinstance(package.artifact_root, Path):
            diagnostics.append(
                _diagnostic(
                    "artifact.collection.root-invalid",
                    "/package",
                    f"package '{package.package_id}' artifact root must use pathlib.Path",
                )
            )
        if not isinstance(package.products, tuple) or not package.products:
            diagnostics.append(
                _diagnostic(
                    "artifact.collection.products-invalid",
                    "/modules",
                    f"package '{package.package_id}' collection must bind at least one product",
                )
            )
            continue
        for product in package.products:
            if not isinstance(product, ProductArtifactCollectionBinding):
                diagnostics.append(
                    _diagnostic(
                        "artifact.collection.product-invalid",
                        "/modules",
                        f"package '{package.package_id}' contains an invalid product binding",
                    )
                )
                continue
            if not isinstance(product.files, tuple) or not product.files:
                diagnostics.append(
                    _diagnostic(
                        "artifact.observation.files-invalid",
                        "/modules",
                        f"product '{product.module_id}:{product.product_id}' needs files",
                    )
                )
                continue
            if any(
                not isinstance(file, ArtifactFileCollectionBinding)
                for file in product.files
            ):
                diagnostics.append(
                    _diagnostic(
                        "artifact.collection.file-invalid",
                        "/modules",
                        f"product '{product.module_id}:{product.product_id}' has an invalid file binding",
                    )
                )
    if diagnostics:
        return values, diagnostics
    return tuple(sorted(values, key=lambda value: _utf8_key(value.package_id))), []


def _dummy_observations(
    collections: tuple[PackageArtifactCollection, ...],
    source_plan: source_build_plan.SourceBuildPlan,
) -> tuple[artifacts.ProductArtifactObservation, ...]:
    empty_integrity = contracts.compute_bytes_integrity(b"")
    integrity = artifacts.IntegrityRecord(
        empty_integrity["algorithm"],
        empty_integrity["digest"],
    )
    observations: list[artifacts.ProductArtifactObservation] = []
    for package in collections:
        for product in package.products:
            observations.append(
                artifacts.ProductArtifactObservation(
                    package_id=package.package_id,
                    package_version=package.package_version,
                    module_id=product.module_id,
                    product_id=product.product_id,
                    target_platform=source_plan.target_platform,
                    configuration=source_plan.configuration,
                    files=tuple(
                        artifacts.ArtifactFileObservation(
                            path=file.path,
                            role=file.role,
                            media_type=file.media_type,
                            size=0,
                            integrity=integrity,
                            content=b"",
                        )
                        for file in product.files
                    ),
                )
            )
    return tuple(observations)


def _expected_package_tree(
    package: PackageArtifactCollection,
) -> tuple[set[str], set[str]]:
    files = {
        file.path
        for product in package.products
        for file in product.files
    }
    directories: set[str] = set()
    for file in files:
        segments = file.split("/")[:-1]
        for length in range(1, len(segments) + 1):
            directories.add("/".join(segments[:length]))
    return files, directories


def _scan_regular_tree(
    root: Path,
    *,
    code_prefix: str,
) -> tuple[dict[str, _FileFingerprint], set[str]]:
    files: dict[str, _FileFingerprint] = {}
    directories: set[str] = set()

    def visit(directory: Path, prefix: str) -> None:
        try:
            with os.scandir(directory) as iterator:
                entries = sorted(iterator, key=lambda entry: os.fsencode(entry.name))
        except OSError as error:
            _raise(
                f"{code_prefix}.scan-failed",
                "/package",
                f"could not scan '{_path_text(directory)}': {error}",
            )
        for entry in entries:
            relative = f"{prefix}/{entry.name}" if prefix else entry.name
            try:
                status = entry.stat(follow_symlinks=False)
            except OSError as error:
                _raise(
                    f"{code_prefix}.entry-invalid",
                    "/package",
                    f"could not inspect '{relative}': {error}",
                )
            if _is_link_or_reparse(status):
                _raise(
                    f"{code_prefix}.entry-link",
                    "/package",
                    f"tree entry '{relative}' is a link/reparse point",
                )
            if stat.S_ISDIR(status.st_mode):
                directories.add(relative)
                visit(Path(entry.path), relative)
            elif stat.S_ISREG(status.st_mode):
                # On Windows DirEntry.stat() reports zero st_dev/st_ino while
                # Path.lstat()/fstat() provide the stable file identity. Use the
                # same evidence source as copy/open checks to avoid false drift.
                regular_status = _stable_regular_file_status(
                    Path(entry.path),
                    "tree file",
                )
                files[relative] = _fingerprint(regular_status)
            else:
                _raise(
                    f"{code_prefix}.entry-unsupported",
                    "/package",
                    f"tree entry '{relative}' is not a regular file or directory",
                )

    visit(root, "")
    return files, directories


def _validate_closed_source_tree(
    package: PackageArtifactCollection,
    root: Path,
    expected_fingerprints: dict[str, _FileFingerprint] | None = None,
) -> None:
    expected_files, expected_directories = _expected_package_tree(package)
    actual_files, actual_directories = _scan_regular_tree(
        root,
        code_prefix="artifact.collection",
    )
    for path in sorted(expected_files - set(actual_files), key=_utf8_key):
        _raise(
            "artifact.collection.file-missing",
            "/modules",
            f"package '{package.package_id}' artifact root is missing '{path}'",
        )
    for path in sorted(set(actual_files) - expected_files, key=_utf8_key):
        _raise(
            "artifact.collection.file-extra",
            "/modules",
            f"package '{package.package_id}' artifact root contains extra file '{path}'",
        )
    for path in sorted(expected_directories - actual_directories, key=_utf8_key):
        _raise(
            "artifact.collection.directory-missing",
            "/modules",
            f"package '{package.package_id}' artifact root is missing directory '{path}'",
        )
    for path in sorted(actual_directories - expected_directories, key=_utf8_key):
        _raise(
            "artifact.collection.directory-extra",
            "/modules",
            f"package '{package.package_id}' artifact root contains extra directory '{path}'",
        )
    if expected_fingerprints is not None:
        for path in sorted(expected_files, key=_utf8_key):
            if actual_files[path] != expected_fingerprints[path]:
                _raise(
                    "artifact.collection.source-drift",
                    "/modules",
                    f"package '{package.package_id}' source file '{path}' changed during collection",
                )


def _stable_regular_file_status(path: Path, label: str) -> os.stat_result:
    try:
        status = path.lstat()
    except OSError as error:
        _raise(
            "artifact.collection.file-invalid",
            "/modules",
            f"could not inspect {label} '{_path_text(path)}': {error}",
        )
    if _is_link_or_reparse(status) or not stat.S_ISREG(status.st_mode):
        _raise(
            "artifact.collection.file-invalid",
            "/modules",
            f"{label} '{_path_text(path)}' is not a regular file",
        )
    return status


def _copy_file_to_staging(
    source: Path,
    destination: Path,
) -> tuple[int, artifacts.IntegrityRecord, _FileFingerprint]:
    source_status = _stable_regular_file_status(source, "source file")
    source_fingerprint = _fingerprint(source_status)
    destination.parent.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256()
    size = 0
    try:
        with source.open("rb") as source_file, destination.open("xb") as destination_file:
            opened_source = _fingerprint(os.fstat(source_file.fileno()))
            if opened_source != source_fingerprint:
                _raise(
                    "artifact.collection.source-drift",
                    "/modules",
                    f"source file '{_path_text(source)}' changed while it was opened",
                )
            while True:
                chunk = source_file.read(_COPY_CHUNK_SIZE)
                if not chunk:
                    break
                destination_file.write(chunk)
                digest.update(chunk)
                size += len(chunk)
            destination_file.flush()
            if _fingerprint(os.fstat(source_file.fileno())) != opened_source:
                _raise(
                    "artifact.collection.source-drift",
                    "/modules",
                    f"source file '{_path_text(source)}' changed while it was copied",
                )
    except _PublicationFailure:
        raise
    except OSError as error:
        _raise(
            "artifact.collection.copy-failed",
            "/modules",
            (
                f"could not copy source '{_path_text(source)}' to owned staging "
                f"'{_path_text(destination)}': {error}"
            ),
        )
    if _fingerprint(_stable_regular_file_status(source, "source file")) != source_fingerprint:
        _raise(
            "artifact.collection.source-drift",
            "/modules",
            f"source file '{_path_text(source)}' changed after collection",
        )
    return (
        size,
        artifacts.IntegrityRecord("sha256", digest.hexdigest()),
        source_fingerprint,
    )


def _hash_staged_file(
    path: Path,
) -> tuple[int, artifacts.IntegrityRecord, _FileFingerprint]:
    status = _stable_regular_file_status(path, "staged file")
    fingerprint = _fingerprint(status)
    digest = hashlib.sha256()
    size = 0
    try:
        with path.open("rb") as file:
            opened = _fingerprint(os.fstat(file.fileno()))
            if opened != fingerprint:
                _raise(
                    "artifact.publication.staging-drift",
                    "/modules",
                    f"staged file '{_path_text(path)}' changed while it was opened",
                )
            while True:
                chunk = file.read(_COPY_CHUNK_SIZE)
                if not chunk:
                    break
                digest.update(chunk)
                size += len(chunk)
            if _fingerprint(os.fstat(file.fileno())) != opened:
                _raise(
                    "artifact.publication.staging-drift",
                    "/modules",
                    f"staged file '{_path_text(path)}' changed while it was verified",
                )
    except _PublicationFailure:
        raise
    except OSError as error:
        _raise(
            "artifact.publication.staging-read-failed",
            "/modules",
            f"could not verify staged file '{_path_text(path)}': {error}",
        )
    if _fingerprint(_stable_regular_file_status(path, "staged file")) != fingerprint:
        _raise(
            "artifact.publication.staging-drift",
            "/modules",
            f"staged file '{_path_text(path)}' changed after verification",
        )
    return size, artifacts.IntegrityRecord("sha256", digest.hexdigest()), fingerprint


def _write_manifest(path: Path, manifest: artifacts.PackageArtifactManifest) -> None:
    expected = artifacts.render_package_artifact_manifest(manifest).encode("utf-8")
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("xb") as file:
            file.write(expected)
            file.flush()
        with path.open("rb") as file:
            actual = file.read()
    except OSError as error:
        _raise(
            "artifact.publication.manifest-write-failed",
            "/package",
            f"could not write staged manifest '{_path_text(path)}': {error}",
        )
    if actual != expected:
        _raise(
            "artifact.publication.manifest-mismatch",
            "/package",
            f"staged manifest '{_path_text(path)}' bytes changed after write",
        )
    try:
        parsed = json.loads(actual.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        _raise(
            "artifact.publication.manifest-mismatch",
            "/package",
            f"staged manifest '{_path_text(path)}' is not canonical JSON: {error}",
        )
    if parsed != artifacts.package_artifact_manifest_to_data(manifest):
        _raise(
            "artifact.publication.manifest-mismatch",
            "/package",
            f"staged manifest '{_path_text(path)}' does not match verified evidence",
        )


def _expected_generation_tree(
    manifests: tuple[artifacts.PackageArtifactManifest, ...],
) -> tuple[set[str], set[str], dict[str, artifacts.ArtifactFileEvidence]]:
    files: set[str] = set()
    directories = {_PACKAGES_DIRECTORY_NAME}
    artifact_evidence: dict[str, artifacts.ArtifactFileEvidence] = {}
    for manifest in manifests:
        package_root = f"{_PACKAGES_DIRECTORY_NAME}/{manifest.package_id}"
        directories.add(package_root)
        files.add(f"{package_root}/{contracts.PACKAGE_ARTIFACT_MANIFEST_NAME}")
        for module in manifest.modules:
            for product in module.products:
                for artifact in product.files:
                    relative = f"{package_root}/{artifact.path}"
                    files.add(relative)
                    artifact_evidence[relative] = artifact
                    segments = relative.split("/")[:-1]
                    for length in range(1, len(segments) + 1):
                        directories.add("/".join(segments[:length]))
    return files, directories, artifact_evidence


def _validate_generation_layout(
    generation_root: Path,
    manifests: tuple[artifacts.PackageArtifactManifest, ...],
    *,
    staged_fingerprints: dict[str, _FileFingerprint] | None,
    rehash_artifacts: bool,
) -> None:
    expected_files, expected_directories, artifact_evidence = _expected_generation_tree(
        manifests
    )
    actual_files, actual_directories = _scan_regular_tree(
        generation_root,
        code_prefix="artifact.publication",
    )
    if set(actual_files) != expected_files or actual_directories != expected_directories:
        missing_files = sorted(expected_files - set(actual_files), key=_utf8_key)
        extra_files = sorted(set(actual_files) - expected_files, key=_utf8_key)
        missing_directories = sorted(
            expected_directories - actual_directories,
            key=_utf8_key,
        )
        extra_directories = sorted(
            actual_directories - expected_directories,
            key=_utf8_key,
        )
        _raise(
            "artifact.publication.layout-mismatch",
            "/package",
            (
                "generation layout differs from verified manifests "
                f"missingFiles={missing_files} extraFiles={extra_files} "
                f"missingDirectories={missing_directories} extraDirectories={extra_directories}"
            ),
        )

    for manifest in manifests:
        manifest_path = (
            generation_root
            / _PACKAGES_DIRECTORY_NAME
            / manifest.package_id
            / contracts.PACKAGE_ARTIFACT_MANIFEST_NAME
        )
        expected = artifacts.render_package_artifact_manifest(manifest).encode("utf-8")
        try:
            with manifest_path.open("rb") as file:
                actual = file.read()
        except OSError as error:
            _raise(
                "artifact.publication.manifest-read-failed",
                "/package",
                f"could not read manifest '{_path_text(manifest_path)}': {error}",
            )
        if actual != expected:
            _raise(
                "artifact.publication.manifest-mismatch",
                "/package",
                f"manifest '{manifest.package_id}' differs from verified evidence",
            )

    for relative, evidence in sorted(
        artifact_evidence.items(),
        key=lambda value: _utf8_key(value[0]),
    ):
        if staged_fingerprints is not None:
            if actual_files[relative] != staged_fingerprints[relative]:
                _raise(
                    "artifact.publication.staging-drift",
                    "/modules",
                    f"staged artifact '{relative}' changed before commit",
                )
        if rehash_artifacts:
            size, integrity, _ = _hash_staged_file(generation_root / Path(relative))
            if size != evidence.size or integrity != evidence.integrity:
                _raise(
                    "artifact.publication.existing-corrupt",
                    "/modules",
                    f"published artifact '{relative}' does not match its manifest",
                )


def _cleanup_staging(path: Path) -> contracts.Diagnostic | None:
    if not os.path.lexists(path):
        return None
    try:
        shutil.rmtree(path)
    except OSError as error:
        return _diagnostic(
            "artifact.publication.cleanup-failed",
            "/package",
            f"could not remove owned staging '{_path_text(path)}': {error}",
        )
    return None


def collect_and_publish_package_artifacts(
    host_plan: Any,
    source_plan: Any,
    verified_graph: Any,
    collections: Iterable[PackageArtifactCollection],
    publication_root: Path,
    validators: contracts.ContractValidators,
) -> PackageArtifactPublicationResult:
    """Collect exact roots into one verified, immutable artifact generation."""

    staging_path: Path | None = None
    try:
        collection_values, input_diagnostics = _snapshot_collections(
            collections,
            source_plan,
        )
        if input_diagnostics:
            return _failure(input_diagnostics)
        if not isinstance(source_plan, source_build_plan.SourceBuildPlan):
            return _failure(
                artifacts.verify_package_artifacts(
                    host_plan,
                    source_plan,
                    verified_graph,
                    (),
                    validators,
                ).diagnostics
            )

        preflight = artifacts.verify_package_artifacts(
            host_plan,
            source_plan,
            verified_graph,
            _dummy_observations(collection_values, source_plan),
            validators,
        )
        if not preflight.succeeded:
            return _failure(preflight.diagnostics)

        canonical_publication_root = _inspect_existing_directory(
            publication_root,
            "publication root",
        )
        canonical_roots: dict[str, Path] = {}
        for package in collection_values:
            root = _inspect_existing_directory(
                package.artifact_root,
                f"package '{package.package_id}' artifact root",
            )
            if _paths_overlap(root, canonical_publication_root):
                _raise(
                    "artifact.collection.root-overlap",
                    "/package",
                    f"package '{package.package_id}' artifact root overlaps publication root",
                )
            for other_id, other_root in canonical_roots.items():
                if _paths_overlap(root, other_root):
                    _raise(
                        "artifact.collection.root-overlap",
                        "/package",
                        f"package roots '{other_id}' and '{package.package_id}' overlap",
                    )
            canonical_roots[package.package_id] = root
            _validate_closed_source_tree(package, root)

        staging_parent = canonical_publication_root / _STAGING_DIRECTORY_NAME
        generations_parent = canonical_publication_root / _GENERATIONS_DIRECTORY_NAME
        staging_parent.mkdir(exist_ok=True)
        generations_parent.mkdir(exist_ok=True)
        canonical_staging_parent = _inspect_existing_directory(
            staging_parent,
            "publication staging root",
        )
        canonical_generations_parent = _inspect_existing_directory(
            generations_parent,
            "publication generations root",
        )
        if (
            canonical_staging_parent.stat().st_dev
            != canonical_generations_parent.stat().st_dev
        ):
            _raise(
                "artifact.publication.cross-filesystem",
                "/package",
                "staging and generation roots must use the same filesystem",
            )
        staging_path = Path(
            tempfile.mkdtemp(prefix="generation-", dir=canonical_staging_parent)
        )

        observations: list[artifacts._CollectedProductArtifactEvidence] = []
        source_fingerprints: dict[str, dict[str, _FileFingerprint]] = {}
        staged_fingerprints: dict[str, _FileFingerprint] = {}
        for package in collection_values:
            package_root = canonical_roots[package.package_id]
            package_source_fingerprints: dict[str, _FileFingerprint] = {}
            for product in sorted(
                package.products,
                key=lambda value: (
                    _utf8_key(value.module_id),
                    _utf8_key(value.product_id),
                ),
            ):
                file_evidence: list[artifacts.ArtifactFileEvidence] = []
                for file in sorted(
                    product.files,
                    key=lambda value: (_utf8_key(value.path), value.role),
                ):
                    source = package_root.joinpath(*file.path.split("/"))
                    relative = (
                        f"{_PACKAGES_DIRECTORY_NAME}/{package.package_id}/{file.path}"
                    )
                    destination = staging_path.joinpath(*relative.split("/"))
                    copied_size, copied_integrity, source_fingerprint = (
                        _copy_file_to_staging(source, destination)
                    )
                    staged_size, staged_integrity, staged_fingerprint = _hash_staged_file(
                        destination
                    )
                    if (
                        staged_size != copied_size
                        or staged_integrity != copied_integrity
                    ):
                        _raise(
                            "artifact.publication.staging-integrity-mismatch",
                            "/modules",
                            f"staged artifact '{relative}' differs from collected bytes",
                        )
                    package_source_fingerprints[file.path] = source_fingerprint
                    staged_fingerprints[relative] = staged_fingerprint
                    file_evidence.append(
                        artifacts.ArtifactFileEvidence(
                            path=file.path,
                            role=file.role,
                            media_type=file.media_type,
                            size=staged_size,
                            integrity=staged_integrity,
                        )
                    )
                observations.append(
                    artifacts._CollectedProductArtifactEvidence(
                        package_id=package.package_id,
                        package_version=package.package_version,
                        module_id=product.module_id,
                        product_id=product.product_id,
                        target_platform=source_plan.target_platform,
                        configuration=source_plan.configuration,
                        files=tuple(file_evidence),
                    )
                )
            source_fingerprints[package.package_id] = package_source_fingerprints

        for package in collection_values:
            _validate_closed_source_tree(
                package,
                canonical_roots[package.package_id],
                source_fingerprints[package.package_id],
            )

        verification = artifacts._verify_collected_package_artifacts(
            host_plan,
            source_plan,
            verified_graph,
            observations,
            validators,
        )
        if not verification.succeeded:
            cleanup_diagnostic = _cleanup_staging(staging_path)
            diagnostics = list(verification.diagnostics)
            if cleanup_diagnostic is not None:
                diagnostics.append(cleanup_diagnostic)
            return _failure(diagnostics)
        assert verification.manifest_set_integrity is not None

        for manifest in verification.manifests:
            _write_manifest(
                staging_path
                / _PACKAGES_DIRECTORY_NAME
                / manifest.package_id
                / contracts.PACKAGE_ARTIFACT_MANIFEST_NAME,
                manifest,
            )
        _validate_generation_layout(
            staging_path,
            verification.manifests,
            staged_fingerprints=staged_fingerprints,
            rehash_artifacts=False,
        )

        artifact_generation_id = (
            f"{verification.manifest_set_integrity.algorithm}-"
            f"{verification.manifest_set_integrity.digest}"
        )
        final_path = canonical_generations_parent / artifact_generation_id
        reused = False
        if os.path.lexists(final_path):
            _inspect_existing_directory(final_path, "existing artifact generation")
            _validate_generation_layout(
                final_path,
                verification.manifests,
                staged_fingerprints=None,
                rehash_artifacts=True,
            )
            cleanup_diagnostic = _cleanup_staging(staging_path)
            if cleanup_diagnostic is not None:
                return _failure([cleanup_diagnostic])
            reused = True
        else:
            try:
                os.rename(staging_path, final_path)
            except OSError as error:
                if not os.path.lexists(final_path):
                    _raise(
                        "artifact.publication.rename-failed",
                        "/package",
                        (
                            "could not publish artifact generation "
                            f"'{artifact_generation_id}' with one rename: "
                            f"{error}"
                        ),
                    )
                _inspect_existing_directory(final_path, "concurrent artifact generation")
                _validate_generation_layout(
                    final_path,
                    verification.manifests,
                    staged_fingerprints=None,
                    rehash_artifacts=True,
                )
                cleanup_diagnostic = _cleanup_staging(staging_path)
                if cleanup_diagnostic is not None:
                    return _failure([cleanup_diagnostic])
                reused = True

        return PackageArtifactPublicationResult(
            receipt=PackageArtifactPublicationReceipt(
                artifact_generation_id=artifact_generation_id,
                artifact_generation_path=final_path,
                manifest_set_integrity=verification.manifest_set_integrity,
                manifests=verification.manifests,
                reused=reused,
            ),
            diagnostics=(),
        )
    except _PublicationFailure as error:
        diagnostics = [error.diagnostic]
    except OSError as error:
        diagnostics = [
            _diagnostic(
                "artifact.publication.io-failed",
                "/package",
                f"package artifact publication failed: {error}",
            )
        ]

    if staging_path is not None:
        cleanup_diagnostic = _cleanup_staging(staging_path)
        if cleanup_diagnostic is not None:
            diagnostics.append(cleanup_diagnostic)
    return _failure(diagnostics)
