"""Typed request and outcome records for Host binding publication."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

from tools import check_package_contracts as contracts
from tools import host_cmake_target
from tools import host_executable_binding as binding
from tools import host_executable_template as host_template
from tools import host_registration_snapshot
from tools import host_registration_verification
from tools import static_composition_root as composition


@dataclass(frozen=True)
class HostExecutableBindingPublicationRequestV1:
    """Committed generated inputs and exact #290 build handoffs."""

    composition_generation: composition.StaticCompositionRootGeneration
    composition_root: Path = field(repr=False)
    template_generation: host_template.WindowsDevelopmentHostTemplateGenerationV1
    template_root: Path = field(repr=False)
    target: host_cmake_target.HostCMakeTargetEvidence
    registration_snapshot: host_registration_snapshot.HostRegistrationSnapshot
    environment: tuple[tuple[str, str], ...] = field(repr=False)
    verification_timeout_seconds: float = 60.0
    max_snapshot_bytes: int = (
        host_registration_verification.DEFAULT_MAX_REGISTRATION_SNAPSHOT_BYTES
    )


@dataclass(frozen=True)
class HostExecutableBindingPublicationReceipt:
    binding_generation_id: str
    generation_path: Path = field(repr=False)
    receipt: binding.HostExecutableBindingReceiptV1
    registration_snapshot: host_registration_snapshot.HostRegistrationSnapshot
    reused: bool


@dataclass(frozen=True)
class HostExecutableBindingPublicationResult:
    receipt: HostExecutableBindingPublicationReceipt | None
    diagnostics: tuple[contracts.Diagnostic, ...]

    @property
    def succeeded(self) -> bool:
        return self.receipt is not None and not self.diagnostics


def diagnostic(code: str, pointer: str, message: str) -> contracts.Diagnostic:
    return contracts.Diagnostic(
        code,
        binding.HOST_EXECUTABLE_BINDING_RECEIPT_NAME,
        pointer,
        message,
    )


def failure_result(
    diagnostics: Iterable[contracts.Diagnostic],
) -> HostExecutableBindingPublicationResult:
    unique = {
        (value.manifest_path, value.pointer, value.code, value.message): value
        for value in diagnostics
    }
    ordered = sorted(
        unique.values(),
        key=lambda value: (
            value.manifest_path,
            value.pointer,
            value.code,
            value.message,
        ),
    )
    return HostExecutableBindingPublicationResult(None, tuple(ordered))


__all__ = [
    "HostExecutableBindingPublicationReceipt",
    "HostExecutableBindingPublicationRequestV1",
    "HostExecutableBindingPublicationResult",
    "diagnostic",
    "failure_result",
]
