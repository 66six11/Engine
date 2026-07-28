using System;

namespace Asharia.Studio.Application.Extensions;

internal sealed class EditorScopeRegistration : IDisposable
{
    private readonly object gate_ = new();
    private readonly EditorModuleRegistry registry_;
    private bool isDisposed_;

    internal EditorScopeRegistration(
        EditorModuleRegistry registry,
        EditorScopePartition partition)
    {
        registry_ = registry ?? throw new ArgumentNullException(nameof(registry));
        Partition = partition ?? throw new ArgumentNullException(nameof(partition));
    }

    public EditorScopePartition Partition { get; }

    public void Dispose()
    {
        lock (gate_)
        {
            if (isDisposed_)
            {
                return;
            }

            if (!registry_.TryRemove(Partition))
            {
                throw new InvalidOperationException(
                    "The editor scope registration no longer owns the committed partition.");
            }

            isDisposed_ = true;
        }
    }
}
