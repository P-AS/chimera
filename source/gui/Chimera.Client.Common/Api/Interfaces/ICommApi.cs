#nullable enable

namespace Chimera.Client.Common
{
	/// <summary>
	/// Exchanging data with other programs on this machine: memory-mapped files,
	/// and nothing else. Chimera has no network functions (user-decided, 2026-10-07).
	/// </summary>
	public interface ICommApi : IExternalApi
	{
		MemoryMappedFiles MMF { get; }
	}
}
