"""Deterministic in-memory Package Resolver v1 tests."""

from __future__ import annotations

import copy
import hashlib
import json
import unittest
from pathlib import Path

from tools import check_package_contracts
from tools import package_resolver
from tools.tests import package_test_support


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"
ENGINE_API_VERSION = "0.1.0"


def exact(version: str) -> dict[str, object]:
    return {"kind": "exact", "version": version}


def version_range(
    minimum: str,
    maximum: str,
    *,
    allow_prerelease: bool = False,
) -> dict[str, object]:
    return {
        "kind": "range",
        "minimumInclusive": minimum,
        "maximumExclusive": maximum,
        "allowPrerelease": allow_prerelease,
    }


def requirement(identity: str, constraint: dict[str, object]) -> dict[str, object]:
    return {"id": identity, "version": constraint}


class PackageResolverTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = check_package_contracts.load_contract_validators()
        cls.installable_template = json.loads(
            (FIXTURE_ROOT / "valid-system.json").read_text(encoding="utf-8")
        )
        cls.feature_set_template = json.loads(
            (FIXTURE_ROOT / "valid-feature-set.json").read_text(encoding="utf-8")
        )

    def project(
        self,
        *,
        packages: list[dict[str, object]] | None = None,
        feature_sets: list[dict[str, object]] | None = None,
        options: list[dict[str, object]] | None = None,
    ) -> dict[str, object]:
        return {
            "schema": "com.asharia.project-packages",
            "schemaVersion": 2,
            "engine": package_test_support.engine_requirement(),
            "directPackages": packages or [],
            "directFeatureSets": feature_sets or [],
            "packageOptions": options or [],
        }

    def installable(
        self,
        identity: str,
        version: str,
        *,
        dependencies: list[dict[str, object]] | None = None,
        engine_api: dict[str, object] | None = None,
    ) -> dict[str, object]:
        manifest = copy.deepcopy(self.installable_template)
        manifest["id"] = identity
        manifest["version"] = version
        manifest["displayName"] = identity
        manifest["dependencies"] = dependencies or []
        if engine_api is not None:
            manifest["engineApi"] = engine_api
        return manifest

    def feature_set(
        self,
        identity: str,
        version: str,
        *,
        packages: list[dict[str, object]] | None = None,
        feature_sets: list[dict[str, object]] | None = None,
        engine_api: dict[str, object] | None = None,
    ) -> dict[str, object]:
        manifest = copy.deepcopy(self.feature_set_template)
        manifest["id"] = identity
        manifest["version"] = version
        manifest["displayName"] = identity
        manifest["packages"] = packages or []
        manifest["featureSets"] = feature_sets or []
        if engine_api is not None:
            manifest["engineApi"] = engine_api
        return manifest

    def candidate(
        self,
        manifest: dict[str, object],
        *,
        origin: str | None = None,
        source_id: str = "com.asharia.source.synthetic",
    ) -> package_resolver.PackageCandidate:
        stable_origin = origin or f"catalog/{manifest['id']}/{manifest['version']}"
        manifest_bytes = (
            json.dumps(manifest, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
            .encode("utf-8")
        )
        payload_bytes = f"payload:{stable_origin}".encode("utf-8")
        return package_resolver.PackageCandidate(
            identity=str(manifest["id"]),
            version=str(manifest["version"]),
            package_kind=str(manifest["packageKind"]),
            origin=stable_origin,
            source={"kind": "local", "sourceId": source_id},
            manifest_integrity={
                "algorithm": "sha256",
                "digest": hashlib.sha256(manifest_bytes).hexdigest(),
            },
            payload_integrity={
                "algorithm": "sha256",
                "digest": hashlib.sha256(payload_bytes).hexdigest(),
            },
            manifest=manifest,
        )

    def distributed_candidate(
        self,
        manifest: dict[str, object],
        *,
        root: str = "packages/rendering",
    ) -> package_resolver.PackageCandidate:
        candidate = self.candidate(manifest, origin=f"engine-distribution:{root}")
        return package_resolver.PackageCandidate(
            **{**candidate.__dict__, "source": {"kind": "engine-distribution"}}
        )

    def resolve(
        self,
        project: dict[str, object],
        candidates: list[package_resolver.PackageCandidate],
        *,
        engine_api_version: str = ENGINE_API_VERSION,
    ) -> package_resolver.ResolutionResult:
        return package_resolver.resolve_package_graph(
            project,
            package_test_support.make_engine_distribution(
                candidates,
                engine_api_version=engine_api_version,
            ),
            candidates,
            self.validators,
        )

    def test_empty_project_resolves_to_valid_empty_lock(self) -> None:
        project = self.project()

        result = self.resolve(project, [])

        self.assertTrue(result.succeeded)
        self.assertEqual([], result.lock["nodes"])
        self.assertEqual([], check_package_contracts.validate_manifest_data(
            result.lock,
            "asharia.packages.lock.json",
            self.validators,
        ))

    def test_distributed_nodes_reference_inventory_without_copying_evidence(self) -> None:
        identity = "com.asharia.system.distributed-rendering"
        project = self.project(packages=[requirement(identity, exact("1.0.0"))])
        candidate = self.distributed_candidate(
            self.installable(identity, "1.0.0")
        )

        result = self.resolve(project, [candidate])

        self.assertTrue(result.succeeded)
        node = result.lock["nodes"][0]
        self.assertEqual({"kind": "engine-distribution"}, node["source"])
        self.assertNotIn("manifestIntegrity", node)
        self.assertNotIn("payloadIntegrity", node)
        distribution = package_test_support.make_engine_distribution([candidate])
        self.assertEqual(
            distribution["engineGenerationId"],
            result.lock["inputs"]["engine"]["engineGenerationId"],
        )

    def test_project_owned_candidate_cannot_shadow_distribution_inventory(self) -> None:
        identity = "com.asharia.system.distributed-rendering"
        manifest = self.installable(identity, "1.0.0")
        project = self.project(packages=[requirement(identity, exact("1.0.0"))])
        distributed = self.distributed_candidate(copy.deepcopy(manifest))
        shadow = self.candidate(
            copy.deepcopy(manifest),
            source_id="com.asharia.source.shadow",
        )

        result = self.resolve(project, [distributed, shadow])

        self.assertFalse(result.succeeded)
        self.assertIn(
            "resolver.engine.distribution-shadowed",
            {diagnostic.code for diagnostic in result.diagnostics},
        )

    def test_project_engine_requirement_is_checked_before_solving(self) -> None:
        project = self.project()
        wrong_distribution = package_test_support.make_engine_distribution(
            distribution_id="com.asharia.distribution.other-engine"
        )
        incompatible_api = package_test_support.make_engine_distribution(
            engine_api_version="0.2.0"
        )

        wrong = package_resolver.resolve_package_graph(
            project, wrong_distribution, [], self.validators
        )
        incompatible = package_resolver.resolve_package_graph(
            project, incompatible_api, [], self.validators
        )

        self.assertIn(
            "resolver.engine.distribution-mismatch",
            {diagnostic.code for diagnostic in wrong.diagnostics},
        )
        self.assertIn(
            "resolver.engine.api-incompatible",
            {diagnostic.code for diagnostic in incompatible.diagnostics},
        )

    def test_highest_compatible_version_and_candidate_order_are_deterministic(self) -> None:
        identity = "com.asharia.system.rendering"
        project = self.project(
            packages=[requirement(identity, version_range("0.1.0", "1.0.0"))]
        )
        candidates = [
            self.candidate(self.installable(identity, "0.1.0")),
            self.candidate(self.installable(identity, "0.4.0")),
            self.candidate(self.installable(identity, "1.0.0")),
        ]

        first = self.resolve(project, candidates)
        second = self.resolve(copy.deepcopy(project), list(reversed(candidates)))

        self.assertTrue(first.succeeded)
        self.assertTrue(second.succeeded)
        self.assertEqual("0.4.0", first.lock["nodes"][0]["version"])
        self.assertEqual(
            check_package_contracts.render_normalized_lock_manifest(first.lock),
            check_package_contracts.render_normalized_lock_manifest(second.lock),
        )

    def test_backtracks_from_highest_candidate_to_compatible_graph(self) -> None:
        alpha = "com.asharia.system.alpha"
        charlie = "com.asharia.system.charlie"
        delta = "com.asharia.system.delta"
        project = self.project(
            packages=[
                requirement(alpha, version_range("1.0.0", "3.0.0")),
                requirement(charlie, exact("1.0.0")),
            ]
        )
        candidates = [
            self.candidate(
                self.installable(
                    alpha,
                    "2.0.0",
                    dependencies=[requirement(delta, version_range("2.0.0", "3.0.0"))],
                )
            ),
            self.candidate(
                self.installable(
                    alpha,
                    "1.0.0",
                    dependencies=[requirement(delta, version_range("1.0.0", "2.0.0"))],
                )
            ),
            self.candidate(
                self.installable(
                    charlie,
                    "1.0.0",
                    dependencies=[requirement(delta, version_range("1.0.0", "2.0.0"))],
                )
            ),
            self.candidate(self.installable(delta, "2.0.0")),
            self.candidate(self.installable(delta, "1.0.0")),
        ]

        result = self.resolve(project, candidates)
        versions = {node["id"]: node["version"] for node in result.lock["nodes"]}

        self.assertTrue(result.succeeded)
        self.assertEqual("1.0.0", versions[alpha])
        self.assertEqual("1.0.0", versions[delta])

    def test_nested_feature_sets_remain_exact_nodes(self) -> None:
        outer = "com.asharia.features.outer"
        inner = "com.asharia.features.inner"
        package = "com.asharia.system.rendering"
        project = self.project(feature_sets=[requirement(outer, exact("1.0.0"))])
        candidates = [
            self.candidate(
                self.feature_set(
                    outer,
                    "1.0.0",
                    feature_sets=[requirement(inner, exact("1.0.0"))],
                )
            ),
            self.candidate(
                self.feature_set(
                    inner,
                    "1.0.0",
                    packages=[requirement(package, exact("1.0.0"))],
                )
            ),
            self.candidate(self.installable(package, "1.0.0")),
        ]

        result = self.resolve(project, candidates)

        self.assertTrue(result.succeeded)
        self.assertEqual([inner, outer, package], [node["id"] for node in result.lock["nodes"]])
        self.assertEqual("feature-set", result.lock["nodes"][0]["packageKind"])
        self.assertEqual(outer, result.lock["roots"]["directFeatureSets"][0]["id"])

    def test_prerelease_range_policy_is_explicit(self) -> None:
        identity = "com.asharia.system.rendering"
        candidates = [
            self.candidate(self.installable(identity, "0.1.0")),
            self.candidate(self.installable(identity, "0.2.0-alpha.1")),
        ]
        stable_project = self.project(
            packages=[requirement(identity, version_range("0.1.0", "1.0.0"))]
        )
        prerelease_project = self.project(
            packages=[
                requirement(
                    identity,
                    version_range("0.1.0", "1.0.0", allow_prerelease=True),
                )
            ]
        )

        stable = self.resolve(stable_project, candidates)
        prerelease = self.resolve(prerelease_project, candidates)
        exact_prerelease = self.resolve(
            self.project(
                packages=[requirement(identity, exact("0.2.0-alpha.1"))]
            ),
            candidates,
        )

        self.assertEqual("0.1.0", stable.lock["nodes"][0]["version"])
        self.assertEqual("0.2.0-alpha.1", prerelease.lock["nodes"][0]["version"])
        self.assertEqual("0.2.0-alpha.1", exact_prerelease.lock["nodes"][0]["version"])

    def test_conflicting_origins_include_both_requirement_chains(self) -> None:
        feature_set = "com.asharia.features.rendering"
        package = "com.asharia.system.rendering"
        project = self.project(
            packages=[requirement(package, exact("1.0.0"))],
            feature_sets=[requirement(feature_set, exact("1.0.0"))],
        )
        candidates = [
            self.candidate(self.installable(package, "1.0.0")),
            self.candidate(self.installable(package, "2.0.0")),
            self.candidate(
                self.feature_set(
                    feature_set,
                    "1.0.0",
                    packages=[requirement(package, exact("2.0.0"))],
                )
            ),
        ]

        result = self.resolve(project, candidates)

        self.assertFalse(result.succeeded)
        self.assertEqual("resolver.version.unsatisfied", result.diagnostics[0].code)
        self.assertEqual(2, len(result.diagnostics[0].requirement_chains))
        self.assertIn("project.directPackages", result.diagnostics[0].render())
        self.assertIn(f"{feature_set}@1.0.0 requires", result.diagnostics[0].render())
        reordered = self.resolve(copy.deepcopy(project), list(reversed(candidates)))
        self.assertEqual(result.diagnostics[0].render(), reordered.diagnostics[0].render())

    def test_missing_kind_and_engine_failures_are_distinct(self) -> None:
        identity = "com.asharia.system.rendering"
        project = self.project(packages=[requirement(identity, exact("1.0.0"))])
        missing = self.resolve(project, [])
        wrong_kind = self.resolve(
            project,
            [
                self.candidate(
                    self.feature_set(
                        identity,
                        "1.0.0",
                        packages=[
                            requirement("com.asharia.system.member", exact("1.0.0"))
                        ],
                    )
                )
            ],
        )
        engine = self.resolve(
            project,
            [
                self.candidate(
                    self.installable(
                        identity,
                        "1.0.0",
                        engine_api=version_range("0.2.0", "0.3.0"),
                    )
                )
            ],
        )

        self.assertEqual("resolver.candidate.missing", missing.diagnostics[0].code)
        self.assertEqual("resolver.requirement.kind-mismatch", wrong_kind.diagnostics[0].code)
        self.assertEqual("resolver.engine-api.unsatisfied", engine.diagnostics[0].code)

    def test_same_exact_version_from_multiple_sources_is_ambiguous(self) -> None:
        identity = "com.asharia.system.rendering"
        manifest = self.installable(identity, "1.0.0")
        project = self.project(packages=[requirement(identity, exact("1.0.0"))])
        candidates = [
            self.candidate(
                copy.deepcopy(manifest),
                origin="bundled/rendering",
                source_id="com.asharia.source.bundled",
            ),
            self.candidate(
                copy.deepcopy(manifest),
                origin="workspace/rendering",
                source_id="com.asharia.source.workspace",
            ),
        ]

        result = self.resolve(project, list(reversed(candidates)))

        self.assertFalse(result.succeeded)
        self.assertEqual("resolver.candidate.ambiguous", result.diagnostics[0].code)
        self.assertIn("bundled/rendering", result.diagnostics[0].message)
        self.assertIn("workspace/rendering", result.diagnostics[0].message)

    def test_engine_metadata_cannot_hide_an_exact_version_ambiguity(self) -> None:
        identity = "com.asharia.system.rendering"
        project = self.project(packages=[requirement(identity, exact("1.0.0"))])
        compatible = self.installable(identity, "1.0.0")
        incompatible = self.installable(
            identity,
            "1.0.0",
            engine_api=version_range("0.2.0", "0.3.0"),
        )
        candidates = [
            self.candidate(compatible, origin="bundled/rendering"),
            self.candidate(
                incompatible,
                origin="workspace/rendering",
                source_id="com.asharia.source.workspace",
            ),
        ]

        result = self.resolve(project, candidates)

        self.assertEqual("resolver.candidate.ambiguous", result.diagnostics[0].code)

    def test_irrelevant_ambiguity_does_not_poison_resolution(self) -> None:
        required = "com.asharia.system.required"
        unrelated = "com.asharia.system.unrelated"
        unrelated_manifest = self.installable(unrelated, "2.0.0")
        excluded_manifest = self.installable(required, "2.0.0")
        project = self.project(packages=[requirement(required, exact("1.0.0"))])
        candidates = [
            self.candidate(self.installable(required, "1.0.0")),
            self.candidate(copy.deepcopy(unrelated_manifest), origin="bundled/unrelated"),
            self.candidate(
                copy.deepcopy(unrelated_manifest),
                origin="workspace/unrelated",
                source_id="com.asharia.source.workspace",
            ),
            self.candidate(copy.deepcopy(excluded_manifest), origin="bundled/required-v2"),
            self.candidate(
                copy.deepcopy(excluded_manifest),
                origin="workspace/required-v2",
                source_id="com.asharia.source.workspace",
            ),
        ]

        result = self.resolve(project, candidates)

        self.assertTrue(result.succeeded)
        self.assertEqual([required], [node["id"] for node in result.lock["nodes"]])

    def test_invalid_candidate_metadata_and_evidence_fail_before_solving(self) -> None:
        identity = "com.asharia.system.rendering"
        project = self.project(packages=[requirement(identity, exact("1.0.0"))])
        metadata = self.candidate(self.installable(identity, "1.0.0"))
        metadata = package_resolver.PackageCandidate(
            **{**metadata.__dict__, "version": "2.0.0"}
        )
        invalid_source = self.candidate(self.installable(identity, "1.0.0"))
        invalid_source = package_resolver.PackageCandidate(
            **{
                **invalid_source.__dict__,
                "source": {
                    "kind": "local",
                    "sourceId": "com.asharia.source.synthetic",
                    "relativePath": "not-allowed",
                },
            }
        )

        metadata_result = self.resolve(project, [metadata])
        evidence_result = self.resolve(project, [invalid_source])

        self.assertIn(
            "resolver.candidate.metadata-mismatch",
            {item.code for item in metadata_result.diagnostics},
        )
        self.assertIn(
            "resolver.candidate.evidence-invalid",
            {item.code for item in evidence_result.diagnostics},
        )

    def test_cycles_fail_without_returning_partial_lock(self) -> None:
        alpha = "com.asharia.system.alpha"
        beta = "com.asharia.system.beta"
        project = self.project(packages=[requirement(alpha, exact("1.0.0"))])
        candidates = [
            self.candidate(
                self.installable(
                    alpha,
                    "1.0.0",
                    dependencies=[requirement(beta, exact("1.0.0"))],
                )
            ),
            self.candidate(
                self.installable(
                    beta,
                    "1.0.0",
                    dependencies=[requirement(alpha, exact("1.0.0"))],
                )
            ),
        ]

        result = self.resolve(project, candidates)

        self.assertIsNone(result.lock)
        self.assertEqual((), result.selected_candidates)
        self.assertEqual("resolver.dependency.cycle", result.diagnostics[0].code)
        self.assertTrue(result.diagnostics[0].requirement_chains)

    def test_project_options_are_proven_by_cross_document_validation(self) -> None:
        identity = "com.asharia.system.rendering"
        project = self.project(
            packages=[requirement(identity, exact("1.0.0"))],
            options=[
                {
                    "packageId": identity,
                    "values": [{"id": "missing-option", "value": True}],
                }
            ],
        )

        result = self.resolve(
            project,
            [self.candidate(self.installable(identity, "1.0.0"))],
        )

        self.assertIsNone(result.lock)
        self.assertEqual("lock.option.unknown-id", result.diagnostics[0].code)

    def test_resolver_does_not_mutate_project_or_candidates(self) -> None:
        identity = "com.asharia.system.rendering"
        project = self.project(packages=[requirement(identity, exact("1.0.0"))])
        candidate = self.candidate(self.installable(identity, "1.0.0"))
        candidate = package_resolver.PackageCandidate(
            **{**candidate.__dict__, "payload_location": Path("adapter-cache/rendering")}
        )
        candidates = [candidate]
        project_before = copy.deepcopy(project)
        candidates_before = copy.deepcopy(candidates)

        result = self.resolve(project, candidates)

        self.assertTrue(result.succeeded)
        self.assertEqual(project_before, project)
        self.assertEqual(candidates_before, candidates)
        self.assertEqual(
            Path("adapter-cache/rendering"),
            result.selected_candidates[0].payload_location,
        )


if __name__ == "__main__":
    unittest.main()
