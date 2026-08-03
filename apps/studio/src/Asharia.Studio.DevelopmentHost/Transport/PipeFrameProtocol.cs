using System;
using System.Buffers.Binary;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

namespace Asharia.Studio.DevelopmentHost.Transport;

internal static class PipeFrameProtocol
{
    private const int HeaderSize = sizeof(int);

    public static async ValueTask<byte[]?> ReadAsync(
        Stream stream,
        int maxPayloadBytes,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(stream);
        if (maxPayloadBytes <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(maxPayloadBytes));
        }

        var header = new byte[HeaderSize];
        var firstByteCount = await stream.ReadAsync(
                header.AsMemory(0, 1),
                cancellationToken)
            .ConfigureAwait(false);
        if (firstByteCount == 0)
        {
            return null;
        }

        try
        {
            await stream.ReadExactlyAsync(
                    header.AsMemory(1, HeaderSize - 1),
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (EndOfStreamException error)
        {
            throw new InvalidDataException(
                "Pipe frame ended inside its length prefix.",
                error);
        }

        var payloadLength = BinaryPrimitives.ReadInt32LittleEndian(header);
        if (payloadLength <= 0 || payloadLength > maxPayloadBytes)
        {
            throw new InvalidDataException(
                $"Pipe frame length must be between 1 and {maxPayloadBytes} bytes.");
        }

        var payload = new byte[payloadLength];
        try
        {
            await stream.ReadExactlyAsync(payload, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (EndOfStreamException error)
        {
            throw new InvalidDataException(
                "Pipe frame ended before its declared payload length.",
                error);
        }

        return payload;
    }

    public static async ValueTask WriteAsync(
        Stream stream,
        ReadOnlyMemory<byte> payload,
        int maxPayloadBytes,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(stream);
        if (payload.IsEmpty || payload.Length > maxPayloadBytes)
        {
            throw new InvalidDataException(
                $"Pipe frame payload must be between 1 and {maxPayloadBytes} bytes.");
        }

        var header = new byte[HeaderSize];
        BinaryPrimitives.WriteInt32LittleEndian(header, payload.Length);
        await stream.WriteAsync(header, cancellationToken).ConfigureAwait(false);
        await stream.WriteAsync(payload, cancellationToken).ConfigureAwait(false);
        await stream.FlushAsync(cancellationToken).ConfigureAwait(false);
    }
}
