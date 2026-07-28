"""Bind one CMake Host target to its lexical primary artifact path."""

from __future__ import annotations

import stat
import unicodedata
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import Any

from tools import host_cmake_query as query
from tools import host_cmake_reply as reply


@dataclass(frozen=True)
class HostCMakeArtifactBinding:
    name_on_disk: str
    relative_path: str
    path: Path = field(repr=False)


def _name_on_disk(value: Any) -> str:
    if not isinstance(value, str) or unicodedata.normalize("NFC", value) != value:
        raise reply.ReplyFailure(
            [
                query.diagnostic(
                    "host-build.cmake-name-on-disk-invalid",
                    "/codemodel/target/nameOnDisk",
                    "executable target must expose one normalized nameOnDisk",
                )
            ]
        )
    path = PurePosixPath(value)
    if (
        not value
        or path.is_absolute()
        or len(path.parts) != 1
        or path.name != value
        or value in {".", ".."}
        or "\\" in value
        or ":" in value
    ):
        raise reply.ReplyFailure(
            [
                query.diagnostic(
                    "host-build.cmake-name-on-disk-invalid",
                    "/codemodel/target/nameOnDisk",
                    "executable target must expose one normalized nameOnDisk",
                )
            ]
        )
    return value


def _primary_artifact(target: dict[str, Any], name_on_disk: str) -> str:
    artifacts = target.get("artifacts")
    if not isinstance(artifacts, list):
        artifacts = []
    matches = [
        value
        for artifact in artifacts
        if isinstance(artifact, dict)
        for value in [artifact.get("path")]
        if isinstance(value, str) and PurePosixPath(value).name == name_on_disk
    ]
    if len(matches) != 1:
        raise reply.ReplyFailure(
            [
                query.diagnostic(
                    "host-build.cmake-primary-artifact-mismatch",
                    "/codemodel/target/artifacts",
                    f"target must expose exactly one artifact named '{name_on_disk}'",
                )
            ]
        )
    return matches[0]


def _path_failure(
    message: str = "primary artifact must resolve inside the explicit build root",
) -> reply.ReplyFailure:
    return reply.ReplyFailure(
        [
            query.diagnostic(
                "host-build.cmake-artifact-path-invalid",
                "/codemodel/target/artifacts/path",
                message,
            )
        ]
    )


def _artifact_path(
    build_root: Path,
    value: str,
    *,
    require_artifact: bool,
) -> tuple[str, Path]:
    if (
        not value
        or unicodedata.normalize("NFC", value) != value
        or "\\" in value
    ):
        raise _path_failure()
    raw_path = Path(value)
    if raw_path.is_absolute():
        candidate = raw_path
    else:
        pure = PurePosixPath(value)
        if any(part in {"", ".", ".."} for part in pure.parts) or ":" in value:
            raise _path_failure()
        candidate = build_root.joinpath(*pure.parts)
    try:
        candidate = candidate.absolute()
        relative = candidate.relative_to(build_root).as_posix()
    except ValueError:
        raise _path_failure() from None

    status: Any = None
    if require_artifact:
        try:
            status = candidate.lstat()
        except OSError:
            raise reply.ReplyFailure(
                [
                    query.diagnostic(
                        "host-build.cmake-artifact-missing",
                        "/artifact",
                        "built primary artifact is missing or unreadable",
                    )
                ]
            ) from None
    if not query.path_components_without_links(
        candidate,
        allow_missing=not require_artifact,
        final_kind="regular" if require_artifact else "any",
    ):
        if not require_artifact:
            raise _path_failure(
                "primary artifact path must not cross link or reparse components"
            )
        raise reply.ReplyFailure(
            [
                query.diagnostic(
                    "host-build.cmake-artifact-not-regular",
                    "/artifact",
                    "built primary artifact must be a regular non-link file",
                )
            ]
        )
    if not require_artifact:
        return relative, candidate

    assert status is not None
    if query.is_link_or_reparse(status) or not stat.S_ISREG(status.st_mode):
        raise reply.ReplyFailure(
            [
                query.diagnostic(
                    "host-build.cmake-artifact-not-regular",
                    "/artifact",
                    "built primary artifact must be a regular non-link file",
                )
            ]
        )
    try:
        resolved = candidate.resolve(strict=True)
        resolved.relative_to(build_root)
    except (OSError, ValueError):
        raise _path_failure(
            "built primary artifact must remain inside the explicit build root"
        ) from None
    return relative, resolved


def bind_primary_host_artifact(
    target: dict[str, Any],
    build_root: Path,
    *,
    require_artifact: bool,
) -> HostCMakeArtifactBinding:
    name_on_disk = _name_on_disk(target.get("nameOnDisk"))
    artifact = _primary_artifact(target, name_on_disk)
    relative, path = _artifact_path(
        build_root,
        artifact,
        require_artifact=require_artifact,
    )
    return HostCMakeArtifactBinding(name_on_disk, relative, path)


__all__ = [
    "HostCMakeArtifactBinding",
    "bind_primary_host_artifact",
]
