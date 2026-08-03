using System;
using System.Buffers.Binary;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

namespace Asharia.Studio.Observe.Client;

internal static class PipeFrameClientProtocol
{
    private const int HeaderBytes = sizeof(int);

    internal static async ValueTask<byte[]?> ReadAsync(
        Stream stream,
        int maxPayloadBytes,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(stream);
        ValidateMaximum(maxPayloadBytes);
        var header = new byte[HeaderBytes];
        var headerRead = await ReadAtMostAsync(
                stream,
                header,
                cancellationToken)
            .ConfigureAwait(false);
        if (headerRead == 0)
        {
            return null;
        }

        if (headerRead != HeaderBytes)
        {
            throw new InvalidDataException("Pipe frame header was truncated.");
        }

        var payloadBytes = BinaryPrimitives.ReadInt32LittleEndian(header);
        if (payloadBytes <= 0 || payloadBytes > maxPayloadBytes)
        {
            throw new InvalidDataException(
                "Pipe frame length is outside the typed protocol bound.");
        }

        var payload = new byte[payloadBytes];
        var payloadRead = await ReadAtMostAsync(
                stream,
                payload,
                cancellationToken)
            .ConfigureAwait(false);
        if (payloadRead != payloadBytes)
        {
            throw new InvalidDataException("Pipe frame payload was truncated.");
        }

        return payload;
    }

    internal static async ValueTask WriteAsync(
        Stream stream,
        ReadOnlyMemory<byte> payload,
        int maxPayloadBytes,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(stream);
        ValidateMaximum(maxPayloadBytes);
        if (payload.Length <= 0 || payload.Length > maxPayloadBytes)
        {
            throw new ArgumentOutOfRangeException(nameof(payload));
        }

        var header = new byte[HeaderBytes];
        BinaryPrimitives.WriteInt32LittleEndian(header, payload.Length);
        await stream.WriteAsync(header, cancellationToken).ConfigureAwait(false);
        await stream.WriteAsync(payload, cancellationToken).ConfigureAwait(false);
        await stream.FlushAsync(cancellationToken).ConfigureAwait(false);
    }

    private static async ValueTask<int> ReadAtMostAsync(
        Stream stream,
        Memory<byte> buffer,
        CancellationToken cancellationToken)
    {
        var total = 0;
        while (total < buffer.Length)
        {
            var read = await stream.ReadAsync(
                    buffer[total..],
                    cancellationToken)
                .ConfigureAwait(false);
            if (read == 0)
            {
                break;
            }

            total += read;
        }

        return total;
    }

    private static void ValidateMaximum(int maxPayloadBytes)
    {
        if (maxPayloadBytes <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(maxPayloadBytes));
        }
    }
}
