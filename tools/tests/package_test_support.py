"""Shared v2 Project/Distribution builders for package-runtime tests."""

from __future__ import annotations

from typing import Any, Iterable

from tools import check_package_contracts as contracts


DEFAULT_DISTRIBUTION_ID = "com.asharia.distribution.test-engine"
DEFAULT_ENGINE_API_VERSION = "0.1.0"


def engine_requirement(
    distribution_id: str = DEFAULT_DISTRIBUTION_ID,
    engine_api_version: str = DEFAULT_ENGINE_API_VERSION,
) -> dict[str, Any]:
    """Return an exact API requirement suitable for one Project Manifest v2."""

    return {
        "distributionId": distribution_id,
        "apiVersion": {"kind": "exact", "version": engine_api_version},
    }


def make_engine_distribution(
    candidates: Iterable[Any] = (),
    *,
    distribution_id: str = DEFAULT_DISTRIBUTION_ID,
    engine_api_version: str = DEFAULT_ENGINE_API_VERSION,
) -> dict[str, Any]:
    """Build one schema-valid Distribution whose inventory owns distributed candidates."""

    distributed = sorted(
        (
            candidate
            for candidate in candidates
            if isinstance(candidate.source, dict)
            and candidate.source.get("kind") == "engine-distribution"
        ),
        key=lambda candidate: candidate.identity.encode("utf-8"),
    )
    bundled_packages = [
        {
            "id": candidate.identity,
            "version": candidate.version,
            "packageKind": candidate.package_kind,
            "availability": "default",
            "root": (
                candidate.origin.removeprefix("engine-distribution:")
                if candidate.origin.startswith("engine-distribution:")
                else f"packages/{index:04d}"
            ),
            "manifestIntegrity": candidate.manifest_integrity,
            "payloadIntegrity": candidate.payload_integrity,
        }
        for index, candidate in enumerate(distributed)
    ]
    if not bundled_packages:
        bundled_packages.append(
            {
                "id": "com.asharia.foundation.test-bootstrap",
                "version": "0.1.0",
                "packageKind": "installable-capability",
                "availability": "required",
                "root": "packages/test-bootstrap",
                "manifestIntegrity": {"algorithm": "sha256", "digest": "1" * 64},
                "payloadIntegrity": {"algorithm": "sha256", "digest": "2" * 64},
            }
        )

    manifest = {
        "schema": "com.asharia.engine-distribution",
        "schemaVersion": 1,
        "engineGenerationId": f"sha256-{'0' * 64}",
        "distribution": {
            "id": distribution_id,
            "engineVersion": "0.1.0",
            "engineApiVersion": engine_api_version,
        },
        "context": {
            "targetPlatform": "com.asharia.platform.windows",
            "configuration": "Debug",
            "toolchain": {
                "compilerId": "test",
                "compilerVersion": "0.1.0",
                "targetSystem": "Windows",
                "targetArchitecture": "x86_64",
                "runtimeLibrary": "test",
            },
        },
        "editorImage": {
            "entryPoint": "bin/asharia-editor.exe",
            "files": [
                {
                    "path": "bin/asharia-editor.exe",
                    "role": "executable",
                    "mediaType": "application/octet-stream",
                    "size": 1,
                    "integrity": {"algorithm": "sha256", "digest": "3" * 64},
                }
            ],
        },
        "bundledPackages": bundled_packages,
        "packageArtifacts": [],
        "hostProfiles": [
            {
                "hostKind": "editor",
                "targetPlatform": "com.asharia.platform.windows",
                "path": "profiles/editor/asharia.host-profile.json",
                "integrity": {"algorithm": "sha256", "digest": "4" * 64},
            }
        ],
    }
    manifest["engineGenerationId"] = contracts.compute_engine_generation_id(manifest)
    return manifest
