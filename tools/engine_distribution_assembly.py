"""Assemble one immutable Engine Distribution generation from explicit inputs.

This build/release-tool boundary does not execute a build, repair an installed
generation, select an active generation, or load project-native code.
"""

from __future__ import annotations

import json
import os
import shutil
import stat
import tempfile
from dataclasses import dataclass, field, replace
from pathlib import Path
from typing import Any, Iterable

from tools import check_package_contracts as contracts
from tools import package_artifact_evidence as artifact_evidence
from tools import package_artifact_publication as artifact_publication
from tools import stable_file_identity
from tools.package_candidates import PackageCandidate


_COPY_CHUNK_SIZE = 1024 * 1024
_STAGING_DIRECTORY_NAME = ".asharia-distribution-staging"
_GENERATIONS_DIRECTORY_NAME = "generations"
_ARTIFACTS_DIRECTORY_NAME = "artifacts"


@dataclass(frozen=True)
class EngineDistributionIdentity:
    distribution_id: str
    engine_version: str
    engine_api_version: str


@dataclass(frozen=True)
class EngineDistributionContext:
    target_platform: str
    configuration: str
    compiler_id: str
    compiler_version: str
    target_system: str
    target_architecture: str
    runtime_library: str


@dataclass(frozen=True, order=True)
class EditorImageFileBinding:
    path: str
    role: str
    media_type: str


@dataclass(frozen=True)
class EditorImageAssembly:
    root: Path = field(repr=False, compare=False)
    entry_point: str
    files: tuple[EditorImageFileBinding, ...]


@dataclass(frozen=True)
class BundledPackageAssembly:
    candidate: PackageCandidate
    availability: str
    root: str


@dataclass(frozen=True)
class HostProfileAssembly:
    path: str
    exact_bytes: bytes = field(repr=False)


@dataclass(frozen=True)
class DistributionAssemblyRequest:
    identity: EngineDistributionIdentity
    context: EngineDistributionContext
    editor_image: EditorImageAssembly
    bundled_packages: tuple[BundledPackageAssembly, ...]
    artifact_publications: tuple[
        artifact_publication.PackageArtifactPublicationReceipt, ...
    ]
    host_profiles: tuple[HostProfileAssembly, ...]


@dataclass(frozen=True)
class EngineDistributionAssemblyReceipt:
    engine_generation_id: str
    engine_generation_path: Path = field(repr=False)
    manifest: dict[str, Any]
    manifest_integrity: dict[str, str]
    reused: bool


@dataclass(frozen=True)
class EngineDistributionAssemblyResult:
    receipt: EngineDistributionAssemblyReceipt | None
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


class _AssemblyFailure(Exception):
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
        manifest_path=contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME,
        pointer=pointer,
        message=message,
    )


def _raise(code: str, pointer: str, message: str) -> None:
    raise _AssemblyFailure(_diagnostic(code, pointer, message))


def _failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> EngineDistributionAssemblyResult:
    return EngineDistributionAssemblyResult(
        receipt=None,
        diagnostics=tuple(sorted(diagnostics, key=_diagnostic_sort_key)),
    )


def _fingerprint(status: os.stat_result) -> _FileFingerprint:
    return _FileFingerprint(
        device=status.st_dev,
        inode=status.st_ino,
        mode=stable_file_identity.file_kind(status),
        size=status.st_size,
        modified_ns=status.st_mtime_ns,
        changed_ns=stable_file_identity.changed_ns(status),
    )


def _is_link_or_reparse(status: os.stat_result) -> bool:
    if stat.S_ISLNK(status.st_mode):
        return True
    reparse_attribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    return bool(getattr(status, "st_file_attributes", 0) & reparse_attribute)


def _path_text(path: Path) -> str:
    return path.as_posix()


def _inspect_existing_directory(path: Any, label: str) -> Path:
    if not isinstance(path, Path):
        _raise(
            "distribution.assembly.root-invalid",
            "",
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
                "distribution.assembly.root-invalid",
                "",
                f"could not inspect {label} '{_path_text(current)}': {error}",
            )
        if _is_link_or_reparse(status):
            _raise(
                "distribution.assembly.root-link",
                "",
                f"{label} crosses link/reparse point '{_path_text(current)}'",
            )
    try:
        status = absolute.lstat()
    except OSError as error:
        _raise(
            "distribution.assembly.root-invalid",
            "",
            f"could not inspect {label} '{_path_text(absolute)}': {error}",
        )
    if not stat.S_ISDIR(status.st_mode):
        _raise(
            "distribution.assembly.root-invalid",
            "",
            f"{label} '{_path_text(absolute)}' is not a directory",
        )
    return absolute


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


def _relative_parts(value: Any, pointer: str) -> tuple[str, ...]:
    if (
        not isinstance(value, str)
        or not value
        or "\\" in value
        or value.startswith("/")
        or any(part in {"", ".", ".."} for part in value.split("/"))
    ):
        _raise(
            "distribution.assembly.path-invalid",
            pointer,
            f"'{value}' must be a normalized relative path",
        )
    return tuple(value.split("/"))


def _parents(relative: str) -> set[str]:
    parts = relative.split("/")[:-1]
    return {"/".join(parts[:length]) for length in range(1, len(parts) + 1)}


def _scan_regular_tree(
    root: Path,
    *,
    excluded_root_names: frozenset[str] = frozenset(),
) -> tuple[dict[str, _FileFingerprint], set[str]]:
    files: dict[str, _FileFingerprint] = {}
    directories: set[str] = set()

    def visit(directory: Path, relative_parts: tuple[str, ...]) -> None:
        try:
            entries = sorted(
                os.scandir(directory),
                key=lambda value: value.name.encode("utf-8", errors="surrogatepass"),
            )
        except OSError as error:
            _raise(
                "distribution.assembly.tree-read-failed",
                "",
                f"could not enumerate '{_path_text(directory)}': {error}",
            )
        for entry in entries:
            if not relative_parts and entry.name in excluded_root_names:
                continue
            child_parts = relative_parts + (entry.name,)
            relative = "/".join(child_parts)
            try:
                status = entry.stat(follow_symlinks=False)
            except OSError as error:
                _raise(
                    "distribution.assembly.tree-read-failed",
                    "",
                    f"could not inspect '{relative}': {error}",
                )
            if _is_link_or_reparse(status):
                _raise(
                    "distribution.assembly.tree-link",
                    "",
                    f"tree contains link/reparse entry '{relative}'",
                )
            if stat.S_ISDIR(status.st_mode):
                directories.add(relative)
                visit(Path(entry.path), child_parts)
            elif stat.S_ISREG(status.st_mode):
                try:
                    regular_status = Path(entry.path).lstat()
                except OSError as error:
                    _raise(
                        "distribution.assembly.tree-read-failed",
                        "",
                        f"could not inspect regular file '{relative}': {error}",
                    )
                if _is_link_or_reparse(regular_status) or not stat.S_ISREG(
                    regular_status.st_mode
                ):
                    _raise(
                        "distribution.assembly.tree-non-regular",
                        "",
                        f"tree entry '{relative}' is not a regular file",
                    )
                files[relative] = _fingerprint(regular_status)
            else:
                _raise(
                    "distribution.assembly.tree-non-regular",
                    "",
                    f"tree contains non-regular entry '{relative}'",
                )
    visit(root, ())
    return files, directories


def _hash_regular_file(path: Path) -> tuple[int, dict[str, str], _FileFingerprint]:
    import hashlib

    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            before = _fingerprint(os.fstat(source.fileno()))
            if not stat.S_ISREG(before.mode):
                _raise(
                    "distribution.assembly.file-non-regular",
                    "",
                    f"'{_path_text(path)}' is not a regular file",
                )
            size = 0
            while chunk := source.read(_COPY_CHUNK_SIZE):
                size += len(chunk)
                digest.update(chunk)
            after = _fingerprint(os.fstat(source.fileno()))
    except OSError as error:
        _raise(
            "distribution.assembly.file-read-failed",
            "",
            f"could not hash '{_path_text(path)}': {error}",
        )
    if before != after or size != before.size:
        _raise(
            "distribution.assembly.source-drift",
            "",
            f"source file '{_path_text(path)}' changed while it was read",
        )
    return size, {"algorithm": "sha256", "digest": digest.hexdigest()}, after


def _copy_regular_file(
    source: Path,
    destination: Path,
) -> tuple[int, dict[str, str], _FileFingerprint, _FileFingerprint]:
    import hashlib

    digest = hashlib.sha256()
    destination.parent.mkdir(parents=True, exist_ok=True)
    try:
        with source.open("rb") as source_file:
            source_before = _fingerprint(os.fstat(source_file.fileno()))
            if not stat.S_ISREG(source_before.mode):
                _raise(
                    "distribution.assembly.file-non-regular",
                    "",
                    f"'{_path_text(source)}' is not a regular file",
                )
            size = 0
            with destination.open("xb") as destination_file:
                while chunk := source_file.read(_COPY_CHUNK_SIZE):
                    size += len(chunk)
                    digest.update(chunk)
                    destination_file.write(chunk)
                destination_file.flush()
            source_after = _fingerprint(os.fstat(source_file.fileno()))
    except OSError as error:
        _raise(
            "distribution.assembly.copy-failed",
            "",
            f"could not copy '{_path_text(source)}': {error}",
        )
    if source_before != source_after or size != source_before.size:
        _raise(
            "distribution.assembly.source-drift",
            "",
            f"source file '{_path_text(source)}' changed while it was copied",
        )
    staged_size, staged_integrity, staged_fingerprint = _hash_regular_file(destination)
    copied_integrity = {"algorithm": "sha256", "digest": digest.hexdigest()}
    if staged_size != size or staged_integrity != copied_integrity:
        _raise(
            "distribution.assembly.staging-integrity-mismatch",
            "",
            f"staged file '{_path_text(destination)}' differs from copied bytes",
        )
    return size, copied_integrity, source_after, staged_fingerprint


def _write_exact_file(path: Path, contents: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("xb") as output:
            output.write(contents)
            output.flush()
    except OSError as error:
        _raise(
            "distribution.assembly.write-failed",
            "",
            f"could not write '{_path_text(path)}': {error}",
        )
    size, integrity, _ = _hash_regular_file(path)
    if size != len(contents) or integrity != contracts.compute_bytes_integrity(contents):
        _raise(
            "distribution.assembly.staging-integrity-mismatch",
            "",
            f"staged file '{_path_text(path)}' changed after write",
        )


def _read_utf8_json_file(path: Path, label: str) -> Any:
    try:
        with path.open("rb") as source:
            contents = source.read()
        return json.loads(contents.decode("utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        _raise(
            "distribution.assembly.manifest-read-failed",
            "",
            f"could not read {label} '{_path_text(path)}': {error}",
        )


def _validate_typed_request(request: Any) -> None:
    if not isinstance(request, DistributionAssemblyRequest):
        _raise(
            "distribution.assembly.request-invalid",
            "",
            "request must use DistributionAssemblyRequest",
        )
    if not isinstance(request.identity, EngineDistributionIdentity):
        _raise("distribution.assembly.request-invalid", "/distribution", "invalid identity")
    if not isinstance(request.context, EngineDistributionContext):
        _raise("distribution.assembly.request-invalid", "/context", "invalid context")
    if not isinstance(request.editor_image, EditorImageAssembly):
        _raise("distribution.assembly.request-invalid", "/editorImage", "invalid Editor Image")
    tuple_fields = (
        (request.editor_image.files, "/editorImage/files"),
        (request.bundled_packages, "/bundledPackages"),
        (request.artifact_publications, "/packageArtifacts"),
        (request.host_profiles, "/hostProfiles"),
    )
    for value, pointer in tuple_fields:
        if not isinstance(value, tuple):
            _raise(
                "distribution.assembly.request-invalid",
                pointer,
                "assembly collections must be immutable tuples",
            )
    if not request.editor_image.files or not request.host_profiles:
        _raise(
            "distribution.assembly.request-invalid",
            "",
            "Editor Image files and Host Profiles must be non-empty",
        )
    if any(not isinstance(value, str) or not value for value in request.identity.__dict__.values()):
        _raise("distribution.assembly.request-invalid", "/distribution", "identity fields must be non-empty strings")
    if any(not isinstance(value, str) or not value for value in request.context.__dict__.values()):
        _raise("distribution.assembly.request-invalid", "/context", "context fields must be non-empty strings")
    if any(not isinstance(value, EditorImageFileBinding) for value in request.editor_image.files):
        _raise("distribution.assembly.request-invalid", "/editorImage/files", "invalid Editor Image file binding")
    if not isinstance(request.editor_image.entry_point, str) or not request.editor_image.entry_point:
        _raise("distribution.assembly.request-invalid", "/editorImage/entryPoint", "Editor entry point must be a non-empty string")
    for value in request.editor_image.files:
        if any(
            not isinstance(field_value, str) or not field_value
            for field_value in (value.path, value.role, value.media_type)
        ):
            _raise("distribution.assembly.request-invalid", "/editorImage/files", "Editor file binding fields must be non-empty strings")
    if any(not isinstance(value, BundledPackageAssembly) for value in request.bundled_packages):
        _raise("distribution.assembly.request-invalid", "/bundledPackages", "invalid bundled package binding")
    for value in request.bundled_packages:
        if (
            not isinstance(value.availability, str)
            or not value.availability
            or not isinstance(value.root, str)
            or not value.root
        ):
            _raise("distribution.assembly.request-invalid", "/bundledPackages", "bundled package availability/root must be non-empty strings")
    if any(not isinstance(value, HostProfileAssembly) for value in request.host_profiles):
        _raise("distribution.assembly.request-invalid", "/hostProfiles", "invalid Host Profile binding")


def _profile_data(
    profile: HostProfileAssembly,
    validators: contracts.ContractValidators,
) -> tuple[dict[str, Any], list[contracts.Diagnostic]]:
    if not isinstance(profile.path, str) or not isinstance(profile.exact_bytes, bytes):
        _raise(
            "distribution.assembly.request-invalid",
            "/hostProfiles",
            "Host Profile path and exact bytes have invalid types",
        )
    _relative_parts(profile.path, "/hostProfiles/path")
    try:
        data = json.loads(profile.exact_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        _raise(
            "distribution.assembly.host-profile-invalid",
            "/hostProfiles",
            f"Host Profile '{profile.path}' is not UTF-8 JSON: {error}",
        )
    return data, contracts.validate_manifest_data(data, profile.path, validators)


def _artifact_manifest_entries(
    receipts: tuple[artifact_publication.PackageArtifactPublicationReceipt, ...],
) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    for receipt in receipts:
        for manifest in receipt.manifests:
            manifest_bytes = artifact_evidence.render_package_artifact_manifest(
                manifest
            ).encode("utf-8")
            entries.append(
                {
                    "artifactGenerationId": receipt.artifact_generation_id,
                    "package": {
                        "id": manifest.package_id,
                        "version": manifest.package_version,
                    },
                    "context": {
                        "hostKind": manifest.host_kind,
                        "targetPlatform": manifest.target_platform,
                        "configuration": manifest.configuration,
                    },
                    "manifestPath": (
                        f"{_ARTIFACTS_DIRECTORY_NAME}/"
                        f"{receipt.artifact_generation_id}/packages/"
                        f"{manifest.package_id}/"
                        f"{contracts.PACKAGE_ARTIFACT_MANIFEST_NAME}"
                    ),
                    "manifestIntegrity": contracts.compute_bytes_integrity(
                        manifest_bytes
                    ),
                }
            )
    return entries


def _make_manifest(
    request: DistributionAssemblyRequest,
    editor_files: list[dict[str, Any]],
    bundled_packages: list[dict[str, Any]],
    profiles: list[tuple[HostProfileAssembly, dict[str, Any]]],
) -> dict[str, Any]:
    manifest = {
        "schema": "com.asharia.engine-distribution",
        "schemaVersion": 1,
        "engineGenerationId": "sha256-" + ("0" * 64),
        "distribution": {
            "id": request.identity.distribution_id,
            "engineVersion": request.identity.engine_version,
            "engineApiVersion": request.identity.engine_api_version,
        },
        "context": {
            "targetPlatform": request.context.target_platform,
            "configuration": request.context.configuration,
            "toolchain": {
                "compilerId": request.context.compiler_id,
                "compilerVersion": request.context.compiler_version,
                "targetSystem": request.context.target_system,
                "targetArchitecture": request.context.target_architecture,
                "runtimeLibrary": request.context.runtime_library,
            },
        },
        "editorImage": {
            "entryPoint": request.editor_image.entry_point,
            "files": editor_files,
        },
        "bundledPackages": bundled_packages,
        "packageArtifacts": _artifact_manifest_entries(request.artifact_publications),
        "hostProfiles": [
            {
                "hostKind": data["hostKind"],
                "targetPlatform": data["targetPlatform"],
                "path": profile.path,
                "integrity": contracts.compute_bytes_integrity(profile.exact_bytes),
            }
            for profile, data in profiles
        ],
    }
    return contracts.normalize_engine_distribution_manifest(manifest)


def _validate_destination_ownership(
    request: DistributionAssemblyRequest,
) -> None:
    owners: list[tuple[str, str, bool]] = [
        (contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME, "Distribution Manifest", False)
    ]
    for file in request.editor_image.files:
        _relative_parts(file.path, "/editorImage/files/path")
        owners.append((file.path, "Editor Image file", False))
    for package in request.bundled_packages:
        _relative_parts(package.root, "/bundledPackages/root")
        owners.append((package.root, "bundled package root", True))
    for receipt in request.artifact_publications:
        if isinstance(receipt, artifact_publication.PackageArtifactPublicationReceipt):
            owners.append(
                (
                    f"{_ARTIFACTS_DIRECTORY_NAME}/{receipt.artifact_generation_id}",
                    "artifact generation root",
                    True,
                )
            )
    for profile in request.host_profiles:
        _relative_parts(profile.path, "/hostProfiles/path")
        owners.append((profile.path, "Host Profile file", False))

    folded: dict[str, tuple[str, str, bool]] = {}
    for path, label, directory in owners:
        previous = folded.get(path.casefold())
        if previous is not None:
            _raise(
                "distribution.assembly.destination-collision",
                "",
                f"{label} '{path}' collides with {previous[1]} '{previous[0]}'",
            )
        folded_path = path.casefold()
        for previous_path, previous_label, _ in folded.values():
            folded_previous_path = previous_path.casefold()
            if folded_path.startswith(f"{folded_previous_path}/") or folded_previous_path.startswith(
                f"{folded_path}/"
            ):
                _raise(
                    "distribution.assembly.destination-overlap",
                    "",
                    f"{label} '{path}' overlaps {previous_label} '{previous_path}'",
                )
        folded[path.casefold()] = (path, label, directory)


def _preflight(
    request: DistributionAssemblyRequest,
    publication_root: Path,
    validators: contracts.ContractValidators,
) -> tuple[
    Path,
    Path,
    dict[str, Path],
    list[dict[str, Any]],
    list[dict[str, Any]],
    list[tuple[HostProfileAssembly, dict[str, Any]]],
    dict[str, dict[str, _FileFingerprint]],
]:
    _validate_typed_request(request)
    _validate_destination_ownership(request)
    canonical_publication_root = _inspect_existing_directory(
        publication_root, "distribution publication root"
    )
    editor_root = _inspect_existing_directory(request.editor_image.root, "Editor Image root")

    source_roots: dict[str, Path] = {"editor": editor_root}
    package_roots: dict[str, Path] = {}
    for package in request.bundled_packages:
        if not isinstance(package.candidate, PackageCandidate):
            _raise(
                "distribution.assembly.request-invalid",
                "/bundledPackages",
                "bundled package candidate has an invalid type",
            )
        root = _inspect_existing_directory(
            package.candidate.payload_location,
            f"package '{package.candidate.identity}' root",
        )
        key = f"package:{package.candidate.identity}"
        if key in source_roots:
            _raise("distribution.assembly.package-duplicate", "/bundledPackages", f"duplicate package '{package.candidate.identity}'")
        source_roots[key] = root
        package_roots[package.candidate.identity] = root

    artifact_ids: set[str] = set()
    for receipt in request.artifact_publications:
        diagnostics = artifact_publication.verify_package_artifact_publication_receipt(
            receipt, validators
        )
        if diagnostics:
            raise _AssemblyFailure(
                _diagnostic(
                    "distribution.assembly.artifact-receipt-invalid",
                    "/packageArtifacts",
                    diagnostics[0].message,
                )
            )
        if receipt.artifact_generation_id in artifact_ids:
            _raise(
                "distribution.assembly.artifact-generation-duplicate",
                "/packageArtifacts",
                f"duplicate artifact generation '{receipt.artifact_generation_id}'",
            )
        artifact_ids.add(receipt.artifact_generation_id)
        source_roots[f"artifact:{receipt.artifact_generation_id}"] = (
            receipt.artifact_generation_path.absolute()
        )

    root_items = list(source_roots.items())
    for index, (label, root) in enumerate(root_items):
        if _paths_overlap(root, canonical_publication_root):
            _raise(
                "distribution.assembly.root-overlap",
                "",
                f"source root '{label}' overlaps publication root",
            )
        for other_label, other_root in root_items[:index]:
            if _paths_overlap(root, other_root):
                _raise(
                    "distribution.assembly.root-overlap",
                    "",
                    f"source roots '{other_label}' and '{label}' overlap",
                )

    editor_files, editor_directories = _scan_regular_tree(editor_root)
    expected_editor_files = {file.path for file in request.editor_image.files}
    expected_editor_directories = set().union(
        *(_parents(path) for path in expected_editor_files)
    ) if expected_editor_files else set()
    if set(editor_files) != expected_editor_files or editor_directories != expected_editor_directories:
        _raise(
            "distribution.assembly.editor-layout-mismatch",
            "/editorImage/files",
            "Editor Image root is not closed by its explicit file bindings",
        )

    editor_entries: list[dict[str, Any]] = []
    for binding in request.editor_image.files:
        size, integrity, fingerprint = _hash_regular_file(
            editor_root.joinpath(*binding.path.split("/"))
        )
        if fingerprint != editor_files[binding.path]:
            _raise("distribution.assembly.source-drift", "/editorImage/files", f"Editor file '{binding.path}' changed during preflight")
        editor_entries.append(
            {
                "path": binding.path,
                "role": binding.role,
                "mediaType": binding.media_type,
                "size": size,
                "integrity": integrity,
            }
        )

    bundled_entries: list[dict[str, Any]] = []
    package_fingerprints: dict[str, dict[str, _FileFingerprint]] = {}
    package_ids: dict[str, BundledPackageAssembly] = {}
    for package in request.bundled_packages:
        candidate = package.candidate
        if candidate.identity in package_ids:
            _raise("distribution.assembly.package-duplicate", "/bundledPackages", f"duplicate package '{candidate.identity}'")
        package_ids[candidate.identity] = package
        root = package_roots[candidate.identity]
        author_manifest = _read_utf8_json_file(
            root / contracts.PACKAGE_MANIFEST_NAME,
            f"package '{candidate.identity}' author manifest",
        )
        if author_manifest != candidate.manifest:
            _raise(
                "distribution.assembly.package-candidate-mismatch",
                "/bundledPackages",
                f"package candidate '{candidate.identity}' author snapshot is stale",
            )
        manifest_diagnostics = contracts.validate_manifest_data(
            author_manifest,
            f"{candidate.identity}/{contracts.PACKAGE_MANIFEST_NAME}",
            validators,
        )
        if manifest_diagnostics:
            raise _AssemblyFailure(
                _diagnostic(
                    "distribution.assembly.package-invalid",
                    "/bundledPackages",
                    manifest_diagnostics[0].message,
                )
            )
        if (
            candidate.manifest.get("id") != candidate.identity
            or candidate.manifest.get("version") != candidate.version
            or candidate.manifest.get("packageKind") != candidate.package_kind
        ):
            _raise(
                "distribution.assembly.package-candidate-mismatch",
                "/bundledPackages",
                f"package candidate '{candidate.identity}' identity fields are stale",
            )
        manifest_integrity = _hash_regular_file(root / contracts.PACKAGE_MANIFEST_NAME)[1]
        try:
            payload_integrity = contracts.compute_package_tree_integrity(root)
        except contracts.PackageTreeIntegrityError as error:
            _raise("distribution.assembly.package-tree-invalid", "/bundledPackages", str(error))
        if (
            manifest_integrity != candidate.manifest_integrity
            or payload_integrity != candidate.payload_integrity
        ):
            _raise(
                "distribution.assembly.package-evidence-stale",
                "/bundledPackages",
                f"package candidate '{candidate.identity}' no longer matches its source evidence",
            )
        package_fingerprints[candidate.identity] = _scan_regular_tree(
            root,
            excluded_root_names=frozenset(contracts.PACKAGE_TREE_ROOT_EXCLUDES),
        )[0]
        bundled_entries.append(
            {
                "id": candidate.identity,
                "version": candidate.version,
                "packageKind": candidate.package_kind,
                "availability": package.availability,
                "root": package.root,
                "manifestIntegrity": manifest_integrity,
                "payloadIntegrity": payload_integrity,
            }
        )

    bundled_installable = {
        identity: value
        for identity, value in package_ids.items()
        if value.candidate.package_kind == "installable-capability"
    }
    artifact_keys: set[tuple[str, str, str, str]] = set()
    for receipt in request.artifact_publications:
        for manifest in receipt.manifests:
            bundled = bundled_installable.get(manifest.package_id)
            if bundled is None or bundled.candidate.version != manifest.package_version:
                _raise(
                    "distribution.assembly.artifact-package-mismatch",
                    "/packageArtifacts",
                    f"artifact package '{manifest.package_id}' is not the exact bundled installable package",
                )
            if (
                manifest.target_platform != request.context.target_platform
                or manifest.configuration != request.context.configuration
            ):
                _raise(
                    "distribution.assembly.artifact-context-mismatch",
                    "/packageArtifacts",
                    f"artifact package '{manifest.package_id}' does not match Distribution context",
                )
            key = (
                manifest.package_id,
                manifest.host_kind,
                manifest.target_platform,
                manifest.configuration,
            )
            if key in artifact_keys:
                _raise(
                    "distribution.assembly.artifact-context-duplicate",
                    "/packageArtifacts",
                    f"duplicate artifact context for package '{manifest.package_id}'",
                )
            artifact_keys.add(key)

    profiles: list[tuple[HostProfileAssembly, dict[str, Any]]] = []
    profile_diagnostics: list[contracts.Diagnostic] = []
    for profile in request.host_profiles:
        data, diagnostics = _profile_data(profile, validators)
        profiles.append((profile, data))
        profile_diagnostics.extend(diagnostics)
        if not diagnostics and data["targetPlatform"] != request.context.target_platform:
            _raise(
                "distribution.assembly.host-profile-context-mismatch",
                "/hostProfiles",
                f"Host Profile '{profile.path}' target platform differs from Distribution",
            )
    if profile_diagnostics:
        return_diagnostic = profile_diagnostics[0]
        raise _AssemblyFailure(
            _diagnostic(
                "distribution.assembly.host-profile-invalid",
                "/hostProfiles",
                return_diagnostic.message,
            )
        )

    provisional = _make_manifest(
        request,
        editor_entries,
        bundled_entries,
        profiles,
    )
    provisional_diagnostics = contracts.validate_manifest_data(
        provisional,
        contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME,
        validators,
    )
    if provisional_diagnostics:
        raise _AssemblyFailure(provisional_diagnostics[0])
    return (
        canonical_publication_root,
        editor_root,
        package_roots,
        editor_entries,
        bundled_entries,
        profiles,
        package_fingerprints,
    )


def _copy_tree(
    source_root: Path,
    destination_root: Path,
    files: dict[str, _FileFingerprint],
) -> dict[str, _FileFingerprint]:
    staged: dict[str, _FileFingerprint] = {}
    for relative, expected_fingerprint in sorted(
        files.items(), key=lambda value: _utf8_key(value[0])
    ):
        _, _, source_fingerprint, staged_fingerprint = _copy_regular_file(
            source_root.joinpath(*relative.split("/")),
            destination_root.joinpath(*relative.split("/")),
        )
        if source_fingerprint != expected_fingerprint:
            _raise(
                "distribution.assembly.source-drift",
                "",
                f"source file '{relative}' changed after preflight",
            )
        staged[relative] = staged_fingerprint
    return staged


def _verify_distribution_tree(
    generation_root: Path,
    expected_manifest: dict[str, Any],
    artifact_receipts: tuple[
        artifact_publication.PackageArtifactPublicationReceipt, ...
    ],
    validators: contracts.ContractValidators,
    *,
    require_generation_name: bool = True,
) -> None:
    if (
        require_generation_name
        and generation_root.name != expected_manifest["engineGenerationId"]
    ):
        _raise(
            "distribution.assembly.generation-path-mismatch",
            "",
            "generation directory name does not match EngineGenerationId",
        )
    manifest_path = generation_root / contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME
    expected_bytes = contracts.render_normalized_engine_distribution_manifest(
        expected_manifest
    ).encode("utf-8")
    try:
        with manifest_path.open("rb") as manifest_file:
            actual_bytes = manifest_file.read()
    except OSError as error:
        _raise(
            "distribution.assembly.existing-corrupt",
            "",
            f"could not read Distribution Manifest: {error}",
        )
    if actual_bytes != expected_bytes:
        _raise(
            "distribution.assembly.existing-corrupt",
            "",
            "Distribution Manifest differs from the expected canonical inventory",
        )
    try:
        parsed = json.loads(actual_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        _raise("distribution.assembly.existing-corrupt", "", f"Distribution Manifest is invalid: {error}")
    diagnostics = contracts.validate_manifest_data(parsed, str(manifest_path), validators)
    if diagnostics:
        raise _AssemblyFailure(diagnostics[0])

    allowed_exact = {contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME}
    for file in expected_manifest["editorImage"]["files"]:
        path = generation_root.joinpath(*file["path"].split("/"))
        size, integrity, _ = _hash_regular_file(path)
        if size != file["size"] or integrity != file["integrity"]:
            _raise("distribution.assembly.existing-corrupt", "/editorImage/files", f"Editor file '{file['path']}' is corrupt")
        allowed_exact.add(file["path"])

    package_roots: list[str] = []
    for package in expected_manifest["bundledPackages"]:
        root_text = package["root"]
        root = generation_root.joinpath(*root_text.split("/"))
        manifest_integrity = _hash_regular_file(root / contracts.PACKAGE_MANIFEST_NAME)[1]
        try:
            payload_integrity = contracts.compute_package_tree_integrity(root)
        except (contracts.PackageTreeIntegrityError, OSError) as error:
            _raise("distribution.assembly.existing-corrupt", "/bundledPackages", f"bundled package '{package['id']}' is corrupt: {error}")
        if manifest_integrity != package["manifestIntegrity"] or payload_integrity != package["payloadIntegrity"]:
            _raise("distribution.assembly.existing-corrupt", "/bundledPackages", f"bundled package '{package['id']}' evidence is corrupt")
        package_files, _ = _scan_regular_tree(root)
        if any(relative.split("/", 1)[0] in contracts.PACKAGE_TREE_ROOT_EXCLUDES for relative in package_files):
            _raise("distribution.assembly.existing-corrupt", "/bundledPackages", f"bundled package '{package['id']}' contains excluded publication content")
        package_roots.append(root_text)

    for profile in expected_manifest["hostProfiles"]:
        path = generation_root.joinpath(*profile["path"].split("/"))
        _, integrity, _ = _hash_regular_file(path)
        if integrity != profile["integrity"]:
            _raise("distribution.assembly.existing-corrupt", "/hostProfiles", f"Host Profile '{profile['path']}' is corrupt")
        allowed_exact.add(profile["path"])

    artifact_roots: list[str] = []
    for receipt in artifact_receipts:
        relative_root = f"{_ARTIFACTS_DIRECTORY_NAME}/{receipt.artifact_generation_id}"
        staged_receipt = replace(
            receipt,
            artifact_generation_path=generation_root.joinpath(*relative_root.split("/")),
        )
        artifact_diagnostics = artifact_publication.verify_package_artifact_publication_receipt(
            staged_receipt, validators
        )
        if artifact_diagnostics:
            _raise(
                "distribution.assembly.existing-corrupt",
                "/packageArtifacts",
                artifact_diagnostics[0].message,
            )
        artifact_roots.append(relative_root)

    actual_files, actual_directories = _scan_regular_tree(generation_root)
    for relative in actual_files:
        if relative in allowed_exact:
            continue
        if any(relative.startswith(f"{root}/") for root in package_roots):
            continue
        if any(relative.startswith(f"{root}/") for root in artifact_roots):
            continue
        _raise(
            "distribution.assembly.layout-mismatch",
            "",
            f"generation contains undeclared file '{relative}'",
        )
    expected_directories = set().union(
        *(_parents(relative) for relative in actual_files)
    ) if actual_files else set()
    if actual_directories != expected_directories:
        _raise(
            "distribution.assembly.layout-mismatch",
            "",
            "generation contains missing or empty undeclared directories",
        )


def _cleanup_staging(path: Path) -> contracts.Diagnostic | None:
    if not os.path.lexists(path):
        return None
    try:
        shutil.rmtree(path)
    except OSError as error:
        return _diagnostic(
            "distribution.assembly.cleanup-failed",
            "",
            f"could not remove owned staging '{_path_text(path)}': {error}",
        )
    return None


def assemble_engine_distribution(
    request: Any,
    publication_root: Path,
    validators: contracts.ContractValidators,
) -> EngineDistributionAssemblyResult:
    """Stage, verify, and atomically publish one immutable Distribution."""

    staging_path: Path | None = None
    try:
        (
            canonical_publication_root,
            editor_root,
            package_roots,
            _,
            _,
            profiles,
            package_fingerprints,
        ) = _preflight(request, publication_root, validators)

        publication_io_root = stable_file_identity.extended_path(
            canonical_publication_root
        )
        publication_logical_root = stable_file_identity.standard_path(
            publication_io_root
        )
        staging_parent = publication_io_root / _STAGING_DIRECTORY_NAME
        generations_parent = publication_io_root / _GENERATIONS_DIRECTORY_NAME
        staging_parent.mkdir(exist_ok=True)
        generations_parent.mkdir(exist_ok=True)
        canonical_staging_parent = _inspect_existing_directory(staging_parent, "Distribution staging root")
        canonical_generations_parent = _inspect_existing_directory(generations_parent, "Distribution generations root")
        if canonical_staging_parent.stat().st_dev != canonical_generations_parent.stat().st_dev:
            _raise(
                "distribution.assembly.cross-filesystem",
                "",
                "staging and generation roots must use the same filesystem",
            )
        staging_path = Path(tempfile.mkdtemp(prefix="generation-", dir=canonical_staging_parent))

        staged_editor_entries: list[dict[str, Any]] = []
        editor_source_files = _scan_regular_tree(editor_root)[0]
        for binding in sorted(request.editor_image.files, key=lambda value: _utf8_key(value.path)):
            relative = binding.path
            size, integrity, source_fingerprint, _ = _copy_regular_file(
                editor_root.joinpath(*relative.split("/")),
                staging_path.joinpath(*relative.split("/")),
            )
            if source_fingerprint != editor_source_files[relative]:
                _raise("distribution.assembly.source-drift", "/editorImage/files", f"Editor file '{relative}' changed after preflight")
            staged_editor_entries.append(
                {
                    "path": relative,
                    "role": binding.role,
                    "mediaType": binding.media_type,
                    "size": size,
                    "integrity": integrity,
                }
            )
        if _scan_regular_tree(editor_root)[0] != editor_source_files:
            _raise(
                "distribution.assembly.source-drift",
                "/editorImage/files",
                "Editor Image root changed during assembly",
            )

        staged_bundled_entries: list[dict[str, Any]] = []
        for package in sorted(request.bundled_packages, key=lambda value: _utf8_key(value.candidate.identity)):
            candidate = package.candidate
            source_root = package_roots[candidate.identity]
            destination_root = staging_path.joinpath(*package.root.split("/"))
            _copy_tree(
                source_root,
                destination_root,
                package_fingerprints[candidate.identity],
            )
            if (
                _scan_regular_tree(
                    source_root,
                    excluded_root_names=frozenset(
                        contracts.PACKAGE_TREE_ROOT_EXCLUDES
                    ),
                )[0]
                != package_fingerprints[candidate.identity]
            ):
                _raise(
                    "distribution.assembly.source-drift",
                    "/bundledPackages",
                    f"package '{candidate.identity}' changed during assembly",
                )
            try:
                source_integrity = contracts.compute_package_tree_integrity(source_root)
                staged_integrity = contracts.compute_package_tree_integrity(destination_root)
            except contracts.PackageTreeIntegrityError as error:
                _raise("distribution.assembly.package-tree-invalid", "/bundledPackages", str(error))
            if source_integrity != candidate.payload_integrity:
                _raise("distribution.assembly.source-drift", "/bundledPackages", f"package '{candidate.identity}' changed after preflight")
            staged_manifest_integrity = _hash_regular_file(
                destination_root / contracts.PACKAGE_MANIFEST_NAME
            )[1]
            if staged_integrity != candidate.payload_integrity or staged_manifest_integrity != candidate.manifest_integrity:
                _raise("distribution.assembly.staging-integrity-mismatch", "/bundledPackages", f"staged package '{candidate.identity}' differs from verified source")
            staged_bundled_entries.append(
                {
                    "id": candidate.identity,
                    "version": candidate.version,
                    "packageKind": candidate.package_kind,
                    "availability": package.availability,
                    "root": package.root,
                    "manifestIntegrity": staged_manifest_integrity,
                    "payloadIntegrity": staged_integrity,
                }
            )

        for receipt in sorted(request.artifact_publications, key=lambda value: value.artifact_generation_id):
            source_files = _scan_regular_tree(receipt.artifact_generation_path)[0]
            destination_root = staging_path / _ARTIFACTS_DIRECTORY_NAME / receipt.artifact_generation_id
            _copy_tree(receipt.artifact_generation_path, destination_root, source_files)
            if _scan_regular_tree(receipt.artifact_generation_path)[0] != source_files:
                _raise(
                    "distribution.assembly.source-drift",
                    "/packageArtifacts",
                    f"artifact generation '{receipt.artifact_generation_id}' changed during assembly",
                )
            source_diagnostics = artifact_publication.verify_package_artifact_publication_receipt(receipt, validators)
            if source_diagnostics:
                _raise("distribution.assembly.source-drift", "/packageArtifacts", source_diagnostics[0].message)
            staged_receipt = replace(receipt, artifact_generation_path=destination_root)
            staged_diagnostics = artifact_publication.verify_package_artifact_publication_receipt(staged_receipt, validators)
            if staged_diagnostics:
                _raise("distribution.assembly.staging-integrity-mismatch", "/packageArtifacts", staged_diagnostics[0].message)

        for profile, _ in profiles:
            _write_exact_file(
                staging_path.joinpath(*profile.path.split("/")),
                profile.exact_bytes,
            )

        manifest = _make_manifest(
            request,
            staged_editor_entries,
            staged_bundled_entries,
            profiles,
        )
        manifest_diagnostics = contracts.validate_manifest_data(
            manifest,
            contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME,
            validators,
        )
        if manifest_diagnostics:
            raise _AssemblyFailure(manifest_diagnostics[0])
        manifest_bytes = contracts.render_normalized_engine_distribution_manifest(
            manifest
        ).encode("utf-8")
        _write_exact_file(
            staging_path / contracts.ENGINE_DISTRIBUTION_MANIFEST_NAME,
            manifest_bytes,
        )

        engine_generation_id = manifest["engineGenerationId"]
        final_path = canonical_generations_parent / engine_generation_id
        logical_final_path = (
            publication_logical_root
            / _GENERATIONS_DIRECTORY_NAME
            / engine_generation_id
        )
        _verify_distribution_tree(
            staging_path,
            manifest,
            request.artifact_publications,
            validators,
            require_generation_name=False,
        )

        reused = False
        if os.path.lexists(final_path):
            _inspect_existing_directory(final_path, "existing Engine Distribution generation")
            _verify_distribution_tree(final_path, manifest, request.artifact_publications, validators)
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
                        "distribution.assembly.rename-failed",
                        "",
                        f"could not publish Engine Distribution '{engine_generation_id}' with one rename: {error}",
                    )
                _inspect_existing_directory(final_path, "concurrent Engine Distribution generation")
                _verify_distribution_tree(final_path, manifest, request.artifact_publications, validators)
                cleanup_diagnostic = _cleanup_staging(staging_path)
                if cleanup_diagnostic is not None:
                    return _failure([cleanup_diagnostic])
                reused = True

        return EngineDistributionAssemblyResult(
            receipt=EngineDistributionAssemblyReceipt(
                engine_generation_id=engine_generation_id,
                engine_generation_path=logical_final_path,
                manifest=manifest,
                manifest_integrity=contracts.compute_bytes_integrity(manifest_bytes),
                reused=reused,
            ),
            diagnostics=(),
        )
    except _AssemblyFailure as error:
        diagnostics = [error.diagnostic]
    except OSError as error:
        diagnostics = [
            _diagnostic(
                "distribution.assembly.io-failed",
                "",
                f"Engine Distribution assembly failed: {error}",
            )
        ]
    if staging_path is not None:
        cleanup_diagnostic = _cleanup_staging(staging_path)
        if cleanup_diagnostic is not None:
            diagnostics.append(cleanup_diagnostic)
    return _failure(diagnostics)
