using System;

namespace Asharia.Editor.Extensions;

public readonly record struct EditorCapabilityId
{
    private readonly string? value_;

    private EditorCapabilityId(string value)
    {
        value_ = value;
    }

    public string Value => value_ ?? string.Empty;

    public bool IsValid => value_ is not null;

    public static EditorCapabilityId Create(string value)
    {
        if (!TryCreate(value, out var result))
        {
            throw new ArgumentException(
                "Editor capability id must be a lowercase namespace ending in a positive contract major.",
                nameof(value));
        }

        return result;
    }

    public static bool TryCreate(string? value, out EditorCapabilityId result)
    {
        if (EditorIdentityValidation.IsVersionedCapabilityId(value))
        {
            result = new EditorCapabilityId(value!);
            return true;
        }

        result = default;
        return false;
    }

    public override string ToString() => Value;
}

public enum EditorCapabilityState
{
    Unavailable,
    Recovering,
    Ready,
}

public readonly record struct EditorCapabilitySnapshot
{
    private EditorCapabilitySnapshot(
        EditorCapabilityId id,
        long epoch,
        EditorCapabilityState state)
    {
        Id = id;
        Epoch = epoch;
        State = state;
    }

    public EditorCapabilityId Id { get; }

    public long Epoch { get; }

    public EditorCapabilityState State { get; }

    public bool IsValid => Id.IsValid && Epoch >= 0 && Enum.IsDefined(State);

    public static EditorCapabilitySnapshot Create(
        EditorCapabilityId id,
        long epoch,
        EditorCapabilityState state)
    {
        if (!id.IsValid)
        {
            throw new ArgumentException("Capability identity is invalid.", nameof(id));
        }

        if (epoch < 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(epoch),
                epoch,
                "Capability epoch must not be negative.");
        }

        if (!Enum.IsDefined(state))
        {
            throw new ArgumentOutOfRangeException(
                nameof(state),
                state,
                "Capability state is invalid.");
        }

        return new EditorCapabilitySnapshot(id, epoch, state);
    }
}
