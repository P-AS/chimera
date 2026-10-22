using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Emulation.Common
{
	/// <summary>
	/// A package's firmware entry may say what marks its file in a name
	/// (issue 349), and it reaches the declaration under the key a package
	/// writes: "nameHints". A key spelt one way in packages and read another
	/// way here would find nothing and say nothing.
	/// </summary>
	[TestClass]
	public class WaterboxFirmwareHintsTests
	{
		[TestMethod]
		public void AFirmwareEntrysNameHintsAreRead()
		{
			var cfg = WaterboxConfig.FromJson("""
				{
				  "coreName": "hinted",
				  "systemId": "SYS",
				  "video": { "width": 256, "height": 384 },
				  "input": { "buttons": [] },
				  "firmware": [
				    { "id": "bios7i.bin", "display": "DSi ARM7 BIOS", "size": 65536, "name": "bios7i.bin", "nameHints": [ "7i", "dsi7" ] },
				    { "id": "bios7.bin", "display": "DS ARM7 BIOS", "size": 16384, "name": "bios7.bin", "sha1": "24F67BDEA115A2C847C8813A262502EE1607B7DF" }
				  ]
				}
				""");
			Assert.IsNotNull(cfg, "the package this test declares did not parse");
			Assert.IsNotNull(cfg.Firmware);
			CollectionAssert.AreEqual(new[] { "7i", "dsi7" }, cfg.Firmware[0].NameHints, "the hints as the package lists them");
			Assert.IsNull(cfg.Firmware[1].NameHints, "an entry that lists none has none");
		}
	}
}
