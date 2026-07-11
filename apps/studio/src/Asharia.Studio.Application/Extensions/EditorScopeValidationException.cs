using System;
using System.Collections.Generic;
using System.Linq;

namespace Asharia.Studio.Application.Extensions;

public sealed class EditorScopeValidationException : InvalidOperationException
{
    internal EditorScopeValidationException(IEnumerable<string> diagnostics)
        : base("Editor scope candidate failed structural validation.")
    {
        Diagnostics = Array.AsReadOnly(diagnostics.ToArray());
    }

    public IReadOnlyList<string> Diagnostics { get; }
}
