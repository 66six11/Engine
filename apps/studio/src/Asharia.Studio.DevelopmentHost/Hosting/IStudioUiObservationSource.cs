using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.DevelopmentProtocol;

namespace Asharia.Studio.DevelopmentHost.Hosting;

public interface IStudioUiObservationSource
{
    ValueTask<ObservationProtocolReadResult<UiWindowListResult>> ListWindowsAsync(
        UiListWindowsParameters parameters,
        CancellationToken cancellationToken);

    ValueTask<ObservationProtocolReadResult<UiTreeReadResult>> ReadTreeAsync(
        UiReadTreeParameters parameters,
        CancellationToken cancellationToken);
}
