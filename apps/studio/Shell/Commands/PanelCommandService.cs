using System;
using Editor.Shell.ViewModels.Docking;

namespace Editor.Shell.Commands;

internal sealed class PanelCommandService
{
    private readonly EditorDockWorkspaceViewModel mainWorkspace_;
    private Func<string, bool>? focusExternalPanel_;
    private Func<string, bool>? isExternalPanelOpen_;
    private Func<string, bool>? closeExternalPanel_;

    public PanelCommandService(EditorDockWorkspaceViewModel mainWorkspace)
    {
        mainWorkspace_ = mainWorkspace;
        mainWorkspace_.DockContentChanged += OnMainWorkspaceDockContentChanged;
    }

    public event EventHandler? PanelStateChanged;

    public void SetExternalPanelCallbacks(
        Func<string, bool> focusExternalPanel,
        Func<string, bool> isExternalPanelOpen,
        Func<string, bool>? closeExternalPanel = null)
    {
        focusExternalPanel_ = focusExternalPanel;
        isExternalPanelOpen_ = isExternalPanelOpen;
        closeExternalPanel_ = closeExternalPanel;
        RaisePanelStateChanged();
    }

    public bool OpenOrFocusPanel(string? panelId)
    {
        if (!TryGetPanelId(panelId, out var normalizedPanelId))
        {
            return false;
        }

        return FocusPanel(normalizedPanelId)
            || mainWorkspace_.OpenPanel(normalizedPanelId);
    }

    public bool FocusPanel(string? panelId)
    {
        if (!TryGetPanelId(panelId, out var normalizedPanelId))
        {
            return false;
        }

        return mainWorkspace_.ActivatePanel(normalizedPanelId)
            || focusExternalPanel_?.Invoke(normalizedPanelId) == true;
    }

    public bool ClosePanel(string? panelId)
    {
        if (!TryGetPanelId(panelId, out var normalizedPanelId))
        {
            return false;
        }

        if (mainWorkspace_.ClosePanel(normalizedPanelId))
        {
            return true;
        }

        if (closeExternalPanel_?.Invoke(normalizedPanelId) != true)
        {
            return false;
        }

        RaisePanelStateChanged();
        return true;
    }

    public bool IsPanelOpen(string? panelId)
    {
        return TryGetPanelId(panelId, out var normalizedPanelId)
            && (mainWorkspace_.ContainsPanel(normalizedPanelId)
                || isExternalPanelOpen_?.Invoke(normalizedPanelId) == true);
    }

    private void OnMainWorkspaceDockContentChanged(object? sender, EventArgs e)
    {
        RaisePanelStateChanged();
    }

    private void RaisePanelStateChanged()
    {
        PanelStateChanged?.Invoke(this, EventArgs.Empty);
    }

    private static bool TryGetPanelId(string? panelId, out string normalizedPanelId)
    {
        if (string.IsNullOrWhiteSpace(panelId))
        {
            normalizedPanelId = string.Empty;
            return false;
        }

        normalizedPanelId = panelId;
        return true;
    }
}
