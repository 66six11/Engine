using System;
using System.Collections.Generic;
using System.Globalization;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Core.Models.Diagnostics;
using Editor.Core.Services;
using Editor.UI.ViewModels;

namespace Editor.Features.Problems.ViewModels;

public sealed class ProblemsPanelViewModel : ViewModelBase, IDisposable
{
    private readonly IEditorDiagnosticService diagnostics_;
    private readonly IEditorUiDispatcher uiDispatcher_;
    private IReadOnlyList<EditorDiagnosticRecord> records_ = [];
    private string recordCountText_ = "0";

    public ProblemsPanelViewModel(IEditorDiagnosticService? diagnostics = null)
        : this(diagnostics, uiDispatcher: null)
    {
    }

    internal ProblemsPanelViewModel(
        IEditorDiagnosticService? diagnostics,
        IEditorUiDispatcher? uiDispatcher)
    {
        diagnostics_ = diagnostics ?? new EditorDiagnosticService();
        uiDispatcher_ = uiDispatcher ?? new ImmediateEditorUiDispatcher();
        RefreshRecords();
        diagnostics_.DiagnosticsChanged += OnDiagnosticsChanged;
    }

    public IReadOnlyList<EditorDiagnosticRecord> Records
    {
        get => records_;
        private set
        {
            if (SetProperty(ref records_, value))
            {
                OnPropertyChanged(nameof(HasRecords));
                OnPropertyChanged(nameof(HasNoRecords));
            }
        }
    }

    public bool HasRecords => records_.Count > 0;

    public bool HasNoRecords => records_.Count == 0;

    public string EmptyStateText => "No problems";

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
        Records = diagnostics_.GetProblemDiagnostics();
        RecordCountText = Records.Count.ToString(CultureInfo.InvariantCulture);
    }
}
