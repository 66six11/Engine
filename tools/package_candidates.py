"""Shared in-memory package candidate contract."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


@dataclass(frozen=True)
class PackageCandidate:
    """One exact package payload offered to the deterministic resolver."""

    identity: str
    version: str
    package_kind: str
    origin: str
    source: dict[str, Any]
    manifest_integrity: dict[str, Any]
    payload_integrity: dict[str, Any]
    manifest: dict[str, Any]
    build_descriptor: dict[str, Any] | None = None
    build_descriptor_integrity: dict[str, Any] | None = None
    build_descriptor_bytes: bytes | None = field(default=None, repr=False)
    payload_location: Any = field(default=None, compare=False, repr=False)
