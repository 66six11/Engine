using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Windows.Input;
using Asharia.Studio.Application.Diagnostics;
using Editor.Shell.ViewModels;

namespace Editor.Shell.Diagnostics;

internal sealed class StudioDiagnosticsCommand(Action execute) : ICommand
{
    public event EventHandler? CanExecuteChanged
    {
        add { }
        remove { }
    }

    public bool CanExecute(object? parameter) => true;

    public void Execute(object? parameter) => execute();
}

internal sealed class StudioConsoleProjectionViewModel : ViewModelBase
{
    public const string AllSources = "All sources";

    private static readonly IReadOnlyList<StudioLogLevel> LevelChoices =
        Enum.GetValues<StudioLogLevel>();

    private readonly Action rebuild_;
    private IReadOnlyList<StudioConsoleRowViewModel> rows_ = [];
    private StudioConsoleRowViewModel? selectedRow_;
    private string searchText_ = string.Empty;
    private StudioLogLevel minimumLevel_ = StudioLogLevel.Verbose;
    private string sourceFilter_ = AllSources;
    private IReadOnlyList<string> sourceOptions_ = [AllSources];
    private bool collapseRepeated_ = true;
    private bool followTail_ = true;
    private bool isPaused_;
    private int unseenCount_;
    private long totalDropped_;
    private bool cursorExpired_;
    private bool readTruncated_;

    public StudioConsoleProjectionViewModel(
        Action clear,
        Action togglePause,
        Action rebuild)
    {
        rebuild_ = rebuild;
        ClearCommand = new StudioDiagnosticsCommand(clear);
        PauseCommand = new StudioDiagnosticsCommand(togglePause);
    }

    public IReadOnlyList<StudioConsoleRowViewModel> Rows
    {
        get => rows_;
        private set => SetProperty(ref rows_, value);
    }

    public StudioConsoleRowViewModel? SelectedRow
    {
        get => selectedRow_;
        set
        {
            if (SetProperty(ref selectedRow_, value))
            {
                OnPropertyChanged(nameof(HasSelection));
            }
        }
    }

    public bool HasSelection => SelectedRow is not null;

    public string SearchText
    {
        get => searchText_;
        set
        {
            if (SetProperty(ref searchText_, value ?? string.Empty))
            {
                rebuild_();
            }
        }
    }

    public StudioLogLevel MinimumLevel
    {
        get => minimumLevel_;
        set
        {
            if (SetProperty(ref minimumLevel_, value))
            {
                rebuild_();
            }
        }
    }

    public IReadOnlyList<StudioLogLevel> LevelOptions => LevelChoices;

    public string SourceFilter
    {
        get => sourceFilter_;
        set
        {
            var normalized = string.IsNullOrWhiteSpace(value) ? AllSources : value;
            if (SetProperty(ref sourceFilter_, normalized))
            {
                rebuild_();
            }
        }
    }

    public IReadOnlyList<string> SourceOptions
    {
        get => sourceOptions_;
        private set => SetProperty(ref sourceOptions_, value);
    }

    public bool CollapseRepeated
    {
        get => collapseRepeated_;
        set
        {
            if (SetProperty(ref collapseRepeated_, value))
            {
                rebuild_();
            }
        }
    }

    public bool IsPaused => isPaused_;

    public bool FollowTail
    {
        get => followTail_;
        set => SetProperty(ref followTail_, value);
    }

    public string PauseButtonText => IsPaused && UnseenCount > 0
        ? $"Resume ({UnseenCount})"
        : IsPaused
            ? "Resume"
            : "Pause";

    public int UnseenCount => unseenCount_;

    public ICommand ClearCommand { get; }

    public ICommand PauseCommand { get; }

    public long TotalDropped => totalDropped_;

    public bool HasHistoryGap => cursorExpired_;

    public bool IsCatchingUp => readTruncated_;

    public bool HasDataLoss => HasHistoryGap;

    public bool HasTruncatedRecords => Rows.Any(static row => row.WasTruncated);

    public bool HasHealthNotice =>
        HasHistoryGap || TotalDropped > 0 || IsCatchingUp || HasTruncatedRecords;

    public string HealthSummary => StudioDiagnosticsProjectionText.HealthSummary(
        totalDropped_,
        cursorExpired_,
        readTruncated_,
        HasTruncatedRecords);

    internal void SetPaused(bool value)
    {
        if (!SetProperty(ref isPaused_, value, nameof(IsPaused)))
        {
            return;
        }

        OnPropertyChanged(nameof(PauseButtonText));
    }

    internal void AddUnseenCount(int count)
    {
        if (count <= 0)
        {
            return;
        }

        unseenCount_ = count >= int.MaxValue - unseenCount_
            ? int.MaxValue
            : unseenCount_ + count;
        OnPropertyChanged(nameof(UnseenCount));
        OnPropertyChanged(nameof(PauseButtonText));
    }

    internal void ResetUnseenCount()
    {
        if (unseenCount_ == 0)
        {
            return;
        }

        unseenCount_ = 0;
        OnPropertyChanged(nameof(UnseenCount));
        OnPropertyChanged(nameof(PauseButtonText));
    }

    internal void ResetHubState()
    {
        totalDropped_ = 0;
        cursorExpired_ = false;
        readTruncated_ = false;
        ResetUnseenCount();
        OnPropertyChanged(nameof(TotalDropped));
        OnPropertyChanged(nameof(HasHistoryGap));
        OnPropertyChanged(nameof(IsCatchingUp));
        OnPropertyChanged(nameof(HasDataLoss));
        OnPropertyChanged(nameof(HealthSummary));
        OnPropertyChanged(nameof(HasHealthNotice));
    }

    internal void UpdateHubState(
        long totalDropped,
        bool cursorExpired,
        bool readTruncated)
    {
        var changed = totalDropped_ != totalDropped
            || (!cursorExpired_ && cursorExpired)
            || readTruncated_ != readTruncated;
        totalDropped_ = totalDropped;
        cursorExpired_ |= cursorExpired;
        readTruncated_ = readTruncated;
        if (changed)
        {
            OnPropertyChanged(nameof(TotalDropped));
            OnPropertyChanged(nameof(HasHistoryGap));
            OnPropertyChanged(nameof(IsCatchingUp));
            OnPropertyChanged(nameof(HasDataLoss));
            OnPropertyChanged(nameof(HealthSummary));
            OnPropertyChanged(nameof(HasHealthNotice));
        }
    }

    internal void Rebuild(
        IReadOnlyList<StudioLogRecord> records,
        long viewFloor)
    {
        SourceOptions = StudioDiagnosticsProjectionText.CreateSourceOptions(
            records.Select(static record => record.Source));
        if (!SourceOptions.Contains(SourceFilter, StringComparer.Ordinal))
        {
            sourceFilter_ = AllSources;
            OnPropertyChanged(nameof(SourceFilter));
        }

        var selectedSequence = SelectedRow?.SequenceId;
        var selectedCollapseKey = SelectedRow?.CollapseKey;
        var filtered = records.Where(record =>
            record.SequenceId > viewFloor
            && record.Level >= MinimumLevel
            && (SourceFilter == AllSources
                || string.Equals(record.Source, SourceFilter, StringComparison.Ordinal))
            && StudioDiagnosticsProjectionText.ContainsSearch(
                SearchText,
                record.Channel,
                record.Source,
                record.RenderedMessage));

        Rows = CollapseRepeated
            ? Collapse(filtered)
            : filtered.Select(static record => new StudioConsoleRowViewModel(record, 1))
                .ToArray();
        SelectedRow = selectedSequence is null
            ? null
            : Rows.FirstOrDefault(row => row.SequenceId == selectedSequence)
                ?? Rows.FirstOrDefault(row => string.Equals(
                    row.CollapseKey,
                    selectedCollapseKey,
                    StringComparison.Ordinal));
        OnPropertyChanged(nameof(HasTruncatedRecords));
        OnPropertyChanged(nameof(HealthSummary));
        OnPropertyChanged(nameof(HasHealthNotice));
    }

    private static IReadOnlyList<StudioConsoleRowViewModel> Collapse(
        IEnumerable<StudioLogRecord> records)
    {
        var groups = new List<ConsoleCollapseGroup>();
        var indices = new Dictionary<string, int>(StringComparer.Ordinal);
        foreach (var record in records)
        {
            var key = StudioDiagnosticsProjectionText.CreateLogCollapseKey(record);
            if (indices.TryGetValue(key, out var index))
            {
                var group = groups[index];
                groups[index] = group with
                {
                    Last = record,
                    RepeatCount = group.RepeatCount + 1,
                    WasTruncated = group.WasTruncated || record.WasTruncated,
                };
                continue;
            }

            indices.Add(key, groups.Count);
            groups.Add(new ConsoleCollapseGroup(record, record, 1, record.WasTruncated));
        }

        return groups.Select(static group => new StudioConsoleRowViewModel(
            group.First,
            group.Last,
            group.RepeatCount,
            group.WasTruncated)).ToArray();
    }

    private sealed record ConsoleCollapseGroup(
        StudioLogRecord First,
        StudioLogRecord Last,
        int RepeatCount,
        bool WasTruncated);
}

internal sealed class StudioProblemsProjectionViewModel : ViewModelBase
{
    public const string AllSources = "All sources";

    private static readonly IReadOnlyList<StudioDiagnosticSeverity> SeverityChoices =
        Enum.GetValues<StudioDiagnosticSeverity>();

    private readonly Action rebuild_;
    private IReadOnlyList<StudioProblemRowViewModel> rows_ = [];
    private StudioProblemRowViewModel? selectedRow_;
    private string searchText_ = string.Empty;
    private StudioDiagnosticSeverity minimumSeverity_ = StudioDiagnosticSeverity.Debug;
    private string sourceFilter_ = AllSources;
    private IReadOnlyList<string> sourceOptions_ = [AllSources];
    private bool collapseRepeated_ = true;
    private long totalDropped_;
    private bool cursorExpired_;
    private bool readTruncated_;

    public StudioProblemsProjectionViewModel(Action clear, Action rebuild)
    {
        rebuild_ = rebuild;
        ClearCommand = new StudioDiagnosticsCommand(clear);
    }

    public IReadOnlyList<StudioProblemRowViewModel> Rows
    {
        get => rows_;
        private set => SetProperty(ref rows_, value);
    }

    public StudioProblemRowViewModel? SelectedRow
    {
        get => selectedRow_;
        set
        {
            if (SetProperty(ref selectedRow_, value))
            {
                OnPropertyChanged(nameof(HasSelection));
            }
        }
    }

    public bool HasSelection => SelectedRow is not null;

    public string SearchText
    {
        get => searchText_;
        set
        {
            if (SetProperty(ref searchText_, value ?? string.Empty))
            {
                rebuild_();
            }
        }
    }

    public StudioDiagnosticSeverity MinimumSeverity
    {
        get => minimumSeverity_;
        set
        {
            if (SetProperty(ref minimumSeverity_, value))
            {
                rebuild_();
            }
        }
    }

    public IReadOnlyList<StudioDiagnosticSeverity> SeverityOptions => SeverityChoices;

    public string SourceFilter
    {
        get => sourceFilter_;
        set
        {
            var normalized = string.IsNullOrWhiteSpace(value) ? AllSources : value;
            if (SetProperty(ref sourceFilter_, normalized))
            {
                rebuild_();
            }
        }
    }

    public IReadOnlyList<string> SourceOptions
    {
        get => sourceOptions_;
        private set => SetProperty(ref sourceOptions_, value);
    }

    public bool CollapseRepeated
    {
        get => collapseRepeated_;
        set
        {
            if (SetProperty(ref collapseRepeated_, value))
            {
                rebuild_();
            }
        }
    }

    public ICommand ClearCommand { get; }

    public long TotalDropped => totalDropped_;

    public bool HasHistoryGap => cursorExpired_;

    public bool IsCatchingUp => readTruncated_;

    public bool HasDataLoss => HasHistoryGap;

    public bool HasTruncatedRecords => Rows.Any(static row => row.WasTruncated);

    public bool HasHealthNotice =>
        HasHistoryGap || TotalDropped > 0 || IsCatchingUp || HasTruncatedRecords;

    public string HealthSummary => StudioDiagnosticsProjectionText.HealthSummary(
        totalDropped_,
        cursorExpired_,
        readTruncated_,
        HasTruncatedRecords);

    internal void ResetHubState()
    {
        totalDropped_ = 0;
        cursorExpired_ = false;
        readTruncated_ = false;
        OnPropertyChanged(nameof(TotalDropped));
        OnPropertyChanged(nameof(HasHistoryGap));
        OnPropertyChanged(nameof(IsCatchingUp));
        OnPropertyChanged(nameof(HasDataLoss));
        OnPropertyChanged(nameof(HealthSummary));
        OnPropertyChanged(nameof(HasHealthNotice));
    }

    internal void UpdateHubState(
        long totalDropped,
        bool cursorExpired,
        bool readTruncated)
    {
        var changed = totalDropped_ != totalDropped
            || (!cursorExpired_ && cursorExpired)
            || readTruncated_ != readTruncated;
        totalDropped_ = totalDropped;
        cursorExpired_ |= cursorExpired;
        readTruncated_ = readTruncated;
        if (changed)
        {
            OnPropertyChanged(nameof(TotalDropped));
            OnPropertyChanged(nameof(HasHistoryGap));
            OnPropertyChanged(nameof(IsCatchingUp));
            OnPropertyChanged(nameof(HasDataLoss));
            OnPropertyChanged(nameof(HealthSummary));
            OnPropertyChanged(nameof(HasHealthNotice));
        }
    }

    internal void Rebuild(IReadOnlyList<StudioDiagnosticRecord> records, long viewFloor)
    {
        SourceOptions = StudioDiagnosticsProjectionText.CreateSourceOptions(
            records.Select(static record => record.Source));
        if (!SourceOptions.Contains(SourceFilter, StringComparer.Ordinal))
        {
            sourceFilter_ = AllSources;
            OnPropertyChanged(nameof(SourceFilter));
        }

        var selectedSequence = SelectedRow?.SequenceId;
        var selectedCollapseKey = SelectedRow?.CollapseKey;
        var filtered = records.Where(record =>
            record.SequenceId > viewFloor
            && record.Severity >= MinimumSeverity
            && (SourceFilter == AllSources
                || string.Equals(record.Source, SourceFilter, StringComparison.Ordinal))
            && StudioDiagnosticsProjectionText.ContainsSearch(
                SearchText,
                record.Code,
                record.Category,
                record.Source,
                record.Message,
                record.Remediation));

        Rows = CollapseRepeated
            ? Collapse(filtered)
            : filtered.Select(static record => new StudioProblemRowViewModel(record, 1))
                .ToArray();
        SelectedRow = selectedSequence is null
            ? null
            : Rows.FirstOrDefault(row => row.SequenceId == selectedSequence)
                ?? Rows.FirstOrDefault(row => string.Equals(
                    row.CollapseKey,
                    selectedCollapseKey,
                    StringComparison.Ordinal));
        OnPropertyChanged(nameof(HasTruncatedRecords));
        OnPropertyChanged(nameof(HealthSummary));
        OnPropertyChanged(nameof(HasHealthNotice));
    }

    private static IReadOnlyList<StudioProblemRowViewModel> Collapse(
        IEnumerable<StudioDiagnosticRecord> records)
    {
        var groups = new List<ProblemCollapseGroup>();
        var indices = new Dictionary<string, int>(StringComparer.Ordinal);
        foreach (var record in records)
        {
            var key = StudioDiagnosticsProjectionText.CreateProblemCollapseKey(record);
            if (indices.TryGetValue(key, out var index))
            {
                var group = groups[index];
                groups[index] = group with
                {
                    Last = record,
                    RepeatCount = group.RepeatCount + Math.Max(1, record.RepeatCount),
                    WasTruncated = group.WasTruncated || record.WasTruncated,
                };
                continue;
            }

            indices.Add(key, groups.Count);
            groups.Add(new ProblemCollapseGroup(
                record,
                record,
                Math.Max(1, record.RepeatCount),
                record.WasTruncated));
        }

        return groups.Select(static group => new StudioProblemRowViewModel(
            group.First,
            group.Last,
            group.RepeatCount,
            group.WasTruncated)).ToArray();
    }

    private sealed record ProblemCollapseGroup(
        StudioDiagnosticRecord First,
        StudioDiagnosticRecord Last,
        int RepeatCount,
        bool WasTruncated);
}

internal sealed record StudioConsoleRowViewModel
{
    public StudioConsoleRowViewModel(StudioLogRecord record, int repeatCount)
        : this(record, record, repeatCount)
    {
    }

    public StudioConsoleRowViewModel(
        StudioLogRecord first,
        StudioLogRecord last,
        int repeatCount,
        bool? wasTruncated = null)
    {
        SequenceId = first.SequenceId;
        LastSequenceId = last.SequenceId;
        TimestampUtc = first.TimestampUtc;
        Level = first.Level;
        Channel = first.Channel;
        Source = first.Source;
        Message = first.RenderedMessage;
        RepeatCount = repeatCount;
        WasTruncated = wasTruncated ?? (first.WasTruncated || last.WasTruncated);
        CollapseKey = StudioDiagnosticsProjectionText.CreateLogCollapseKey(first);
        DetailsText = StudioDiagnosticsProjectionText.FormatLogDetails(
            first,
            last,
            repeatCount);
    }

    public long SequenceId { get; }

    public long LastSequenceId { get; }

    public DateTimeOffset TimestampUtc { get; }

    public string TimestampText => TimestampUtc.ToLocalTime().ToString("HH:mm:ss.fff", CultureInfo.CurrentCulture);

    public StudioLogLevel Level { get; }

    public string LevelText => Level.ToString();

    public string Channel { get; }

    public string Source { get; }

    public string Message { get; }

    public int RepeatCount { get; }

    public string RepeatText => RepeatCount > 1 ? $"x{RepeatCount}" : string.Empty;

    public bool WasTruncated { get; }

    public string DetailsText { get; }

    internal string CollapseKey { get; }

}

internal sealed record StudioProblemRowViewModel
{
    public StudioProblemRowViewModel(StudioDiagnosticRecord record, int repeatCount)
        : this(record, record, repeatCount)
    {
    }

    public StudioProblemRowViewModel(
        StudioDiagnosticRecord first,
        StudioDiagnosticRecord last,
        int repeatCount,
        bool? wasTruncated = null)
    {
        SequenceId = first.SequenceId;
        LastSequenceId = last.SequenceId;
        TimestampUtc = first.TimestampUtc;
        Severity = first.Severity;
        Code = first.Code;
        Category = first.Category;
        Source = first.Source;
        Message = first.Message;
        Remediation = first.Remediation ?? string.Empty;
        RepeatCount = repeatCount;
        WasTruncated = wasTruncated ?? (first.WasTruncated || last.WasTruncated);
        CollapseKey = StudioDiagnosticsProjectionText.CreateProblemCollapseKey(first);
        DetailsText = StudioDiagnosticsProjectionText.FormatProblemDetails(
            first,
            last,
            repeatCount);
    }

    public long SequenceId { get; }

    public long LastSequenceId { get; }

    public DateTimeOffset TimestampUtc { get; }

    public string TimestampText => TimestampUtc.ToLocalTime().ToString("HH:mm:ss.fff", CultureInfo.CurrentCulture);

    public StudioDiagnosticSeverity Severity { get; }

    public string SeverityText => Severity.ToString();

    public string Code { get; }

    public string Category { get; }

    public string Source { get; }

    public string Message { get; }

    public string Remediation { get; }

    public int RepeatCount { get; }

    public string RepeatText => RepeatCount > 1 ? $"x{RepeatCount}" : string.Empty;

    public bool WasTruncated { get; }

    public string DetailsText { get; }

    internal string CollapseKey { get; }
}

internal static class StudioDiagnosticsProjectionText
{
    public static IReadOnlyList<string> CreateSourceOptions(IEnumerable<string> sources) =>
        new[] { StudioConsoleProjectionViewModel.AllSources }
            .Concat(sources.Distinct(StringComparer.Ordinal).OrderBy(static value => value, StringComparer.Ordinal))
            .ToArray();

    public static bool ContainsSearch(string search, params string?[] candidates)
    {
        if (string.IsNullOrWhiteSpace(search))
        {
            return true;
        }

        return candidates.Any(candidate => candidate?.Contains(
            search,
            StringComparison.CurrentCultureIgnoreCase) == true);
    }

    public static string CreateLogCollapseKey(StudioLogRecord record)
    {
        var key = new StringBuilder();
        AppendCollapsePart(key, ((int)record.Level).ToString(CultureInfo.InvariantCulture));
        AppendCollapsePart(key, record.Channel);
        AppendCollapsePart(key, ((int)record.Context.Origin).ToString(CultureInfo.InvariantCulture));
        AppendCollapsePart(key, record.Context.Package);
        AppendCollapsePart(key, record.Context.Component);
        AppendCollapsePart(key, record.Context.Scope.Kind);
        AppendCollapsePart(key, record.Context.Scope.Identity);
        AppendCollapsePart(
            key,
            record.Context.Scope.Generation.ToString(CultureInfo.InvariantCulture));
        AppendCollapsePart(key, FormatNullableGuid(record.Context.OperationId));
        AppendCollapsePart(key, FormatNullableGuid(record.Context.CorrelationId));
        AppendCollapsePart(key, FormatNullableGuid(record.Context.ParentCorrelationId));
        AppendCollapsePart(
            key,
            ((int)record.Context.Sensitivity).ToString(CultureInfo.InvariantCulture));
        AppendCollapsePart(
            key,
            record.ManagedThreadId.ToString(CultureInfo.InvariantCulture));
        AppendCollapsePart(key, record.MessageTemplate);
        AppendCollapsePart(key, record.RenderedMessage);
        AppendCollapsePart(key, record.WasTruncated ? "1" : "0");
        AppendCollapseAttributes(key, record.Attributes);
        return key.ToString();
    }

    public static string CreateProblemCollapseKey(StudioDiagnosticRecord record)
    {
        var key = new StringBuilder();
        AppendCollapsePart(
            key,
            ((int)record.Severity).ToString(CultureInfo.InvariantCulture));
        AppendCollapsePart(key, ((int)record.Channel).ToString(CultureInfo.InvariantCulture));
        AppendCollapsePart(key, record.Code);
        AppendCollapsePart(key, record.Category);
        AppendCollapsePart(key, record.Message);
        AppendCollapsePart(key, record.Remediation);
        AppendCollapsePart(key, record.Fingerprint);
        AppendCollapsePart(
            key,
            ((int)record.Context.Origin).ToString(CultureInfo.InvariantCulture));
        AppendCollapsePart(key, record.Context.Package);
        AppendCollapsePart(key, record.Context.Component);
        AppendCollapsePart(key, record.Context.Scope.Kind);
        AppendCollapsePart(key, record.Context.Scope.Identity);
        AppendCollapsePart(
            key,
            record.Context.Scope.Generation.ToString(CultureInfo.InvariantCulture));
        AppendCollapsePart(key, FormatNullableGuid(record.Context.OperationId));
        AppendCollapsePart(key, FormatNullableGuid(record.Context.CorrelationId));
        AppendCollapsePart(key, FormatNullableGuid(record.Context.ParentCorrelationId));
        AppendCollapsePart(
            key,
            ((int)record.Context.Sensitivity).ToString(CultureInfo.InvariantCulture));
        AppendCollapsePart(key, record.WasTruncated ? "1" : "0");
        AppendCollapseAttributes(key, record.Attributes);
        return key.ToString();
    }

    public static string HealthSummary(
        long totalDropped,
        bool cursorExpired,
        bool readTruncated,
        bool recordTruncated)
    {
        var parts = new List<string>(4);
        if (cursorExpired)
        {
            parts.Add("The view cursor expired; showing the retained window.");
        }

        if (readTruncated)
        {
            parts.Add("New records remain; the view will continue catching up.");
        }

        if (totalDropped > 0)
        {
            parts.Add($"The source ring overwrote {totalDropped} record(s) outside the current retained window.");
        }

        if (recordTruncated)
        {
            parts.Add("One or more records were truncated at ingress.");
        }

        return string.Join(' ', parts);
    }

    public static string FormatLogDetails(
        StudioLogRecord first,
        StudioLogRecord last,
        int repeatCount)
    {
        var details = new StringBuilder()
            .Append(first.TimestampUtc.ToString("O", CultureInfo.InvariantCulture))
            .Append(" | ")
            .Append(first.Level)
            .Append(" | ")
            .Append(first.Channel)
            .Append(" | ")
            .Append(first.Source)
            .AppendLine()
            .Append(first.RenderedMessage);
        AppendRepeatSummary(details, last.TimestampUtc, repeatCount);
        AppendContext(details, first.Context);
        AppendAttributes(details, first.Attributes);
        return details.ToString();
    }

    public static string FormatProblemDetails(
        StudioDiagnosticRecord first,
        StudioDiagnosticRecord last,
        int repeatCount)
    {
        var details = new StringBuilder()
            .Append(first.TimestampUtc.ToString("O", CultureInfo.InvariantCulture))
            .Append(" | ")
            .Append(first.Severity)
            .Append(" | ")
            .Append(first.Code)
            .Append(" | ")
            .Append(first.Source)
            .AppendLine()
            .Append(first.Message);
        if (!string.IsNullOrWhiteSpace(first.Remediation))
        {
            details.AppendLine().Append("Action: ").Append(first.Remediation);
        }

        AppendRepeatSummary(details, last.TimestampUtc, repeatCount);
        AppendContext(details, first.Context);
        AppendAttributes(details, first.Attributes);
        return details.ToString();
    }

    private static void AppendContext(StringBuilder details, StudioDiagnosticContext context)
    {
        details.AppendLine()
            .Append("Origin: ")
            .Append(context.Origin)
            .AppendLine()
            .Append("Package: ")
            .Append(context.Package)
            .AppendLine()
            .Append("Component: ")
            .Append(context.Component)
            .AppendLine()
            .Append("Sensitivity: ")
            .Append(context.Sensitivity)
            .AppendLine()
            .Append("Scope: ")
            .Append(context.Scope.Kind)
            .Append('/')
            .Append(context.Scope.Identity)
            .Append('@')
            .Append(context.Scope.Generation);
        if (context.OperationId is { } operationId)
        {
            details.AppendLine().Append("Operation: ").Append(operationId.ToString("D"));
        }

        if (context.CorrelationId is { } correlationId)
        {
            details.AppendLine().Append("Correlation: ").Append(correlationId.ToString("D"));
        }

        if (context.ParentCorrelationId is { } parentCorrelationId)
        {
            details.AppendLine()
                .Append("Parent correlation: ")
                .Append(parentCorrelationId.ToString("D"));
        }
    }

    private static void AppendAttributes(
        StringBuilder details,
        ImmutableArray<StudioDiagnosticAttribute> attributes)
    {
        foreach (var attribute in attributes)
        {
            details.AppendLine()
                .Append(attribute.Name)
                .Append(": ")
                .Append(attribute.Value);
        }
    }

    private static void AppendCollapseAttributes(
        StringBuilder key,
        ImmutableArray<StudioDiagnosticAttribute> attributes)
    {
        foreach (var attribute in attributes)
        {
            AppendCollapsePart(key, attribute.Name);
            AppendCollapsePart(key, attribute.Value);
        }
    }

    private static void AppendCollapsePart(StringBuilder key, string? value)
    {
        if (value is null)
        {
            key.Append("-1:");
            return;
        }

        key.Append(value.Length.ToString(CultureInfo.InvariantCulture))
            .Append(':')
            .Append(value);
    }

    private static string? FormatNullableGuid(Guid? value) =>
        value?.ToString("D", CultureInfo.InvariantCulture);

    private static void AppendRepeatSummary(
        StringBuilder details,
        DateTimeOffset lastTimestamp,
        int repeatCount)
    {
        if (repeatCount <= 1)
        {
            return;
        }

        details.AppendLine()
            .Append("Occurrences: ")
            .Append(repeatCount.ToString(CultureInfo.InvariantCulture))
            .Append("; last: ")
            .Append(lastTimestamp.ToString("O", CultureInfo.InvariantCulture));
    }
}
