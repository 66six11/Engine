using Avalonia;

namespace Editor.Features.SceneView.Views;

internal sealed class SceneViewCompositionCommitState
{
    private ulong currentVersion_;
    private ulong lastSuccessfulVersion_;

    public Size? LastSuccessfulFrameSizeDip { get; private set; }

    public ulong BeginAttempt()
    {
        return ++currentVersion_;
    }

    public bool CompleteSuccessfulAttempt(
        ulong version,
        Size frameSizeDip)
    {
        if (version > lastSuccessfulVersion_)
        {
            lastSuccessfulVersion_ = version;
            LastSuccessfulFrameSizeDip = frameSizeDip;
        }

        return version == currentVersion_;
    }

    public bool TryGetRollbackTarget(
        ulong version,
        out Size? frameSizeDip)
    {
        frameSizeDip = LastSuccessfulFrameSizeDip;
        return version == currentVersion_;
    }

    public void Reset()
    {
        currentVersion_++;
        lastSuccessfulVersion_ = currentVersion_;
        LastSuccessfulFrameSizeDip = null;
    }
}
