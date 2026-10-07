using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;

using NLua;

namespace Chimera.Client.Common
{
	[Description("A library for exchanging data with other programs on this machine through memory-mapped files. Chimera has no network functions: nothing in it opens a socket or makes a request")]
	public sealed class CommLuaLibrary : LuaLibraryBase
	{
		public CommLuaLibrary(ILuaLibraries luaLibsImpl, ApiContainer apiContainer, Action<string> logOutputCallback)
			: base(luaLibsImpl, apiContainer, logOutputCallback) {}

		public override string Name => "comm";

		//TO DO: not fully working yet!
		[LuaMethod("getluafunctionslist", "returns a list of implemented functions")]
		public static string GetLuaFunctionsList()
		{
			var list = new StringBuilder();
			foreach (var function in typeof(CommLuaLibrary).GetMethods())
			{
				list.AppendLine(function.ToString());
			}
			return list.ToString();
		}

		// All MemoryMappedFile related methods
		[LuaMethod("mmfSetFilename", "Sets the filename for the screenshots")]
		public void MmfSetFilename(string filename)
			=> APIs.Comm.MMF.Filename = filename;

		[LuaMethod("mmfGetFilename", "Gets the filename for the screenshots")]
		public string MmfGetFilename()
			=> APIs.Comm.MMF.Filename;

		[LuaMethod("mmfScreenshot", "Saves screenshot to memory mapped file")]
		public int MmfScreenshot()
			=> APIs.Comm.MMF.ScreenShotToFile();

		[LuaMethod("mmfWrite", "Writes a string to a memory mapped file")]
		public int MmfWrite(string mmf_filename, string outputString)
			=> APIs.Comm.MMF.WriteToFile(mmf_filename, outputString);

		[LuaMethod("mmfWriteBytes", "Write bytes to a memory mapped file")]
		public int MmfWriteBytes(string mmf_filename, LuaTable byteArray)
			=> APIs.Comm.MMF.WriteToFile(mmf_filename, _th.EnumerateValues<long>(byteArray).Select(l => (byte) l).ToArray());

		[LuaMethod("mmfCopyFromMemory", "Copy a section of the memory to a memory mapped file")]
		public int MmfCopyFromMemory(
			string mmf_filename,
			long addr,
			int length,
			string domain)
				=> APIs.Comm.MMF.WriteToFile(mmf_filename, APIs.Memory.ReadByteRange(addr, length, domain).ToArray());

		[LuaMethod("mmfCopyToMemory", "Copy a memory mapped file to a section of the memory")]
		public void MmfCopyToMemory(
			string mmf_filename,
			long addr,
			int length,
			string domain)
				=> APIs.Memory.WriteByteRange(addr, APIs.Comm.MMF.ReadBytesFromFile(mmf_filename, length), domain);

		[LuaMethod("mmfRead", "Reads a string from a memory mapped file")]
		public string MmfRead(string mmf_filename, int expectedSize)
			=> APIs.Comm.MMF.ReadFromFile(mmf_filename, expectedSize);

		[LuaMethod("mmfReadBytes", "Reads bytes from a memory mapped file")]
		[return: LuaZeroIndexed]
		public LuaTable MmfReadBytes(string mmf_filename, int expectedSize)
			=> _th.ListToTable(APIs.Comm.MMF.ReadBytesFromFile(mmf_filename, expectedSize), indexFrom: 0);
	}
}