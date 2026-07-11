using System;
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
    public void Kernel_type_is_owned_by_public_editor_api(Type type)
    {
        Assert.Equal("Asharia.Editor", type.Assembly.GetName().Name);
    }
}
