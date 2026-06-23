using System;
using System.Collections.Generic;
using System.Globalization;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Core.Services;
using Editor.Shell.Services;
using Editor.Shell.ViewModels;

namespace Editor.Features.Console.ViewModels;

public sealed class ConsolePanelViewModel : ViewModelBase, IDisposable
{
    private readonly IEditorDiagnosticService diagnostics_;
    private readonly IEditorUiDispatcher uiDispatcher_;
    private IReadOnlyList<EditorDiagnosticRecord> records_ = [];
    private string recordCountText_ = "0";

    public ConsolePanelViewModel(IEditorDiagnosticService? diagnostics = null)
        : this(diagnostics, uiDispatcher: null)
    {
    }

    internal ConsolePanelViewModel(
        IEditorDiagnosticService? diagnostics,
        IEditorUiDispatcher? uiDispatcher)
    {
        diagnostics_ = diagnostics ?? new EditorDiagnosticService();
        uiDispatcher_ = uiDispatcher ?? new AvaloniaEditorUiDispatcher();
        RefreshRecords();
        diagnostics_.DiagnosticsChanged += OnDiagnosticsChanged;
    }

    public IReadOnlyList<EditorDiagnosticRecord> Records
    {
        get => records_;
        private set => SetProperty(ref records_, value);
    }

    public string RecordCountText
    {
        get => recordCountText_;
        private set => SetProperty(ref recordCountText_, value);
    }

    public void Dispose()
    {
        diagnostics_.DiagnosticsChanged -= OnDiagnosticsChanged;
    }

    private void OnDiagnosticsChanged(object? sender, EventArgs e)
    {
        if (uiDispatcher_.CheckAccess())
        {
            RefreshRecords();
            return;
        }

        uiDispatcher_.Post(RefreshRecords);
    }

    private void RefreshRecords()
    {
        Records = diagnostics_.GetRecentDiagnostics();
        RecordCountText = Records.Count.ToString(CultureInfo.InvariantCulture);
    }
}
