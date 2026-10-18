using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

using Chimera.Common;
using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Emulation.Common
{
	/// <summary>
	/// A machine's memory is offered to the tools only when it has some (issue
	/// #197). RAM Watch, RAM Search and the hex editor require the memory
	/// service and start from its first domain; a Flash movie describes none, the
	/// service was registered with an empty list, the tools were enabled, and RAM
	/// Search threw "Index was out of range" on opening.
	/// </summary>
	[TestClass]
	public class WaterboxMemoryTests
	{
		[TestMethod]
		public void AMachineWithNoMemoryOffersNoMemoryService()
		{
			BasicServiceProvider services = new(new NullEmulator());
			WaterboxCore.PublishMemory(services, new List<MemoryDomain>());
			Assert.IsFalse(services.HasService<IMemoryDomains>(),
				"an empty list enables the memory tools, which then fail on opening");
		}

		[TestMethod]
		public void AMachineWithMemoryOffersIt()
		{
			BasicServiceProvider services = new(new NullEmulator());
			List<MemoryDomain> domains = [ new MemoryDomainByteArray("RAM", MemoryDomain.Endian.Little, new byte[16], writable: true, wordSize: 1) ];
			WaterboxCore.PublishMemory(services, domains);
			Assert.IsTrue(services.HasService<IMemoryDomains>());
			Assert.AreEqual("RAM", services.GetService<IMemoryDomains>().MainMemory.Name);
		}

		/// <summary>
		/// A pointer domain reads through its read view and writes through the
		/// core's own pointer. The engine's read view never runs the core's code: a
		/// core that protects pages to watch them (RPCS3's texture cache) heard of
		/// every read the hex editor or RAM Search made, and its handler could
		/// change the machine (engine analysis, 2026-10-18). With no read view,
		/// both go through the one pointer, as before.
		/// </summary>
		[TestMethod]
		public void APointerDomainReadsThroughItsReadViewAndWritesThroughTheCoresPointer()
		{
			IntPtr core = Marshal.AllocHGlobal(16), view = Marshal.AllocHGlobal(16);
			try
			{
				for (int i = 0; i < 16; i++)
				{
					Marshal.WriteByte(core, i, 0x11);
					Marshal.WriteByte(view, i, 0x22);
				}
				MemoryDomainIntPtr domain = new("RAM", MemoryDomain.Endian.Little, core, 16, writable: true, wordSize: 1);
				Assert.AreEqual(0x11, domain.PeekByte(3), "no read view: reads go through the core's pointer");
				domain.ReadData = view;
				Assert.AreEqual(0x22, domain.PeekByte(3));
				byte[] bulk = new byte[4];
				domain.BulkPeekByte(4L.RangeToExclusive(8L), bulk);
				CollectionAssert.AreEqual(new byte[] { 0x22, 0x22, 0x22, 0x22 }, bulk);
				domain.PokeByte(5, 0x7F);
				Assert.AreEqual(0x7F, Marshal.ReadByte(core, 5), "a write goes to the core's pointer");
				Assert.AreEqual(0x22, Marshal.ReadByte(view, 5), "and not to the read view");
			}
			finally
			{
				Marshal.FreeHGlobal(core);
				Marshal.FreeHGlobal(view);
			}
		}
	}
}
