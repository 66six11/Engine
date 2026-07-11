using System;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Editor.Extensions;
using Xunit;

namespace Asharia.Editor.Tests.Extensions;

public sealed class EditorModuleActivationTests
{
    [Fact]
    public async Task Default_activation_returns_the_shared_empty_lease()
    {
        var module = new ContributionOnlyModule();
        var context = new EditorModuleContext(CreateInstanceId(), []);

        var first = await module.ActivateAsync(context, CancellationToken.None);
        var second = await module.ActivateAsync(context, CancellationToken.None);

        Assert.Same(EditorModuleActivation.Empty, first);
        Assert.Same(first, second);
    }

    [Fact]
    public async Task Empty_activation_is_synchronous_idempotent_and_recoverable()
    {
        var activation = EditorModuleActivation.Empty;

        var quiesce = activation.QuiesceAsync(
            EditorModuleStopReason.Reload,
            CancellationToken.None);
        var resume = activation.ResumeAsync(
            new EditorModuleResumeContext(
                EditorModuleResumeReason.ReloadRollback,
                ScopeInstanceId.Application,
                []),
            CancellationToken.None);
        var firstDispose = activation.DisposeAsync();
        var secondDispose = activation.DisposeAsync();

        Assert.True(quiesce.IsCompletedSuccessfully);
        Assert.Equal(EditorModuleQuiesceResult.Ready, await quiesce);
        Assert.True(resume.IsCompletedSuccessfully);
        Assert.True(firstDispose.IsCompletedSuccessfully);
        Assert.True(secondDispose.IsCompletedSuccessfully);
        await resume;
        await firstDispose;
        await secondDispose;
    }

    [Fact]
    public void Empty_activation_rejects_unknown_stop_reason()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            EditorModuleActivation.Empty.QuiesceAsync(
                (EditorModuleStopReason)99,
                CancellationToken.None));
    }

    private static EditorModuleInstanceId CreateInstanceId()
    {
        var definition = EditorModuleDefinitionId.Create(
            EditorAssemblyId.Create(
                PackageName.Create("com.asharia.terrain"),
                EditorAssemblyName.Create("Asharia.Terrain.Editor")),
            ModuleLocalId.Create("terrain.editor"),
            EditorModuleScopeKind.Application);
        return EditorModuleInstanceId.Create(definition, ScopeInstanceId.Application);
    }

    private sealed class ContributionOnlyModule : EditorModule
    {
        public override void Configure(EditorModuleBuilder editor)
        {
            ArgumentNullException.ThrowIfNull(editor);
        }
    }
}
