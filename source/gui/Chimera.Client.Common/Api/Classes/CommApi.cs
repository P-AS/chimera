#nullable enable

namespace Chimera.Client.Common
{
	public sealed class CommApi : ICommApi
	{
		public MemoryMappedFiles MMF { get; }

		public CommApi(IMainFormForApi mainForm) => MMF = mainForm.MemoryMappedFiles;
	}
}
