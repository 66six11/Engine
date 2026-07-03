using System;

namespace Editor.Core.CodeFirstUI;

[Flags]
public enum GuiRebuildReason
{
    None = 0,
    InitialOpen = 1 << 0,
    LifecycleChanged = 1 << 1,
    InputEvent = 1 << 2,
    FrameTick = 1 << 3,
    ExplicitRefresh = 1 << 4,
}
