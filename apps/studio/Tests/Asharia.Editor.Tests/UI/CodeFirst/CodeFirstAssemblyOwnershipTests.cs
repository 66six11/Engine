using System;
using System.Collections.Generic;
using Asharia.Editor.Panels;
using Asharia.Editor.UI.CodeFirst.Abstractions;
using Asharia.Editor.UI.CodeFirst.Authoring;
using Asharia.Editor.UI.CodeFirst.Building;
using Asharia.Editor.UI.CodeFirst.Events;
using Asharia.Editor.UI.CodeFirst.Models;
using Asharia.Editor.UI.CodeFirst.State;
using Asharia.Editor.UI.CodeFirst.Validation;
using Xunit;

namespace Asharia.Editor.Tests.UI.CodeFirst;

public sealed class CodeFirstAssemblyOwnershipTests
{
    [Theory]
    [InlineData(typeof(GuiFrameBuilder))]
    [InlineData(typeof(GuiEventQueue))]
    [InlineData(typeof(GuiTreeSnapshot))]
    [InlineData(typeof(GuiStateStore))]
    [InlineData(typeof(GuiTreeValidator))]
    [InlineData(typeof(EditorGui))]
    [InlineData(typeof(CodeFirstEditorPanel))]
    [InlineData(typeof(IEditorGuiCommandExecutor))]
    [InlineData(typeof(ICodeFirstEditorPanelHost))]
    public void Kernel_type_is_owned_by_public_editor_api(Type type)
    {
        Assert.Equal("Asharia.Editor", type.Assembly.GetName().Name);
    }

    [Fact]
    public void Panel_host_spi_dispatches_to_protected_authoring_hooks()
    {
        var panel = new RecordingPanel();
        var host = Assert.IsAssignableFrom<ICodeFirstEditorPanelHost>(panel);
        var lifecycle = new EditorPanelLifecycleContext(
            "test.panel",
            "Test",
            EditorDockArea.Center,
            IsFloatingWorkspace: false);

        host.Create(lifecycle);
        host.Enable();
        host.Disable();
        host.Destroy();

        Assert.Equal(["create", "enable", "disable", "destroy"], panel.Events);
    }

    private sealed class RecordingPanel : CodeFirstEditorPanel
    {
        public List<string> Events { get; } = [];

        protected override void OnCreate(EditorPanelLifecycleContext context)
        {
            Events.Add("create");
        }

        protected override void OnEnable()
        {
            Events.Add("enable");
        }

        protected override void OnGui(EditorGui gui)
        {
        }

        protected override void OnDisable()
        {
            Events.Add("disable");
        }

        protected override void OnDestroy()
        {
            Events.Add("destroy");
        }
    }
}
