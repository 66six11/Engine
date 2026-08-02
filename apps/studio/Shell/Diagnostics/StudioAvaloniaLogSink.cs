using System;
using System.Collections.Immutable;
using System.Globalization;
using System.Text;
using Asharia.Studio.Application.Diagnostics;
using Avalonia.Logging;

namespace Editor.Shell.Diagnostics;

internal sealed class StudioAvaloniaLogSink : ILogSink
{
    private const int MaxTemplateLength = 2048;
    private const int MaxRenderedLength = 4096;
    private const int MaxPropertyCount = 16;
    private const int MaxPropertyLength = 256;

    private readonly IStudioDiagnosticHub diagnostics_;
    private readonly LogEventLevel minimumLevel_;

    public StudioAvaloniaLogSink(
        IStudioDiagnosticHub diagnostics,
        LogEventLevel minimumLevel = LogEventLevel.Warning)
    {
        ArgumentNullException.ThrowIfNull(diagnostics);
        diagnostics_ = diagnostics;
        minimumLevel_ = minimumLevel;
    }

    public bool IsEnabled(LogEventLevel level, string area) =>
        level >= minimumLevel_ && !string.IsNullOrWhiteSpace(area);

    public void Log(
        LogEventLevel level,
        string area,
        object? source,
        string messageTemplate)
    {
        Publish(level, area, source, messageTemplate, []);
    }

    public void Log(
        LogEventLevel level,
        string area,
        object? source,
        string messageTemplate,
        params object?[] propertyValues)
    {
        Publish(level, area, source, messageTemplate, propertyValues);
    }

    private void Publish(
        LogEventLevel level,
        string area,
        object? source,
        string messageTemplate,
        object?[] propertyValues)
    {
        if (!IsEnabled(level, area))
        {
            return;
        }

        try
        {
            var template = Truncate(messageTemplate, MaxTemplateLength);
            var normalizedValues = NormalizeValues(propertyValues);
            var attributes = CreateAttributes(source, normalizedValues);
            diagnostics_.PublishLog(new StudioLogWrite(
                MapLevel(level),
                Truncate(area, 64),
                new StudioDiagnosticContext(
                    StudioRecordOrigin.Framework,
                    "avalonia",
                    SourceComponent(source),
                    StudioDiagnosticScope.Process(
                        diagnostics_.ProcessIdentity),
                    Sensitivity: StudioDataSensitivity.ProjectPath),
                template,
                Render(template, normalizedValues),
                attributes));
        }
        catch
        {
            // A framework log sink must never change application control flow.
        }
    }

    private static ImmutableArray<StudioDiagnosticAttribute> CreateAttributes(
        object? source,
        string[] propertyValues)
    {
        var capacity = propertyValues.Length + (source is null ? 0 : 1);
        if (capacity == 0)
        {
            return [];
        }

        var attributes = ImmutableArray.CreateBuilder<StudioDiagnosticAttribute>(
            capacity);
        if (source is not null)
        {
            attributes.Add(new StudioDiagnosticAttribute(
                "sourceType",
                SourceComponent(source)));
        }

        for (var index = 0; index < propertyValues.Length; index++)
        {
            attributes.Add(new StudioDiagnosticAttribute(
                $"arg{index}",
                propertyValues[index]));
        }

        return attributes.MoveToImmutable();
    }

    private static string Render(string template, string[] values)
    {
        var result = new StringBuilder(Math.Min(template.Length + 64, MaxRenderedLength));
        var valueIndex = 0;
        for (var index = 0; index < template.Length && result.Length < MaxRenderedLength; index++)
        {
            var character = template[index];
            if (character != '{' || index + 1 >= template.Length)
            {
                result.Append(character);
                continue;
            }

            if (template[index + 1] == '{')
            {
                result.Append('{');
                index++;
                continue;
            }

            var close = template.IndexOf('}', index + 1);
            if (close < 0)
            {
                result.Append(character);
                continue;
            }

            if (valueIndex < values.Length)
            {
                result.Append(values[valueIndex++]);
            }

            index = close;
        }

        if (result.Length > MaxRenderedLength)
        {
            result.Length = MaxRenderedLength;
        }

        return result.ToString();
    }

    private static string SourceComponent(object? source) =>
        source?.GetType().FullName is { Length: > 0 } name
            ? Truncate(name, 128)
            : "framework";

    private static string[] NormalizeValues(object?[] values)
    {
        var count = Math.Min(values.Length, MaxPropertyCount);
        if (count == 0)
        {
            return [];
        }

        var normalized = new string[count];
        for (var index = 0; index < count; index++)
        {
            normalized[index] = NormalizeValue(values[index]);
        }

        return normalized;
    }

    private static string NormalizeValue(object? value) =>
        value switch
        {
            null => "null",
            string text => Truncate(text, MaxPropertyLength),
            bool typed => typed ? "true" : "false",
            char typed => typed.ToString(),
            byte typed => typed.ToString(CultureInfo.InvariantCulture),
            sbyte typed => typed.ToString(CultureInfo.InvariantCulture),
            short typed => typed.ToString(CultureInfo.InvariantCulture),
            ushort typed => typed.ToString(CultureInfo.InvariantCulture),
            int typed => typed.ToString(CultureInfo.InvariantCulture),
            uint typed => typed.ToString(CultureInfo.InvariantCulture),
            long typed => typed.ToString(CultureInfo.InvariantCulture),
            ulong typed => typed.ToString(CultureInfo.InvariantCulture),
            float typed => typed.ToString(CultureInfo.InvariantCulture),
            double typed => typed.ToString(CultureInfo.InvariantCulture),
            decimal typed => typed.ToString(CultureInfo.InvariantCulture),
            IntPtr typed => typed.ToString(),
            UIntPtr typed => typed.ToString(),
            DateTime typed => typed.ToString("O", CultureInfo.InvariantCulture),
            DateTimeOffset typed => typed.ToString("O", CultureInfo.InvariantCulture),
            TimeSpan typed => typed.ToString("c", CultureInfo.InvariantCulture),
            Guid typed => typed.ToString("D"),
            Enum typed => Truncate(typed.ToString(), MaxPropertyLength),
            _ => TypeMarker(value),
        };

    private static string TypeMarker(object value)
    {
        var type = value.GetType();
        var name = Truncate(
            type.FullName ?? type.Name,
            MaxPropertyLength - 2);
        return $"<{name}>";
    }

    private static string Truncate(string value, int maximumLength) =>
        value.Length <= maximumLength
            ? value
            : value[..maximumLength];

    private static StudioLogLevel MapLevel(LogEventLevel level) =>
        level switch
        {
            LogEventLevel.Verbose => StudioLogLevel.Verbose,
            LogEventLevel.Debug => StudioLogLevel.Debug,
            LogEventLevel.Information => StudioLogLevel.Information,
            LogEventLevel.Warning => StudioLogLevel.Warning,
            LogEventLevel.Error => StudioLogLevel.Error,
            LogEventLevel.Fatal => StudioLogLevel.Fatal,
            _ => StudioLogLevel.Information,
        };
}
