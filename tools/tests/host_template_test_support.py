"""Shared verified composition fixture for Host Template tests."""

from __future__ import annotations

from dataclasses import dataclass, replace

from tools import check_package_contracts as contracts
from tools import host_activation_blueprint as activation
from tools import source_build_plan
from tools import static_composition_root
from tools import static_factory_provider_bindings as provider_bindings
from tools.cmake_file_api import CMakeGeneratorEvidence, CMakeToolchainEvidence


@dataclass(frozen=True)
class ProviderFixture:
    package_id: str
    package_version: str
    module_id: str
    target_name: str
    header: str
    function: str
    factory_id: str
    contribution_id: str
    contribution_kind: str


SYNTHETIC_PROVIDER_FIXTURE = ProviderFixture(
    package_id="com.asharia.synthetic",
    package_version="1.0.0",
    module_id="implementation",
    target_name="asharia_synthetic_runtime",
    header="asharia/synthetic/runtime_provider.hpp",
    function="asharia::synthetic::provideRuntimeFactories",
    factory_id="runtime-service",
    contribution_id="com.asharia.contribution.synthetic-runtime",
    contribution_kind="com.asharia.contribution.synthetic-service",
)

PROJECT_BOOTSTRAP_PROVIDER_FIXTURE = ProviderFixture(
    package_id="com.asharia.project-bootstrap",
    package_version="0.1.0",
    module_id="bootstrap",
    target_name="asharia-project-bootstrap-provider",
    header="asharia/project_bootstrap/project_bootstrap_provider.hpp",
    function="asharia::project_bootstrap::provideProjectBootstrapFactories",
    factory_id="project-bootstrap-application",
    contribution_id="com.asharia.contribution.project-bootstrap-application",
    contribution_kind="com.asharia.host.process-application",
)

RESTRICTED_SENTINEL_PROVIDER_FIXTURE = ProviderFixture(
    package_id="com.asharia.restricted-sentinel",
    package_version="1.0.0",
    module_id="verification",
    target_name="asharia_restricted_sentinel",
    header="asharia/sentinel/restricted_sentinel_provider.hpp",
    function="asharia::sentinel::provideRestrictedSentinelFactories",
    factory_id="restricted-sentinel",
    contribution_id="com.asharia.contribution.restricted-sentinel",
    contribution_kind="com.asharia.contribution.restricted-sentinel",
)


def digest(character: str) -> str:
    return character * 64


def build_plan(
    compiler_version: str = "19.1.5",
    compiler_id: str = "Clang",
    provider_fixture: ProviderFixture = SYNTHETIC_PROVIDER_FIXTURE,
    tool_provider_fixture: ProviderFixture | None = None,
) -> source_build_plan.SourceBuildPlan:
    fixtures = (provider_fixture,)
    if tool_provider_fixture is not None:
        fixtures += (tool_provider_fixture,)
    ordered_fixtures = sorted(
        fixtures,
        key=lambda value: value.target_name.encode("utf-8"),
    )
    targets = tuple(
        source_build_plan.BuildTargetReference(value.target_name, "STATIC_LIBRARY")
        for value in ordered_fixtures
    )
    plan = source_build_plan.SourceBuildPlan(
        inputs=source_build_plan.SourceBuildInputs(
            host_composition_integrity=source_build_plan.IntegrityRecord(
                "sha256", digest("1")
            ),
            descriptor_set_integrity=source_build_plan.IntegrityRecord(
                "sha256", digest("2")
            ),
            topology_integrity=source_build_plan.IntegrityRecord(
                "sha256", digest("3")
            ),
            codemodel_integrity=source_build_plan.IntegrityRecord(
                "sha256", digest("4")
            ),
            configuration_integrity=source_build_plan.IntegrityRecord(
                "sha256", digest("5")
            ),
        ),
        host_kind="asset-worker" if tool_provider_fixture else "minimal",
        target_platform="com.asharia.platform.windows-x86-64",
        configuration="Debug",
        generator=CMakeGeneratorEvidence("Ninja", False),
        toolchain=CMakeToolchainEvidence(
            compiler_id, compiler_version, "Windows", "x86_64"
        ),
        packages=(),
        build_roots=targets,
        target_closure=tuple(
            source_build_plan.TargetClosureEvidence(
                target.name, target.target_type, ()
            )
            for target in targets
        ),
        build_options=(),
        integrity=source_build_plan.IntegrityRecord("sha256", digest("0")),
    )
    integrity = source_build_plan.compute_source_build_plan_integrity(plan)
    return replace(
        plan,
        integrity=source_build_plan.IntegrityRecord(
            integrity["algorithm"], integrity["digest"]
        ),
    )


def _factory_activation(
    provider_fixture: ProviderFixture,
) -> activation.FactoryActivation:
    return activation.FactoryActivation(
        reference=activation.ExactFactoryReference(
            provider_fixture.package_id,
            provider_fixture.package_version,
            provider_fixture.module_id,
            provider_fixture.factory_id,
        ),
        requirements=(),
        contributions=(
            activation.FactoryContribution(
                provider_fixture.contribution_id,
                provider_fixture.contribution_kind,
            ),
        ),
    )


def blueprint(
    provider_fixture: ProviderFixture = SYNTHETIC_PROVIDER_FIXTURE,
    tool_provider_fixture: ProviderFixture | None = None,
) -> activation.HostActivationBlueprint:
    scope_templates = [
        activation.ScopeActivationTemplate(
            "process", None, (_factory_activation(provider_fixture),)
        )
    ]
    host_kind = "minimal"
    if tool_provider_fixture is not None:
        host_kind = "asset-worker"
        scope_templates.append(
            activation.ScopeActivationTemplate(
                "tool-job",
                "process",
                (_factory_activation(tool_provider_fixture),),
            )
        )

    value = activation.HostActivationBlueprint(
        inputs=activation.HostActivationBlueprintInputs(
            effective_session_integrity=activation.IntegrityRecord(
                "sha256", digest("6")
            ),
            host_composition_integrity=activation.IntegrityRecord(
                "sha256", digest("1")
            ),
            factory_declaration_set_integrity=activation.IntegrityRecord(
                "sha256", digest("7")
            ),
        ),
        engine_generation_id="sha256-" + digest("a"),
        host_kind=host_kind,
        target_platform="com.asharia.platform.windows-x86-64",
        lifecycle_model=activation.LIFECYCLE_MODEL,
        scope_templates=tuple(scope_templates),
        integrity=activation.IntegrityRecord("sha256", digest("0")),
    )
    integrity = activation.compute_host_activation_blueprint_integrity(value)
    return replace(
        value,
        integrity=activation.IntegrityRecord(
            integrity["algorithm"], integrity["digest"]
        ),
    )


def _binding_provider(
    provider_fixture: ProviderFixture,
) -> provider_bindings.StaticFactoryProvider:
    return provider_bindings.StaticFactoryProvider(
        package_id=provider_fixture.package_id,
        package_version=provider_fixture.package_version,
        module_id=provider_fixture.module_id,
        target=provider_bindings.ProviderTarget(
            provider_fixture.target_name, "STATIC_LIBRARY"
        ),
        entry_point=provider_bindings.ProviderEntryPoint(
            provider_fixture.header,
            provider_fixture.function,
        ),
        factories=(
            provider_bindings.StaticFactoryBinding(
                factory_id=provider_fixture.factory_id,
                contributions=(
                    provider_bindings.StaticFactoryContributionBinding(
                        contribution_id=provider_fixture.contribution_id,
                        contribution_kind=provider_fixture.contribution_kind,
                    ),
                ),
            ),
        ),
    )


def binding_plan(
    plan: source_build_plan.SourceBuildPlan,
    host_blueprint: activation.HostActivationBlueprint,
    provider_fixture: ProviderFixture = SYNTHETIC_PROVIDER_FIXTURE,
    tool_provider_fixture: ProviderFixture | None = None,
) -> provider_bindings.StaticFactoryProviderBindingPlan:
    fixtures = (provider_fixture,)
    if tool_provider_fixture is not None:
        fixtures += (tool_provider_fixture,)
    ordered_fixtures = sorted(
        fixtures,
        key=lambda value: value.package_id.encode("utf-8"),
    )
    providers = tuple(_binding_provider(value) for value in ordered_fixtures)
    value = provider_bindings.StaticFactoryProviderBindingPlan(
        inputs=provider_bindings.StaticFactoryProviderBindingInputs(
            effective_session_integrity=provider_bindings.IntegrityRecord(
                "sha256", digest("6")
            ),
            source_build_plan_integrity=provider_bindings.IntegrityRecord(
                plan.integrity.algorithm, plan.integrity.digest
            ),
            host_activation_blueprint_integrity=provider_bindings.IntegrityRecord(
                host_blueprint.integrity.algorithm, host_blueprint.integrity.digest
            ),
            binding_declaration_set_integrity=provider_bindings.IntegrityRecord(
                "sha256", digest("8")
            ),
        ),
        engine_generation_id=host_blueprint.engine_generation_id,
        host_kind=host_blueprint.host_kind,
        target_platform=host_blueprint.target_platform,
        providers=providers,
        integrity=provider_bindings.IntegrityRecord("sha256", digest("0")),
    )
    integrity = (
        provider_bindings.compute_static_factory_provider_binding_plan_integrity(
            value
        )
    )
    return replace(
        value,
        integrity=provider_bindings.IntegrityRecord(
            integrity["algorithm"], integrity["digest"]
        ),
    )


def composition_generation(
    validators: contracts.ContractValidators,
    compiler_version: str = "19.1.5",
    compiler_id: str = "Clang",
    provider_fixture: ProviderFixture = SYNTHETIC_PROVIDER_FIXTURE,
    tool_provider_fixture: ProviderFixture | None = None,
) -> static_composition_root.StaticCompositionRootGeneration:
    plan = build_plan(
        compiler_version, compiler_id, provider_fixture, tool_provider_fixture
    )
    host_blueprint = blueprint(provider_fixture, tool_provider_fixture)
    result = static_composition_root.generate_static_composition_root(
        plan,
        host_blueprint,
        binding_plan(
            plan, host_blueprint, provider_fixture, tool_provider_fixture
        ),
        validators,
    )
    if not result.succeeded or result.generation is None:
        raise AssertionError([item.render() for item in result.diagnostics])
    return result.generation
