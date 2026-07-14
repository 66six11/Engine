"""Host Activation Blueprint v1 planning and evidence tests."""

from __future__ import annotations

import copy
import json
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest import mock

from tools import check_package_contracts as contracts
from tools import effective_session
from tools import host_activation_blueprint as activation
from tools import host_package_composition as composition
from tools import package_candidate_discovery as discovery
from tools import package_resolver
from tools.package_candidates import PackageCandidate
from tools.tests import package_test_support


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"


def exact(version: str = "0.1.0") -> dict[str, object]:
    return {"kind": "exact", "version": version}


def requirement(identity: str, version: str = "0.1.0") -> dict[str, object]:
    return {"id": identity, "version": exact(version)}


class HostActivationBlueprintTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()
        cls.installable_template = json.loads(
            (FIXTURE_ROOT / "valid-host-package.json").read_text(encoding="utf-8")
        )
        cls.profiles = {
            kind: json.loads(
                (FIXTURE_ROOT / f"valid-host-profile-{kind}.json").read_text(
                    encoding="utf-8"
                )
            )
            for kind in (
                "minimal",
                "editor",
                "runtime",
                "dedicated-server",
                "asset-worker",
            )
        }

    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.candidate_root = Path(self.temporary_directory.name)

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def installable(
        self,
        identity: str,
        *,
        dependencies: list[dict[str, object]] | None = None,
    ) -> dict[str, object]:
        manifest = copy.deepcopy(self.installable_template)
        manifest["id"] = identity
        manifest["displayName"] = identity
        manifest["dependencies"] = dependencies or []
        return manifest

    def declaration(self, manifest: dict[str, object]) -> dict[str, object]:
        identity = str(manifest["id"])
        return {
            "schema": "com.asharia.package-factories",
            "schemaVersion": 1,
            "package": {"id": identity, "version": manifest["version"]},
            "lifecycleModel": activation.LIFECYCLE_MODEL,
            "modules": [
                {
                    "moduleId": "contract",
                    "activation": {"kind": "no-factories"},
                },
                {
                    "moduleId": "runtime",
                    "activation": {"kind": "no-factories"},
                },
                {
                    "moduleId": "implementation",
                    "activation": {
                        "kind": "factory-set",
                        "factories": [
                            {
                                "id": "runtime-service",
                                "scope": "project",
                                "requires": [],
                                "contributions": [
                                    "com.asharia.contribution.synthetic-runtime"
                                ],
                            }
                        ],
                    },
                },
                {
                    "moduleId": "editor",
                    "activation": {
                        "kind": "factory-set",
                        "factories": [
                            {
                                "id": "editor-panel",
                                "scope": "editor",
                                "requires": [
                                    {
                                        "packageId": identity,
                                        "factoryId": "runtime-service",
                                    }
                                ],
                                "contributions": [
                                    "com.asharia.contribution.synthetic-editor"
                                ],
                            }
                        ],
                    },
                },
                {
                    "moduleId": "tool",
                    "activation": {
                        "kind": "factory-set",
                        "factories": [
                            {
                                "id": "asset-importer",
                                "scope": "tool-job",
                                "requires": [],
                                "contributions": [
                                    "com.asharia.contribution.synthetic-asset"
                                ],
                            }
                        ],
                    },
                },
                {
                    "moduleId": "cook",
                    "activation": {"kind": "no-factories"},
                },
                {
                    "moduleId": "diagnostics",
                    "activation": {"kind": "no-factories"},
                },
                {
                    "moduleId": "content",
                    "activation": {"kind": "no-factories"},
                },
            ],
        }

    def candidate(
        self,
        manifest: dict[str, object],
        declaration: dict[str, object] | None,
    ) -> PackageCandidate:
        identity = str(manifest["id"])
        root = self.candidate_root / identity
        root.mkdir(parents=True)
        (root / contracts.PACKAGE_MANIFEST_NAME).write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        if declaration is not None:
            (root / contracts.PACKAGE_FACTORIES_NAME).write_text(
                json.dumps(declaration, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
                newline="\n",
            )
        result = discovery.load_package_candidates(
            [discovery.LocalCandidateLocation(identity, root)],
            self.validators,
        )
        self.assertTrue(
            result.succeeded,
            "\n".join(value.render() for value in result.diagnostics),
        )
        return result.candidates[0]

    def project(self, identities: list[str]) -> dict[str, object]:
        return {
            "schema": "com.asharia.project-packages",
            "schemaVersion": 2,
            "engine": package_test_support.engine_requirement(),
            "directPackages": [requirement(identity) for identity in identities],
            "directFeatureSets": [],
            "packageOptions": [],
        }

    def inputs(
        self,
        project: dict[str, object],
        candidates: list[PackageCandidate],
        *,
        host_kind: str = "editor",
    ) -> tuple[effective_session.EffectiveSessionPlan, composition.HostCompositionPlan]:
        profile_snapshot = package_test_support.make_host_profile_snapshot(
            self.profiles[host_kind]
        )
        distribution = package_test_support.make_engine_distribution(
            host_profile_snapshots=[profile_snapshot]
        )
        resolved = package_resolver.resolve_package_graph(
            project,
            distribution,
            candidates,
            self.validators,
        )
        self.assertTrue(
            resolved.succeeded,
            "\n".join(value.render() for value in resolved.diagnostics),
        )
        session = effective_session.plan_effective_session(
            distribution,
            project,
            resolved.lock,
            candidates,
            profile_snapshot,
            self.validators,
        )
        self.assertTrue(
            session.succeeded,
            "\n".join(value.render() for value in session.diagnostics),
        )
        assert session.plan is not None
        host_plan = composition.plan_host_package_composition(
            session.plan,
            self.validators,
        )
        self.assertTrue(
            host_plan.succeeded,
            "\n".join(value.render() for value in host_plan.diagnostics),
        )
        assert host_plan.plan is not None
        return session.plan, host_plan.plan

    def plan(
        self,
        session: effective_session.EffectiveSessionPlan,
        host_plan: composition.HostCompositionPlan,
    ) -> activation.HostActivationBlueprintResult:
        return activation.plan_host_activation_blueprint(
            session,
            host_plan,
            self.validators,
        )

    def test_editor_blueprint_groups_exact_factories_by_scope_template(self) -> None:
        identity = "com.asharia.system.hosted"
        manifest = self.installable(identity)
        candidate = self.candidate(manifest, self.declaration(manifest))
        session, host_plan = self.inputs(self.project([identity]), [candidate])

        result = self.plan(session, host_plan)

        self.assertTrue(
            result.succeeded,
            "\n".join(value.render() for value in result.diagnostics),
        )
        assert result.blueprint is not None
        data = activation.host_activation_blueprint_to_data(result.blueprint)
        self.assertEqual([], activation.validate_host_activation_blueprint_data(
            data,
            self.validators,
        ))
        self.assertEqual(
            list(activation.HOST_SCOPE_POLICIES["editor"]),
            [value["scope"] for value in data["scopeTemplates"]],
        )
        templates = {value["scope"]: value for value in data["scopeTemplates"]}
        self.assertEqual(
            ["runtime-service"],
            [value["reference"]["factoryId"] for value in templates["project"]["factories"]],
        )
        self.assertEqual(
            ["editor-panel"],
            [value["reference"]["factoryId"] for value in templates["editor"]["factories"]],
        )
        editor_factory = templates["editor"]["factories"][0]
        self.assertEqual("runtime-service", editor_factory["requires"][0]["factoryId"])
        self.assertEqual(
            ["com.asharia.contribution.synthetic-editor"],
            [value["id"] for value in editor_factory["contributions"]],
        )
        rendered = activation.render_host_activation_blueprint(result.blueprint)
        self.assertTrue(rendered.endswith("\n"))
        self.assertNotIn("\r", rendered)
        self.assertNotIn("artifact", rendered.lower())
        self.assertNotIn("cmake", rendered.lower())

    def test_every_host_kind_uses_its_fixed_scope_policy(self) -> None:
        expected_policies = {
            "minimal": ("process",),
            "editor": (
                "process",
                "project",
                "editor",
                "editor-document",
                "preview",
                "game-session",
                "world",
                "local-user",
                "tool-job",
            ),
            "runtime": (
                "process",
                "project",
                "game-session",
                "world",
                "local-user",
            ),
            "dedicated-server": (
                "process",
                "project",
                "game-session",
                "world",
            ),
            "asset-worker": ("process", "tool-job"),
        }
        for host_kind, expected_scopes in expected_policies.items():
            with self.subTest(host_kind=host_kind):
                identity = f"com.asharia.system.host-{host_kind}"
                manifest = self.installable(identity)
                candidate = self.candidate(manifest, self.declaration(manifest))
                session, host_plan = self.inputs(
                    self.project([identity]),
                    [candidate],
                    host_kind=host_kind,
                )

                result = self.plan(session, host_plan)

                self.assertTrue(
                    result.succeeded,
                    "\n".join(value.render() for value in result.diagnostics),
                )
                assert result.blueprint is not None
                self.assertEqual(
                    expected_scopes,
                    tuple(value.scope for value in result.blueprint.scope_templates),
                )

    def test_missing_factory_declaration_fails_closed(self) -> None:
        identity = "com.asharia.system.missing-declaration"
        manifest = self.installable(identity)
        session, host_plan = self.inputs(
            self.project([identity]),
            [self.candidate(manifest, None)],
        )

        result = self.plan(session, host_plan)

        self.assertIsNone(result.blueprint)
        self.assertEqual(
            {"activation.declaration.missing"},
            {value.code for value in result.diagnostics},
        )

    def test_external_requirement_must_target_a_selected_factory(self) -> None:
        dependency_id = "com.asharia.system.dependency"
        root_id = "com.asharia.system.root"
        dependency = self.installable(dependency_id)
        root = self.installable(root_id, dependencies=[requirement(dependency_id)])
        dependency_declaration = self.declaration(dependency)
        root_declaration = self.declaration(root)
        editor_factory = root_declaration["modules"][3]["activation"]["factories"][0]
        editor_factory["requires"].append(
            {"packageId": dependency_id, "factoryId": "asset-importer"}
        )
        candidates = [
            self.candidate(root, root_declaration),
            self.candidate(dependency, dependency_declaration),
        ]
        session, host_plan = self.inputs(self.project([root_id]), candidates)

        result = self.plan(session, host_plan)

        self.assertIsNone(result.blueprint)
        self.assertIn(
            "activation.factory.missing",
            {value.code for value in result.diagnostics},
        )

    def test_external_requirement_must_obey_scope_direction(self) -> None:
        dependency_id = "com.asharia.system.dependency"
        root_id = "com.asharia.system.root"
        dependency = self.installable(dependency_id)
        root = self.installable(root_id, dependencies=[requirement(dependency_id)])
        root_declaration = self.declaration(root)
        runtime_factory = root_declaration["modules"][2]["activation"]["factories"][0]
        runtime_factory["requires"] = [
            {"packageId": dependency_id, "factoryId": "editor-panel"}
        ]
        candidates = [
            self.candidate(root, root_declaration),
            self.candidate(dependency, self.declaration(dependency)),
        ]
        session, host_plan = self.inputs(self.project([root_id]), candidates)

        result = self.plan(session, host_plan)

        self.assertIsNone(result.blueprint)
        self.assertIn(
            "activation.factory.scope-direction",
            {value.code for value in result.diagnostics},
        )

    def test_host_scope_policy_rejects_incompatible_factory(self) -> None:
        identity = "com.asharia.system.runtime-invalid-scope"
        manifest = self.installable(identity)
        declaration = self.declaration(manifest)
        declaration["modules"][2]["activation"]["factories"][0]["scope"] = "editor"
        candidate = self.candidate(manifest, declaration)
        session, host_plan = self.inputs(
            self.project([identity]),
            [candidate],
            host_kind="runtime",
        )

        result = self.plan(session, host_plan)

        self.assertIsNone(result.blueprint)
        self.assertIn(
            "activation.factory.scope-policy",
            {value.code for value in result.diagnostics},
        )

    def test_host_filtered_contributions_do_not_enter_runtime_blueprint(self) -> None:
        identity = "com.asharia.system.runtime-filter"
        manifest = self.installable(identity)
        candidate = self.candidate(manifest, self.declaration(manifest))
        session, host_plan = self.inputs(
            self.project([identity]),
            [candidate],
            host_kind="runtime",
        )

        result = self.plan(session, host_plan)

        self.assertTrue(result.succeeded)
        assert result.blueprint is not None
        contributions = [
            contribution.contribution_id
            for template in result.blueprint.scope_templates
            for factory in template.factories
            for contribution in factory.contributions
        ]
        self.assertEqual(
            ["com.asharia.contribution.synthetic-runtime"],
            contributions,
        )

    def test_same_scope_cross_package_dependency_is_stably_ordered(self) -> None:
        dependency_id = "com.asharia.system.z-dependency"
        root_id = "com.asharia.system.a-root"
        dependency = self.installable(dependency_id)
        root = self.installable(root_id, dependencies=[requirement(dependency_id)])
        root_declaration = self.declaration(root)
        editor_factory = root_declaration["modules"][3]["activation"]["factories"][0]
        editor_factory["requires"].append(
            {"packageId": dependency_id, "factoryId": "editor-panel"}
        )
        candidates = [
            self.candidate(root, root_declaration),
            self.candidate(dependency, self.declaration(dependency)),
        ]
        session, host_plan = self.inputs(self.project([root_id]), candidates)

        first = self.plan(session, host_plan)
        second = self.plan(session, host_plan)
        permuted_session, permuted_host_plan = self.inputs(
            self.project([root_id]),
            list(reversed(candidates)),
        )
        permuted = self.plan(permuted_session, permuted_host_plan)

        self.assertTrue(first.succeeded)
        self.assertTrue(second.succeeded)
        self.assertTrue(permuted.succeeded)
        assert first.blueprint is not None
        assert second.blueprint is not None
        assert permuted.blueprint is not None
        editor_template = next(
            value for value in first.blueprint.scope_templates if value.scope == "editor"
        )
        self.assertEqual(
            [dependency_id, root_id],
            [value.reference.package_id for value in editor_template.factories],
        )
        self.assertEqual(
            activation.render_host_activation_blueprint(first.blueprint),
            activation.render_host_activation_blueprint(second.blueprint),
        )
        self.assertEqual(
            activation.render_host_activation_blueprint(first.blueprint),
            activation.render_host_activation_blueprint(permuted.blueprint),
        )

    def test_full_graph_cycle_diagnostic_is_stable(self) -> None:
        first = activation.ExactFactoryReference(
            "com.asharia.system.first", "0.1.0", "implementation", "service"
        )
        second = activation.ExactFactoryReference(
            "com.asharia.system.second", "0.1.0", "implementation", "service"
        )
        drafts = {
            (first.package_id, first.factory_id): activation._FactoryDraft(
                first,
                "project",
                ((second.package_id, second.factory_id),),
                (),
            ),
            (second.package_id, second.factory_id): activation._FactoryDraft(
                second,
                "project",
                ((first.package_id, first.factory_id),),
                (),
            ),
        }
        requirements = {
            (first.package_id, first.factory_id): (second,),
            (second.package_id, second.factory_id): (first,),
        }

        ordered, diagnostics = activation._dependency_first_order(
            drafts,
            requirements,
        )

        self.assertIsNone(ordered)
        self.assertEqual(["activation.factory.cycle"], [value.code for value in diagnostics])
        self.assertIn("first", diagnostics[0].message)
        self.assertIn("second", diagnostics[0].message)

    def test_stale_or_mutated_evidence_fails_atomically_without_input_changes(self) -> None:
        identity = "com.asharia.system.immutable"
        manifest = self.installable(identity)
        candidate = self.candidate(manifest, self.declaration(manifest))
        session, host_plan = self.inputs(self.project([identity]), [candidate])
        session_before = effective_session.render_effective_session_plan(session)
        host_before = composition.render_host_composition_plan(host_plan)

        forged_module = replace(
            host_plan.packages[0].modules[0],
            required_capabilities=("com.asharia.capability.forged",),
        )
        forged_package = replace(
            host_plan.packages[0],
            modules=(forged_module, *host_plan.packages[0].modules[1:]),
        )
        forged_host = replace(
            host_plan,
            packages=(forged_package, *host_plan.packages[1:]),
        )
        stale = self.plan(session, forged_host)

        self.assertIsNone(stale.blueprint)
        self.assertIn(
            "activation.input.host-composition-stale",
            {value.code for value in stale.diagnostics},
        )
        self.assertEqual(session_before, effective_session.render_effective_session_plan(session))
        self.assertEqual(host_before, composition.render_host_composition_plan(host_plan))

        mutated_session = copy.deepcopy(session)
        selected = mutated_session.verified_graph.selected_candidates[0]
        assert selected.factory_declaration_bytes is not None
        mutated_candidate = replace(
            selected,
            factory_declaration_bytes=selected.factory_declaration_bytes + b" ",
        )
        mutated_graph = replace(
            mutated_session.verified_graph,
            selected_candidates=(
                mutated_candidate,
                *mutated_session.verified_graph.selected_candidates[1:],
            ),
        )
        mutated_session = replace(mutated_session, verified_graph=mutated_graph)
        corrupted = self.plan(mutated_session, host_plan)
        self.assertIsNone(corrupted.blueprint)
        self.assertTrue(
            any(value.code.startswith("session.") for value in corrupted.diagnostics)
        )

    def test_planner_does_not_resolve_or_touch_filesystem(self) -> None:
        identity = "com.asharia.system.pure-planner"
        manifest = self.installable(identity)
        candidate = self.candidate(manifest, self.declaration(manifest))
        session, host_plan = self.inputs(self.project([identity]), [candidate])

        with (
            mock.patch.object(
                package_resolver,
                "resolve_package_graph",
                side_effect=AssertionError("planner must not resolve"),
            ),
            mock.patch.object(
                Path,
                "read_bytes",
                side_effect=AssertionError("planner must not read files"),
            ),
            mock.patch.object(
                Path,
                "read_text",
                side_effect=AssertionError("planner must not read files"),
            ),
        ):
            result = self.plan(session, host_plan)

        self.assertTrue(
            result.succeeded,
            "\n".join(value.render() for value in result.diagnostics),
        )

    def test_closed_schema_and_self_integrity_reject_tampering(self) -> None:
        identity = "com.asharia.system.tamper"
        manifest = self.installable(identity)
        candidate = self.candidate(manifest, self.declaration(manifest))
        session, host_plan = self.inputs(self.project([identity]), [candidate])
        result = self.plan(session, host_plan)
        self.assertTrue(result.succeeded)
        assert result.blueprint is not None

        extra = activation.host_activation_blueprint_to_data(result.blueprint)
        extra["artifactPath"] = "bin/forbidden.dll"
        extra_diagnostics = activation.validate_host_activation_blueprint_data(
            extra,
            self.validators,
        )
        self.assertIn(
            "activation.blueprint.schema",
            {value.code for value in extra_diagnostics},
        )

        tampered = activation.host_activation_blueprint_to_data(result.blueprint)
        tampered["host"]["targetPlatform"] = "com.asharia.platform.linux"
        tampered_diagnostics = activation.validate_host_activation_blueprint_data(
            tampered,
            self.validators,
        )
        self.assertIn(
            "activation.blueprint.integrity-mismatch",
            {value.code for value in tampered_diagnostics},
        )


if __name__ == "__main__":
    unittest.main()
