using System;
using System.Collections.Generic;
using Asharia.Editor.Diagnostics;

namespace Asharia.Studio.Application.Diagnostics;

public sealed class EditorDiagnosticService : IEditorDiagnosticService
{
    public const int DefaultCapacity = 200;

    private readonly int capacity_;
    private readonly object gate_ = new();
    private readonly List<EditorDiagnosticRecord> records_ = [];
    private long nextSequenceId_;

    public EditorDiagnosticService(int capacity = DefaultCapacity)
    {
        if (capacity <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(capacity),
                capacity,
                "Diagnostic capacity must be greater than zero.");
        }

        capacity_ = capacity;
    }

    public event EventHandler? DiagnosticsChanged;

    public EditorDiagnosticRecord Publish(
        EditorDiagnosticSeverity severity,
        EditorDiagnosticChannel channel,
        string source,
        string category,
        string message)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(source);
        ArgumentException.ThrowIfNullOrWhiteSpace(category);
        ArgumentException.ThrowIfNullOrWhiteSpace(message);

        EditorDiagnosticRecord record;
        lock (gate_)
        {
            record = new EditorDiagnosticRecord(
                ++nextSequenceId_,
                severity,
                channel,
                source,
                category,
                message);
            records_.Add(record);
            TrimToCapacity();
        }

        DiagnosticsChanged?.Invoke(this, EventArgs.Empty);
        return record;
    }

    public IReadOnlyList<EditorDiagnosticRecord> GetRecentDiagnostics()
    {
        lock (gate_)
        {
            return records_.ToArray();
        }
    }

    public IReadOnlyList<EditorDiagnosticRecord> GetProblemDiagnostics()
    {
        lock (gate_)
        {
            var records = new List<EditorDiagnosticRecord>();
            foreach (var record in records_)
            {
                if (record.Channel == EditorDiagnosticChannel.Problem)
                {
                    records.Add(record);
                }
            }

            return records;
        }
    }

    public EditorDiagnosticRecord? GetLatestDiagnostic()
    {
        lock (gate_)
        {
            return records_.Count == 0 ? null : records_[^1];
        }
    }

    private void TrimToCapacity()
    {
        while (records_.Count > capacity_)
        {
            records_.RemoveAt(0);
        }
    }
}
