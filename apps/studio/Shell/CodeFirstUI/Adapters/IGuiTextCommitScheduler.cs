using System;

namespace Editor.Shell.CodeFirstUI.Adapters;

internal interface IGuiTextCommitScheduler
{
    IDisposable Schedule(TimeSpan delay, Action action);
}
