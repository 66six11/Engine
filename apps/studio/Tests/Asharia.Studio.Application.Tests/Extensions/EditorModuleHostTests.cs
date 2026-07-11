using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Editor.Extensions;
using Asharia.Studio.Application.Extensions;
using Xunit;

namespace Asharia.Studio.Application.Tests.Extensions;

public sealed class EditorModuleHostTests
{
    [Fact]
    public async Task Same_definition_activates_once_per_overlapping_project_scope()
    {
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var contexts = new List<EditorModuleContext>();
        var module = new TestModule(
            activate: async context =>
            {
                lock (contexts)
                {
                    contexts.Add(context);
                    if (contexts.Count == 2)
                    {
                        entered.SetResult();
                    }
                }

                await release.Task;
                return new TestActivation();
            });
        var definition = CreateDefinition("studio.scene", module);
        var firstPartition = CreateProjectPartition(
            definition,
            "11111111-1111-1111-1111-111111111111");
        var secondPartition = CreateProjectPartition(
            definition,
            "22222222-2222-2222-2222-222222222222");
        await using var host = new EditorModuleHost();

        var firstTask = host.ActivateScopeAsync(firstPartition, []).AsTask();
        var duplicateTask = host.ActivateScopeAsync(firstPartition, []).AsTask();
        var secondTask = host.ActivateScopeAsync(secondPartition, []).AsTask();
        await entered.Task.WaitAsync(TimeSpan.FromSeconds(5));
        release.SetResult();

        var first = await firstTask;
        var duplicate = await duplicateTask;
        var second = await secondTask;

        Assert.Same(first, duplicate);
        Assert.Equal(2, contexts.Count);
        Assert.NotEqual(contexts[0].InstanceId, contexts[1].InstanceId);
        Assert.Equal(EditorModuleInstanceState.Active, first.Instances[definition.Id].State);
        Assert.Equal(EditorModuleInstanceState.Active, second.Instances[definition.Id].State);
    }

    [Fact]
    public async Task Unavailable_required_host_capability_waits_without_failing_scope()
    {
        var capability = EditorCapabilityId.Create("asharia.engine.renderer.v1");
        var activationCalls = 0;
        var module = new TestModule(
            configure: editor => editor.Dependencies.RequireCapability(capability),
            activate: _ =>
            {
                activationCalls++;
                return ValueTask.FromResult<IEditorModuleActivation>(new TestActivation());
            });
        var definition = CreateDefinition("studio.scene", module);
        var registry = new EditorModuleRegistry();
        var scope = ProjectScope("33333333-3333-3333-3333-333333333333");
        var transaction = EditorScopeTransaction.Prepare(
            registry,
            scope,
            [definition],
            [capability]);
        transaction.Commit();
        await using var host = new EditorModuleHost();

        var activation = await host.ActivateScopeAsync(
            transaction.Candidate,
            [EditorCapabilitySnapshot.Create(capability, 7, EditorCapabilityState.Unavailable)]);

        Assert.Equal(
            EditorModuleInstanceState.WaitingForCapability,
            activation.Instances[definition.Id].State);
        Assert.Equal(0, activationCalls);
    }

    [Fact]
    public async Task Activation_failure_faults_only_required_dependent_chain()
    {
        var failingId = DefinitionId("studio.failing");
        var dependentId = DefinitionId("studio.dependent");
        var unrelatedId = DefinitionId("studio.unrelated");
        var unrelatedActivation = new TestActivation();
        var definitions = CreateDefinitions(
            (failingId, new TestModule(
                activate: _ => throw new InvalidOperationException("activation failed"))),
            (dependentId, new TestModule(
                configure: editor => editor.Dependencies.RequireModule(failingId))),
            (unrelatedId, new TestModule(
                activate: _ => ValueTask.FromResult<IEditorModuleActivation>(unrelatedActivation))));
        var partition = CreateProjectPartition(
            definitions,
            "44444444-4444-4444-4444-444444444444");
        await using var host = new EditorModuleHost();

        var activation = await host.ActivateScopeAsync(partition, []);

        Assert.Equal(EditorModuleInstanceState.Faulted, activation.Instances[failingId].State);
        Assert.IsType<InvalidOperationException>(activation.Instances[failingId].Failure);
        Assert.Equal(EditorModuleInstanceState.Blocked, activation.Instances[dependentId].State);
        Assert.Equal(EditorModuleInstanceState.Active, activation.Instances[unrelatedId].State);
    }

    [Fact]
    public async Task Disposal_is_dependents_first_then_reverse_registration_order()
    {
        var disposalOrder = new List<string>();
        var dependencyId = DefinitionId("studio.dependency");
        var dependentId = DefinitionId("studio.dependent");
        var unrelatedId = DefinitionId("studio.unrelated");
        var definitions = CreateDefinitions(
            (dependencyId, ActiveModule("dependency", disposalOrder)),
            (dependentId, new TestModule(
                configure: editor => editor.Dependencies.RequireModule(dependencyId),
                activate: _ => ValueTask.FromResult<IEditorModuleActivation>(
                    new TestActivation(() => disposalOrder.Add("dependent"))))),
            (unrelatedId, ActiveModule("unrelated", disposalOrder)));
        var partition = CreateProjectPartition(
            definitions,
            "55555555-5555-5555-5555-555555555555");
        await using var host = new EditorModuleHost();
        var activation = await host.ActivateScopeAsync(partition, []);

        await activation.DisposeAsync();

        Assert.Equal(["unrelated", "dependent", "dependency"], disposalOrder);
        Assert.All(
            activation.Instances.Values,
            instance => Assert.Equal(EditorModuleInstanceState.Disposed, instance.State));
    }

    private static TestModule ActiveModule(string name, ICollection<string> disposalOrder)
    {
        return new TestModule(
            activate: _ => ValueTask.FromResult<IEditorModuleActivation>(
                new TestActivation(() => disposalOrder.Add(name))));
    }

    private static EditorModuleDefinition CreateDefinition(string module, TestModule instance)
    {
        var id = DefinitionId(module);
        return CreateDefinitions((id, instance))[0];
    }

    private static IReadOnlyList<EditorModuleDefinition> CreateDefinitions(
        params (EditorModuleDefinitionId Id, TestModule Module)[] modules)
    {
        var registrations = new List<StaticEditorModuleRegistration>();
        foreach (var (id, module) in modules)
        {
            var metadata = new EditorModuleMetadata(
                id,
                "Tests.TestModule",
                EditorModuleActivationPolicy.OnScopeReady,
                EditorModuleHandoverPolicy.Coexist);
            registrations.Add(new StaticEditorModuleRegistration(id, () => module, metadata));
        }

        var generation = StaticPackageGenerationHost.Create(registrations);
        var definitions = new List<EditorModuleDefinition>();
        foreach (var (id, _) in modules)
        {
            definitions.Add(generation.GetRequiredDefinition(id));
        }

        return definitions;
    }

    private static EditorScopePartition CreateProjectPartition(
        EditorModuleDefinition definition,
        string projectId)
    {
        return CreateProjectPartition([definition], projectId);
    }

    private static EditorScopePartition CreateProjectPartition(
        IReadOnlyList<EditorModuleDefinition> definitions,
        string projectId)
    {
        var registry = new EditorModuleRegistry();
        var transaction = EditorScopeTransaction.Prepare(
            registry,
            ProjectScope(projectId),
            definitions);
        transaction.Commit();
        return transaction.Candidate;
    }

    private static ScopeInstanceId ProjectScope(string value) =>
        ScopeInstanceId.ForProject(Guid.Parse(value));

    private static EditorModuleDefinitionId DefinitionId(string module) =>
        EditorModuleDefinitionId.Create(
            EditorAssemblyId.Create(
                PackageName.Create("asharia.studio"),
                EditorAssemblyName.Create("Asharia.Studio.BuiltIns")),
            ModuleLocalId.Create(module),
            EditorModuleScopeKind.Project);

    private sealed class TestModule(
        Action<EditorModuleBuilder>? configure = null,
        Func<EditorModuleContext, ValueTask<IEditorModuleActivation>>? activate = null)
        : EditorModule
    {
        public override void Configure(EditorModuleBuilder editor)
        {
            configure?.Invoke(editor);
        }

        public override ValueTask<IEditorModuleActivation> ActivateAsync(
            EditorModuleContext context,
            CancellationToken cancellationToken)
        {
            return activate?.Invoke(context) ??
                ValueTask.FromResult(EditorModuleActivation.Empty);
        }
    }

    private sealed class TestActivation(Action? dispose = null) : IEditorModuleActivation
    {
        public ValueTask<EditorModuleQuiesceResult> QuiesceAsync(
            EditorModuleStopReason reason,
            CancellationToken cancellationToken) =>
            ValueTask.FromResult(EditorModuleQuiesceResult.Ready);

        public ValueTask ResumeAsync(
            EditorModuleResumeContext context,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;

        public ValueTask DisposeAsync()
        {
            dispose?.Invoke();
            return ValueTask.CompletedTask;
        }
    }
}
