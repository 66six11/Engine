using System;
using System.Linq;
using Asharia.Editor.Contributions;
using Asharia.Editor.Extensions;
using Asharia.Editor.Panels;
using Asharia.Studio.Application.Extensions;
using Xunit;

namespace Asharia.Studio.Application.Tests.Extensions;

public sealed class EditorScopeTransactionTests
{
    [Fact]
    public void Prepare_keeps_candidate_invisible_until_atomic_commit()
    {
        var definition = CreateDefinition("studio.scene", EditorModuleScopeKind.Project);
        var registry = new EditorModuleRegistry();
        var scope = ScopeInstanceId.ForProject(Guid.Parse("11111111-1111-1111-1111-111111111111"));

        var transaction = EditorScopeTransaction.Prepare(registry, scope, [definition]);

        Assert.False(registry.TryGetPartition(scope, out _));
        Assert.Single(transaction.Candidate.Instances);

        transaction.Commit();

        var partition = registry.GetRequiredPartition(scope);
        Assert.Same(transaction.Candidate, partition);
        Assert.Equal(
            EditorModuleInstanceId.Create(definition.Id, scope),
            partition.Instances[definition.Id].Id);
    }

    [Fact]
    public void Prepare_rejects_duplicate_panel_contributions_without_visible_mutation()
    {
        var first = CreateDefinition(
            "studio.first",
            EditorModuleScopeKind.Application,
            editor => editor.Panels.Add(CreatePanel("studio.shared", "factory.first")));
        var second = CreateDefinition(
            "studio.second",
            EditorModuleScopeKind.Application,
            editor => editor.Panels.Add(CreatePanel("studio.shared", "factory.second")));
        var registry = new EditorModuleRegistry();

        var error = Assert.Throws<EditorScopeValidationException>(
            () => EditorScopeTransaction.Prepare(
                registry,
                ScopeInstanceId.Application,
                [first, second]));

        Assert.Contains(
            error.Diagnostics,
            diagnostic => diagnostic.Contains("studio.shared", StringComparison.Ordinal));
        Assert.False(registry.TryGetPartition(ScopeInstanceId.Application, out _));
    }

    [Fact]
    public void Prepare_rejects_missing_required_module_and_required_cycles()
    {
        var missingId = CreateDefinitionId("studio.missing", EditorModuleScopeKind.Project);
        var missingDependency = CreateDefinition(
            "studio.consumer",
            EditorModuleScopeKind.Project,
            editor => editor.Dependencies.RequireModule(missingId));
        var registry = new EditorModuleRegistry();
        var scope = ScopeInstanceId.ForProject(Guid.Parse("22222222-2222-2222-2222-222222222222"));

        var missingError = Assert.Throws<EditorScopeValidationException>(
            () => EditorScopeTransaction.Prepare(registry, scope, [missingDependency]));
        Assert.Contains(
            missingError.Diagnostics,
            diagnostic => diagnostic.Contains("not available", StringComparison.Ordinal));

        var firstId = CreateDefinitionId("studio.first", EditorModuleScopeKind.Project);
        var secondId = CreateDefinitionId("studio.second", EditorModuleScopeKind.Project);
        var first = CreateDefinition(
            firstId,
            editor => editor.Dependencies.RequireModule(secondId));
        var second = CreateDefinition(
            secondId,
            editor => editor.Dependencies.RequireModule(firstId));

        var cycleError = Assert.Throws<EditorScopeValidationException>(
            () => EditorScopeTransaction.Prepare(registry, scope, [first, second]));
        Assert.Contains(
            cycleError.Diagnostics,
            diagnostic => diagnostic.Contains("cycle", StringComparison.OrdinalIgnoreCase));
        Assert.False(registry.TryGetPartition(scope, out _));
    }

    [Fact]
    public void Prepare_rejects_required_capability_cycles_before_commit()
    {
        var firstCapability = EditorCapabilityId.Create("asharia.project.first.v1");
        var secondCapability = EditorCapabilityId.Create("asharia.project.second.v1");
        var first = CreateDefinition(
            "studio.first",
            EditorModuleScopeKind.Project,
            editor =>
            {
                editor.Capabilities.Provide(firstCapability);
                editor.Dependencies.RequireCapability(secondCapability);
            });
        var second = CreateDefinition(
            "studio.second",
            EditorModuleScopeKind.Project,
            editor =>
            {
                editor.Capabilities.Provide(secondCapability);
                editor.Dependencies.RequireCapability(firstCapability);
            });
        var registry = new EditorModuleRegistry();
        var scope = ScopeInstanceId.ForProject(
            Guid.Parse("66666666-6666-6666-6666-666666666666"));

        var error = Assert.Throws<EditorScopeValidationException>(
            () => EditorScopeTransaction.Prepare(registry, scope, [first, second]));

        Assert.Contains(
            error.Diagnostics,
            diagnostic => diagnostic.Contains("cycle", StringComparison.OrdinalIgnoreCase));
        Assert.False(registry.TryGetPartition(scope, out _));
    }

    [Fact]
    public void Project_partition_can_require_a_visible_application_definition()
    {
        var applicationDefinition = CreateDefinition(
            "studio.shell",
            EditorModuleScopeKind.Application);
        var projectDefinition = CreateDefinition(
            "studio.scene",
            EditorModuleScopeKind.Project,
            editor => editor.Dependencies.RequireModule(applicationDefinition.Id));
        var registry = new EditorModuleRegistry();
        EditorScopeTransaction.Prepare(
            registry,
            ScopeInstanceId.Application,
            [applicationDefinition]).Commit();
        var projectScope = ScopeInstanceId.ForProject(
            Guid.Parse("33333333-3333-3333-3333-333333333333"));

        EditorScopeTransaction.Prepare(
            registry,
            projectScope,
            [projectDefinition]).Commit();

        Assert.Single(registry.GetRequiredPartition(projectScope).Instances);
    }

    [Fact]
    public void Prepare_rejects_ambiguous_capability_provider()
    {
        var capability = EditorCapabilityId.Create("asharia.project.scene-tools.v1");
        var first = CreateDefinition(
            "studio.first",
            EditorModuleScopeKind.Project,
            editor => editor.Capabilities.Provide(capability));
        var second = CreateDefinition(
            "studio.second",
            EditorModuleScopeKind.Project,
            editor => editor.Capabilities.Provide(capability));
        var registry = new EditorModuleRegistry();
        var scope = ScopeInstanceId.ForProject(Guid.Parse("44444444-4444-4444-4444-444444444444"));

        var error = Assert.Throws<EditorScopeValidationException>(
            () => EditorScopeTransaction.Prepare(registry, scope, [first, second]));

        Assert.Contains(
            error.Diagnostics,
            diagnostic => diagnostic.Contains("ambiguous", StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public void Commit_rejects_a_stale_transaction_without_overwriting_newer_state()
    {
        var first = CreateDefinition("studio.first", EditorModuleScopeKind.Application);
        var second = CreateDefinition("studio.second", EditorModuleScopeKind.Application);
        var registry = new EditorModuleRegistry();
        var firstTransaction = EditorScopeTransaction.Prepare(
            registry,
            ScopeInstanceId.Application,
            [first]);
        var staleTransaction = EditorScopeTransaction.Prepare(
            registry,
            ScopeInstanceId.Application,
            [second]);

        firstTransaction.Commit();
        var error = Assert.Throws<InvalidOperationException>(staleTransaction.Commit);

        Assert.Contains("changed", error.Message, StringComparison.Ordinal);
        var partition = registry.GetRequiredPartition(ScopeInstanceId.Application);
        Assert.True(partition.Instances.ContainsKey(first.Id));
        Assert.False(partition.Instances.ContainsKey(second.Id));
    }

    [Fact]
    public void Initial_commit_registration_retires_only_its_exact_partition()
    {
        var applicationDefinition = CreateDefinition(
            "studio.shell",
            EditorModuleScopeKind.Application);
        var firstProjectDefinition = CreateDefinition(
            "studio.first",
            EditorModuleScopeKind.Project);
        var secondProjectDefinition = CreateDefinition(
            "studio.second",
            EditorModuleScopeKind.Project);
        var registry = new EditorModuleRegistry();
        EditorScopeTransaction.Prepare(
            registry,
            ScopeInstanceId.Application,
            [applicationDefinition]).Commit();
        var firstScope = ScopeInstanceId.ForProject(
            Guid.Parse("55555555-5555-5555-5555-555555555555"));
        var secondScope = ScopeInstanceId.ForProject(
            Guid.Parse("66666666-6666-6666-6666-666666666666"));
        var transaction = EditorScopeTransaction.Prepare(
            registry,
            firstScope,
            [firstProjectDefinition]);

        Assert.True(transaction.TryCommitInitial(out var registration));
        Assert.NotNull(registration);
        Assert.Same(
            transaction.Candidate,
            registry.GetRequiredPartition(firstScope));
        EditorScopeTransaction.Prepare(
            registry,
            secondScope,
            [secondProjectDefinition]).Commit();

        registration.Dispose();
        registration.Dispose();

        Assert.False(registry.TryGetPartition(firstScope, out _));
        Assert.Same(
            applicationDefinition,
            registry.GetRequiredPartition(ScopeInstanceId.Application)
                .RegistrationOrder.Single().Definition);
        Assert.Same(
            secondProjectDefinition,
            registry.GetRequiredPartition(secondScope)
                .RegistrationOrder.Single().Definition);
    }

    [Fact]
    public void Initial_commit_rejects_existing_scope_and_stale_snapshot()
    {
        var first = CreateDefinition(
            "studio.first",
            EditorModuleScopeKind.Project);
        var second = CreateDefinition(
            "studio.second",
            EditorModuleScopeKind.Project);
        var registry = new EditorModuleRegistry();
        var scope = ScopeInstanceId.ForProject(
            Guid.Parse("77777777-7777-7777-7777-777777777777"));
        EditorScopeTransaction.Prepare(
            registry,
            scope,
            [first]).Commit();
        var existingScopeTransaction = EditorScopeTransaction.Prepare(
            registry,
            scope,
            [second]);

        Assert.False(
            existingScopeTransaction.TryCommitInitial(out var existingRegistration));
        Assert.Null(existingRegistration);
        Assert.Same(
            first,
            registry.GetRequiredPartition(scope)
                .RegistrationOrder.Single().Definition);

        var staleRegistry = new EditorModuleRegistry();
        var staleScope = ScopeInstanceId.ForProject(
            Guid.Parse("88888888-8888-8888-8888-888888888888"));
        var staleTransaction = EditorScopeTransaction.Prepare(
            staleRegistry,
            staleScope,
            [first]);
        EditorScopeTransaction.Prepare(
            staleRegistry,
            ScopeInstanceId.Application,
            []).Commit();

        Assert.False(
            staleTransaction.TryCommitInitial(out var staleRegistration));
        Assert.Null(staleRegistration);
        Assert.False(staleRegistry.TryGetPartition(staleScope, out _));
    }

    [Fact]
    public void Registration_retirement_does_not_remove_a_replacement()
    {
        var first = CreateDefinition(
            "studio.first",
            EditorModuleScopeKind.Project);
        var second = CreateDefinition(
            "studio.second",
            EditorModuleScopeKind.Project);
        var registry = new EditorModuleRegistry();
        var scope = ScopeInstanceId.ForProject(
            Guid.Parse("99999999-9999-9999-9999-999999999999"));
        var firstTransaction = EditorScopeTransaction.Prepare(
            registry,
            scope,
            [first]);
        Assert.True(firstTransaction.TryCommitInitial(out var registration));
        var replacementTransaction = EditorScopeTransaction.Prepare(
            registry,
            scope,
            [second]);
        replacementTransaction.Commit();

        var error = Assert.Throws<InvalidOperationException>(
            registration!.Dispose);

        Assert.Contains("no longer owns", error.Message, StringComparison.Ordinal);
        Assert.Same(
            replacementTransaction.Candidate,
            registry.GetRequiredPartition(scope));
    }

    private static EditorModuleDefinition CreateDefinition(
        string module,
        EditorModuleScopeKind scope,
        Action<EditorModuleBuilder>? configure = null)
    {
        return CreateDefinition(CreateDefinitionId(module, scope), configure);
    }

    private static EditorModuleDefinition CreateDefinition(
        EditorModuleDefinitionId definitionId,
        Action<EditorModuleBuilder>? configure = null)
    {
        var metadata = new EditorModuleMetadata(
            definitionId,
            "Tests.DelegateModule",
            EditorModuleActivationPolicy.OnScopeReady,
            EditorModuleHandoverPolicy.Coexist);
        var registration = new StaticEditorModuleRegistration(
            definitionId,
            () => new DelegateModule(configure),
            metadata);
        return StaticPackageGenerationHost.Create([registration])
            .GetRequiredDefinition(definitionId);
    }

    private static EditorModuleDefinitionId CreateDefinitionId(
        string module,
        EditorModuleScopeKind scope)
    {
        return EditorModuleDefinitionId.Create(
            EditorAssemblyId.Create(
                PackageName.Create("asharia.studio"),
                EditorAssemblyName.Create("Asharia.Studio.BuiltIns")),
            ModuleLocalId.Create(module),
            scope);
    }

    private static EditorPanelDescriptor CreatePanel(string id, string factory)
    {
        return new EditorPanelDescriptor(
            EditorContributionId.Create(id),
            id,
            EditorPanelKind.Tool,
            EditorDockPreference.Bottom,
            EditorPanelCachePolicy.KeepAlive,
            UiBackendId.CodeFirst,
            EditorFactoryLocalId.Create(factory));
    }

    private sealed class DelegateModule(Action<EditorModuleBuilder>? configure) : EditorModule
    {
        public override void Configure(EditorModuleBuilder editor)
        {
            configure?.Invoke(editor);
        }
    }
}
