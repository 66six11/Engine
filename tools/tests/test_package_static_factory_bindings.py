"""Static native Factory Provider Binding v1 contract and evidence tests."""

from __future__ import annotations

import copy
import json
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

from tools import check_package_contracts as contracts
from tools import effective_session
from tools import host_activation_blueprint as activation
from tools import host_package_composition as composition
from tools import package_candidate_discovery as discovery
from tools import package_resolver
from tools import source_build_plan
from tools import static_factory_provider_bindings as provider_bindings
from tools.cmake_file_api import CMakeGeneratorEvidence, CMakeToolchainEvidence
from tools.package_lock_verification import verify_locked_package_graph
from tools.tests import package_test_support


FIXTURE_ROOT = Path(__file__).parent / "fixtures/package-contracts"


class PackageStaticFactoryBindingsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.validators = contracts.load_contract_validators()
        cls.manifest = json.loads(
            (FIXTURE_ROOT / "valid-host-package.json").read_text(encoding="utf-8")
        )
        cls.editor_profile = json.loads(
            (FIXTURE_ROOT / "valid-host-profile-editor.json").read_text(
                encoding="utf-8"
            )
        )

    def factory_declaration(self) -> dict[str, object]:
        factories = {
            "implementation": {
                "id": "runtime-service",
                "scope": "project",
                "requires": [],
                "contributions": [
                    "com.asharia.contribution.synthetic-runtime"
                ],
            },
            "editor": {
                "id": "editor-panel",
                "scope": "editor",
                "requires": [
                    {
                        "packageId": self.manifest["id"],
                        "factoryId": "runtime-service",
                    }
                ],
                "contributions": [
                    "com.asharia.contribution.synthetic-editor"
                ],
            },
            "tool": {
                "id": "asset-importer",
                "scope": "tool-job",
                "requires": [],
                "contributions": [
                    "com.asharia.contribution.synthetic-asset"
                ],
            },
        }
        return {
            "schema": "com.asharia.package-factories",
            "schemaVersion": 1,
            "package": {
                "id": self.manifest["id"],
                "version": self.manifest["version"],
            },
            "lifecycleModel": "create-activate-quiesce-deactivate-destroy-v1",
            "modules": [
                {
                    "moduleId": module["id"],
                    "activation": (
                        {
                            "kind": "factory-set",
                            "factories": [factories[module["id"]]],
                        }
                        if module["id"] in factories
                        else {"kind": "no-factories"}
                    ),
                }
                for module in self.manifest["modules"]
            ],
        }

    def build_descriptor(self) -> dict[str, object]:
        target_names = {
            "implementation": "asharia_synthetic_runtime",
            "editor": "asharia_synthetic_editor",
            "tool": "asharia_synthetic_tool",
        }
        return {
            "schema": "com.asharia.package-source-build",
            "schemaVersion": 1,
            "package": {
                "id": self.manifest["id"],
                "version": self.manifest["version"],
            },
            "modules": [
                {
                    "moduleId": module["id"],
                    "sourceBoundaries": [
                        "com.asharia.source.synthetic-hosted"
                    ],
                    "build": (
                        {
                            "kind": "target-roots",
                            "targets": [
                                {
                                    "name": target_names[module["id"]],
                                    "type": "STATIC_LIBRARY",
                                }
                            ],
                        }
                        if module["id"] in target_names
                        else {"kind": "no-build"}
                    ),
                }
                for module in self.manifest["modules"]
            ],
        }

    def bindings(self) -> dict[str, object]:
        providers = {
            "implementation": {
                "target": {
                    "name": "asharia_synthetic_runtime",
                    "type": "STATIC_LIBRARY",
                },
                "entryPoint": {
                    "header": "asharia/synthetic/runtime_provider.hpp",
                    "function": "asharia::synthetic::provideRuntimeFactories",
                },
                "factoryIds": ["runtime-service"],
            },
            "editor": {
                "target": {
                    "name": "asharia_synthetic_editor",
                    "type": "STATIC_LIBRARY",
                },
                "entryPoint": {
                    "header": "asharia/synthetic/editor_provider.hpp",
                    "function": "asharia::synthetic::provideEditorFactories",
                },
                "factoryIds": ["editor-panel"],
            },
            "tool": {
                "target": {
                    "name": "asharia_synthetic_tool",
                    "type": "STATIC_LIBRARY",
                },
                "entryPoint": {
                    "header": "asharia/synthetic/tool_provider.hpp",
                    "function": "asharia::synthetic::provideToolFactories",
                },
                "factoryIds": ["asset-importer"],
            },
        }
        return {
            "schema": "com.asharia.package-static-factory-bindings",
            "schemaVersion": 1,
            "package": {
                "id": self.manifest["id"],
                "version": self.manifest["version"],
            },
            "providerApi": "asharia-static-factory-provider-v1",
            "modules": [
                {
                    "moduleId": module["id"],
                    "binding": (
                        {
                            "kind": "provider-set",
                            "providers": [providers[module["id"]]],
                        }
                        if module["id"] in providers
                        else {"kind": "no-providers"}
                    ),
                }
                for module in self.manifest["modules"]
            ],
        }

    def project(self) -> dict[str, object]:
        return {
            "schema": "com.asharia.project-packages",
            "schemaVersion": 2,
            "engine": package_test_support.engine_requirement(),
            "directPackages": [
                {
                    "id": self.manifest["id"],
                    "version": {
                        "kind": "exact",
                        "version": self.manifest["version"],
                    },
                }
            ],
            "directFeatureSets": [],
            "packageOptions": [],
        }

    def write_package(
        self,
        root: Path,
        *,
        bindings: dict[str, object] | None = None,
        include_bindings: bool = True,
    ) -> None:
        root.mkdir(parents=True)
        values = {
            contracts.PACKAGE_MANIFEST_NAME: self.manifest,
            contracts.PACKAGE_SOURCE_BUILD_NAME: self.build_descriptor(),
            contracts.PACKAGE_FACTORIES_NAME: self.factory_declaration(),
        }
        if include_bindings:
            values[contracts.PACKAGE_STATIC_FACTORY_BINDINGS_NAME] = (
                bindings if bindings is not None else self.bindings()
            )
        for name, value in values.items():
            (root / name).write_text(
                json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
                newline="\n",
            )

    def discover(self, root: Path) -> discovery.CandidateDiscoveryResult:
        return discovery.load_package_candidates(
            [
                discovery.LocalCandidateLocation(
                    source_id=self.manifest["id"],
                    payload_root=root,
                )
            ],
            self.validators,
        )

    def source_plan(
        self,
        host_plan: composition.HostCompositionPlan,
        candidate,
    ) -> source_build_plan.SourceBuildPlan:
        descriptor_modules = {
            module["moduleId"]: module
            for module in candidate.build_descriptor["modules"]
        }
        packages: list[source_build_plan.SourcePackageBuildBinding] = []
        roots: dict[str, source_build_plan.BuildTargetReference] = {}
        for package in host_plan.packages:
            modules: list[source_build_plan.SourceModuleBuildBinding] = []
            for selected_module in package.modules:
                descriptor = descriptor_modules[selected_module.module_id]
                build = descriptor["build"]
                targets = tuple(
                    source_build_plan.BuildTargetReference(
                        target["name"],
                        target["type"],
                    )
                    for target in build.get("targets", [])
                )
                for target in targets:
                    roots[target.name] = target
                modules.append(
                    source_build_plan.SourceModuleBuildBinding(
                        module_id=selected_module.module_id,
                        source_boundaries=tuple(descriptor["sourceBoundaries"]),
                        build_kind=build["kind"],
                        targets=targets,
                    )
                )
            packages.append(
                source_build_plan.SourcePackageBuildBinding(
                    package_id=package.package.package_id,
                    package_version=package.package.package_version,
                    modules=tuple(modules),
                )
            )
        integrity = contracts.compute_bytes_integrity(
            composition.render_host_composition_plan(host_plan).encode("utf-8")
        )
        plan = source_build_plan.SourceBuildPlan(
            inputs=source_build_plan.SourceBuildInputs(
                host_composition_integrity=source_build_plan.IntegrityRecord(
                    integrity["algorithm"], integrity["digest"]
                ),
                descriptor_set_integrity=source_build_plan.IntegrityRecord(
                    "sha256", "0" * 64
                ),
                topology_integrity=source_build_plan.IntegrityRecord(
                    "sha256", "1" * 64
                ),
                codemodel_integrity=source_build_plan.IntegrityRecord(
                    "sha256", "2" * 64
                ),
                configuration_integrity=source_build_plan.IntegrityRecord(
                    "sha256", "3" * 64
                ),
            ),
            host_kind=host_plan.host_kind,
            target_platform=host_plan.target_platform,
            configuration="Debug",
            generator=CMakeGeneratorEvidence("Ninja", False),
            toolchain=CMakeToolchainEvidence(
                "Clang",
                "19.1.5",
                "Windows",
                "x86_64",
            ),
            packages=tuple(packages),
            build_roots=tuple(
                roots[name]
                for name in sorted(
                    roots,
                    key=lambda value: value.encode("utf-8"),
                )
            ),
            target_closure=tuple(
                source_build_plan.TargetClosureEvidence(
                    target.name,
                    target.target_type,
                    (),
                )
                for target in sorted(
                    roots.values(),
                    key=lambda value: value.name.encode("utf-8"),
                )
            ),
            build_options=(),
            integrity=source_build_plan.IntegrityRecord("sha256", "0" * 64),
        )
        computed = source_build_plan.compute_source_build_plan_integrity(plan)
        return replace(
            plan,
            integrity=source_build_plan.IntegrityRecord(
                computed["algorithm"],
                computed["digest"],
            ),
        )

    def selected_handoffs(
        self,
        root: Path,
    ) -> tuple[
        effective_session.EffectiveSessionPlan,
        source_build_plan.SourceBuildPlan,
        activation.HostActivationBlueprint,
    ]:
        discovered = self.discover(root)
        self.assertTrue(
            discovered.succeeded,
            [item.render() for item in discovered.diagnostics],
        )
        candidate = discovered.candidates[0]
        profile_snapshot = package_test_support.make_host_profile_snapshot(
            self.editor_profile
        )
        distribution = package_test_support.make_engine_distribution(
            host_profile_snapshots=[profile_snapshot]
        )
        resolution = package_resolver.resolve_package_graph(
            self.project(),
            distribution,
            discovered.candidates,
            self.validators,
        )
        self.assertTrue(resolution.succeeded)
        session_result = effective_session.plan_effective_session(
            distribution,
            self.project(),
            resolution.lock,
            discovered.candidates,
            profile_snapshot,
            self.validators,
        )
        self.assertTrue(session_result.succeeded)
        assert session_result.plan is not None
        host_result = composition.plan_host_package_composition(
            session_result.plan,
            self.validators,
        )
        self.assertTrue(host_result.succeeded)
        assert host_result.plan is not None
        blueprint_result = activation.plan_host_activation_blueprint(
            session_result.plan,
            host_result.plan,
            self.validators,
        )
        self.assertTrue(blueprint_result.succeeded)
        assert blueprint_result.blueprint is not None
        return (
            session_result.plan,
            self.source_plan(host_result.plan, candidate),
            blueprint_result.blueprint,
        )

    def test_valid_bindings_cover_exact_factories_and_static_targets(self) -> None:
        diagnostics = contracts.validate_package_static_factory_bindings(
            self.bindings(),
            self.manifest,
            self.build_descriptor(),
            self.factory_declaration(),
            self.validators,
        )

        self.assertEqual([], diagnostics)

    def test_schema_rejects_dynamic_library_and_untyped_symbol_shapes(self) -> None:
        shared = self.bindings()
        shared["modules"][2]["binding"]["providers"][0]["target"][
            "type"
        ] = "SHARED_LIBRARY"
        symbol = self.bindings()
        symbol["modules"][2]["binding"]["providers"][0]["entryPoint"][
            "function"
        ] = "provideRuntimeFactories"
        runtime_lookup = self.bindings()
        runtime_lookup["modules"][2]["binding"]["providers"][0][
            "entryPoint"
        ]["library"] = "runtime.dll"

        for value in (shared, symbol, runtime_lookup):
            with self.subTest(value=value):
                diagnostics = contracts.validate_manifest_data(
                    value,
                    contracts.PACKAGE_STATIC_FACTORY_BINDINGS_NAME,
                    self.validators,
                )
                self.assertEqual(
                    {"factory.binding.schema"},
                    {item.code for item in diagnostics},
                )

    def test_cross_contract_validation_rejects_missing_duplicate_and_unowned_bindings(
        self,
    ) -> None:
        unknown = self.bindings()
        unknown["modules"][2]["binding"]["providers"][0]["factoryIds"] = [
            "unknown-service"
        ]
        duplicate = self.bindings()
        duplicate["modules"][2]["binding"]["providers"].append(
            copy.deepcopy(duplicate["modules"][2]["binding"]["providers"][0])
        )
        unowned = self.bindings()
        unowned["modules"][2]["binding"]["providers"][0]["target"][
            "name"
        ] = "asharia_unowned"
        no_provider = self.bindings()
        no_provider["modules"][2]["binding"] = {"kind": "no-providers"}

        expected = (
            {"factory.binding.unknown-factory", "factory.binding.missing-factory"},
            {"factory.binding.duplicate-entry-point", "factory.binding.duplicate-factory"},
            {"factory.binding.unowned-target"},
            {"factory.binding.provider-set-required"},
        )
        for value, codes in zip((unknown, duplicate, unowned, no_provider), expected):
            with self.subTest(codes=codes):
                diagnostics = contracts.validate_package_static_factory_bindings(
                    value,
                    self.manifest,
                    self.build_descriptor(),
                    self.factory_declaration(),
                    self.validators,
                )
                self.assertTrue(codes.issubset({item.code for item in diagnostics}))

    def test_canonical_render_ignores_author_array_order(self) -> None:
        first = self.bindings()
        second = copy.deepcopy(first)
        second["modules"].reverse()

        self.assertEqual(
            contracts.render_normalized_package_static_factory_bindings(first),
            contracts.render_normalized_package_static_factory_bindings(second),
        )

    def test_repository_gate_discovers_and_cross_validates_binding_sidecar(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "package"
            self.write_package(root)
            invalid = self.bindings()
            invalid["package"]["version"] = "0.2.0"
            (root / contracts.PACKAGE_STATIC_FACTORY_BINDINGS_NAME).write_text(
                json.dumps(invalid, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
                newline="\n",
            )

            paths = contracts.discover_contract_manifests(root)
            diagnostics = contracts.validate_manifest_files(paths, self.validators)

            self.assertIn(
                root / contracts.PACKAGE_STATIC_FACTORY_BINDINGS_NAME,
                paths,
            )
            self.assertIn(
                "factory.binding.package-version-mismatch",
                {item.code for item in diagnostics},
            )

    def test_candidate_locked_and_effective_snapshots_preserve_exact_evidence(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "package"
            self.write_package(root)
            discovered = self.discover(root)
            self.assertTrue(
                discovered.succeeded,
                [item.render() for item in discovered.diagnostics],
            )
            candidate = discovered.candidates[0]
            binding_path = root / contracts.PACKAGE_STATIC_FACTORY_BINDINGS_NAME
            self.assertEqual(self.bindings(), candidate.static_factory_bindings)
            self.assertEqual(
                binding_path.read_bytes(), candidate.static_factory_bindings_bytes
            )

            distribution = package_test_support.make_engine_distribution()
            resolution = package_resolver.resolve_package_graph(
                self.project(),
                distribution,
                discovered.candidates,
                self.validators,
            )
            self.assertTrue(resolution.succeeded)
            verified = verify_locked_package_graph(
                self.project(),
                distribution,
                resolution.lock,
                discovered.candidates,
                self.validators,
            )
            self.assertTrue(verified.succeeded)

            profile_snapshot = package_test_support.make_host_profile_snapshot(
                self.editor_profile
            )
            distribution = package_test_support.make_engine_distribution(
                host_profile_snapshots=[profile_snapshot]
            )
            resolution = package_resolver.resolve_package_graph(
                self.project(),
                distribution,
                discovered.candidates,
                self.validators,
            )
            session = effective_session.plan_effective_session(
                distribution,
                self.project(),
                resolution.lock,
                discovered.candidates,
                profile_snapshot,
                self.validators,
            )
            self.assertTrue(session.succeeded)
            assert session.plan is not None
            captured = session.plan.verified_graph.selected_candidates[0]
            self.assertEqual(self.bindings(), captured.static_factory_bindings)

            assert candidate.static_factory_bindings is not None
            candidate.static_factory_bindings["modules"].clear()
            self.assertEqual(self.bindings(), captured.static_factory_bindings)
            assert captured.static_factory_bindings is not None
            captured.static_factory_bindings["modules"].clear()
            diagnostics = effective_session.validate_ready_effective_session(
                session.plan,
                self.validators,
            )
            self.assertIn(
                "session.snapshot.fingerprint-mismatch",
                {item.code for item in diagnostics},
            )

    def test_locked_verification_rejects_incomplete_and_changed_binding_snapshot(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "package"
            self.write_package(root)
            discovered = self.discover(root)
            candidate = discovered.candidates[0]
            distribution = package_test_support.make_engine_distribution()
            resolution = package_resolver.resolve_package_graph(
                self.project(),
                distribution,
                discovered.candidates,
                self.validators,
            )

            incomplete_candidate = replace(
                candidate,
                static_factory_bindings_integrity=None,
            )
            incomplete = verify_locked_package_graph(
                self.project(),
                distribution,
                resolution.lock,
                [incomplete_candidate],
                self.validators,
            )
            self.assertIn(
                "lock.candidate.factory-binding-incomplete",
                {item.code for item in incomplete.diagnostics},
            )

            (root / contracts.PACKAGE_STATIC_FACTORY_BINDINGS_NAME).unlink()
            changed = verify_locked_package_graph(
                self.project(),
                distribution,
                resolution.lock,
                discovered.candidates,
                self.validators,
            )
            self.assertIn(
                "lock.candidate.factory-binding-mismatch",
                {item.code for item in changed.diagnostics},
            )

    def test_binding_handoff_cross_checks_blueprint_and_selected_build_targets(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory) / "package"
            self.write_package(root)
            session, build_plan, blueprint = self.selected_handoffs(root)

            result = provider_bindings.plan_static_factory_provider_bindings(
                session,
                build_plan,
                blueprint,
                self.validators,
            )

            self.assertTrue(
                result.succeeded,
                [item.render() for item in result.diagnostics],
            )
            assert result.plan is not None
            self.assertEqual(
                [],
                provider_bindings.validate_static_factory_provider_binding_plan_data(
                    result.plan,
                    self.validators,
                ),
            )
            selected_factory_count = sum(
                len(scope.factories)
                for scope in blueprint.scope_templates
            )
            self.assertEqual(
                selected_factory_count,
                sum(len(provider.factory_ids) for provider in result.plan.providers),
            )

            changed_packages = []
            target_removed = False
            for package in build_plan.packages:
                modules = []
                for module in package.modules:
                    if module.targets and not target_removed:
                        modules.append(
                            replace(module, build_kind="no-build", targets=())
                        )
                        target_removed = True
                    else:
                        modules.append(module)
                changed_packages.append(replace(package, modules=tuple(modules)))
            unselected_plan = replace(
                build_plan,
                packages=tuple(changed_packages),
                integrity=source_build_plan.IntegrityRecord("sha256", "0" * 64),
            )
            computed = source_build_plan.compute_source_build_plan_integrity(
                unselected_plan
            )
            unselected_plan = replace(
                unselected_plan,
                integrity=source_build_plan.IntegrityRecord(
                    computed["algorithm"], computed["digest"]
                ),
            )

            unselected = provider_bindings.plan_static_factory_provider_bindings(
                session,
                unselected_plan,
                blueprint,
                self.validators,
            )

            self.assertFalse(unselected.succeeded)
            self.assertIn(
                "factory.binding.target-unselected",
                {item.code for item in unselected.diagnostics},
            )

            tampered = (
                provider_bindings.static_factory_provider_binding_plan_to_data(
                    result.plan
                )
            )
            tampered["providers"][0]["target"]["name"] = "asharia_tampered"
            self.assertIn(
                "factory.binding-plan.integrity-mismatch",
                {
                    item.code
                    for item in provider_bindings.validate_static_factory_provider_binding_plan_data(
                        tampered,
                        self.validators,
                    )
                },
            )

    def test_binding_handoff_is_deterministic_and_rejects_missing_sidecar(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            first_root = root / "first"
            second_root = root / "second"
            missing_root = root / "missing"
            self.write_package(first_root)
            reordered = self.bindings()
            reordered["modules"].reverse()
            self.write_package(second_root, bindings=reordered)
            self.write_package(missing_root, include_bindings=False)

            first = self.selected_handoffs(first_root)
            second = self.selected_handoffs(second_root)
            missing = self.selected_handoffs(missing_root)
            first_result = provider_bindings.plan_static_factory_provider_bindings(
                *first,
                self.validators,
            )
            repeated_result = (
                provider_bindings.plan_static_factory_provider_bindings(
                    *first,
                    self.validators,
                )
            )
            second_result = provider_bindings.plan_static_factory_provider_bindings(
                *second,
                self.validators,
            )
            missing_result = provider_bindings.plan_static_factory_provider_bindings(
                *missing,
                self.validators,
            )

            self.assertTrue(first_result.succeeded)
            self.assertTrue(repeated_result.succeeded)
            self.assertTrue(second_result.succeeded)
            assert first_result.plan is not None
            assert repeated_result.plan is not None
            assert second_result.plan is not None
            self.assertEqual(
                provider_bindings.render_static_factory_provider_binding_plan(
                    first_result.plan
                ),
                provider_bindings.render_static_factory_provider_binding_plan(
                    repeated_result.plan
                ),
            )
            self.assertEqual(
                first_result.plan.providers,
                second_result.plan.providers,
            )
            # Semantic providers normalize, while exact-byte provenance still changes.
            self.assertNotEqual(
                first_result.plan.inputs.binding_declaration_set_integrity,
                second_result.plan.inputs.binding_declaration_set_integrity,
            )
            self.assertFalse(missing_result.succeeded)
            self.assertIn(
                "factory.binding.missing",
                {item.code for item in missing_result.diagnostics},
            )


if __name__ == "__main__":
    unittest.main()
