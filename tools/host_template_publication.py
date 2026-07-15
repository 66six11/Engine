"""Immutable publication for generated Windows Development Host Templates."""

from __future__ import annotations

import os
import shutil
import stat
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

from tools import check_package_contracts as contracts
from tools import generated_publication_tree
from tools import host_executable_template as host_template
from tools import static_composition_root as composition


@dataclass(frozen=True)
class HostTemplatePublicationReceipt:
    """Committed Host Template location and reuse status."""

    generation_id: str
    generation_path: Path = field(repr=False)
    manifest_integrity: composition.IntegrityRecord
    reused: bool


@dataclass(frozen=True)
class HostTemplatePublicationResult:
    """Atomic publication result."""

    receipt: HostTemplatePublicationReceipt | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.receipt is not None and not self.diagnostics


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
        manifest_path=host_template.HOST_TEMPLATE_NAME,
        pointer=pointer,
        message=message,
    )


def _failure(
    diagnostics: Iterable[contracts.Diagnostic],
) -> HostTemplatePublicationResult:
    return HostTemplatePublicationResult(
        receipt=None,
        diagnostics=tuple(sorted(diagnostics, key=_diagnostic_sort_key)),
    )


def _is_link_or_reparse(status: os.stat_result) -> bool:
    if stat.S_ISLNK(status.st_mode):
        return True
    reparse_attribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    return bool(getattr(status, "st_file_attributes", 0) & reparse_attribute)


def _prepare_publication_root(path: Path) -> Path:
    if not isinstance(path, Path):
        raise OSError("publication root must use an explicit pathlib.Path")
    absolute = path.absolute()

    def inspect_existing_chain() -> None:
        current = Path(absolute.anchor)
        for component in absolute.parts[1:]:
            current /= component
            try:
                status = current.lstat()
            except FileNotFoundError:
                break
            if _is_link_or_reparse(status):
                raise OSError(
                    f"publication root crosses link/reparse point '{current}'"
                )
            if not stat.S_ISDIR(status.st_mode):
                raise OSError(
                    f"publication root component is not a directory: '{current}'"
                )

    inspect_existing_chain()
    absolute.mkdir(parents=True, exist_ok=True)
    inspect_existing_chain()
    return absolute.resolve(strict=True)


def publish_windows_development_host_template(
    generation: host_template.WindowsDevelopmentHostTemplateGenerationV1,
    destination_root: Path,
    validators: contracts.ContractValidators,
) -> HostTemplatePublicationResult:
    """Publish one immutable Host Template generation or reuse exact bytes."""

    diagnostics = host_template.validate_windows_development_host_template_generation(
        generation, validators
    )
    if diagnostics:
        return _failure(diagnostics)

    expected = host_template.expected_host_template_publication_files(generation)
    staging_path: Path | None = None
    try:
        destination_root = _prepare_publication_root(destination_root)
        final_path = destination_root / generation.manifest.generation_id
        if os.path.lexists(final_path):
            generated_publication_tree.verify_exact_publication_tree(
                final_path, expected
            )
            return HostTemplatePublicationResult(
                receipt=HostTemplatePublicationReceipt(
                    generation.manifest.generation_id,
                    final_path,
                    generation.manifest.integrity,
                    True,
                ),
                diagnostics=(),
            )

        staging_path = Path(
            tempfile.mkdtemp(prefix=".asharia-host-template-", dir=destination_root)
        )
        for relative, content in expected.items():
            destination = staging_path / Path(relative)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(content)
        generated_publication_tree.verify_exact_publication_tree(
            staging_path, expected
        )
        try:
            os.rename(staging_path, final_path)
            staging_path = None
            reused = False
        except OSError:
            if not os.path.lexists(final_path):
                raise
            generated_publication_tree.verify_exact_publication_tree(
                final_path, expected
            )
            reused = True
        return HostTemplatePublicationResult(
            receipt=HostTemplatePublicationReceipt(
                generation.manifest.generation_id,
                final_path,
                generation.manifest.integrity,
                reused,
            ),
            diagnostics=(),
        )
    except OSError as error:
        return _failure(
            [
                _diagnostic(
                    "host-template.publication-failed",
                    "/generationId",
                    f"could not publish immutable Host Template: {error}",
                )
            ]
        )
    finally:
        if staging_path is not None:
            shutil.rmtree(staging_path, ignore_errors=True)
