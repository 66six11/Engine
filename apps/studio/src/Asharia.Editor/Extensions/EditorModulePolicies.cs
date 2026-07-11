namespace Asharia.Editor.Extensions;

public enum EditorModuleScopeKind
{
    Application,
    Project,
}

public enum EditorModuleActivationPolicy
{
    OnScopeReady,
    OnDemand,
}

public enum EditorModuleHandoverPolicy
{
    Coexist,
    QuiesceThenActivate,
    RestartRequired,
}
