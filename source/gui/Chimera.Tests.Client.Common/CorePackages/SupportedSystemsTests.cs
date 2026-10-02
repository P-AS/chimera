using System.Linq;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common.CorePackages
{
	/// <summary>The systems the cores run, once each, with their cores (#172).</summary>
	[TestClass]
	public class SupportedSystemsTests
	{
		private static readonly (string, System.Collections.Generic.IReadOnlyList<string>, bool)[] Cores =
		[
			("ares", [ "PS1", "GB", "NES" ], false),
			("quickerNES", [ "NES" ], true),
			("Genesis Plus GX", [ "GEN", "SMS" ], true),
			("Dolphin", [ "GC", "Wii" ], false),
		];

		[TestMethod]
		public void EachSystemOnceWithEveryCoreThatRunsIt()
		{
			var systems = SupportedSystems.From(Cores);
			var nes = systems.Single(e => e.Id == "NES");
			Assert.AreEqual("Nintendo Entertainment System", nes.Name);
			CollectionAssert.AreEqual(new[] { "ares", "quickerNES" }, nes.Cores.ToArray());
			Assert.IsTrue(nes.Installed, "one of its cores is installed");
			Assert.IsFalse(systems.Single(e => e.Id == "PS1").Installed, "and none of this one's is");
			Assert.AreEqual(7, systems.Count, "PS1 GB NES GEN SMS GC Wii: NES once");
		}

		[TestMethod]
		public void SortedByTheNameAPersonReads()
		{
			var names = SupportedSystems.From(Cores).Select(static e => e.Name).ToList();
			CollectionAssert.AreEqual(names.OrderBy(static n => n, System.StringComparer.OrdinalIgnoreCase).ToList(), names);
		}

		[TestMethod]
		public void TheFilterReadsNamesIdsAndCores()
		{
			var systems = SupportedSystems.From(Cores);
			string[] Shown(string f) => systems.Where(e => SupportedSystems.Matches(e, f)).Select(static e => e.Id).ToArray();
			CollectionAssert.AreEqual(new[] { "PS1" }, Shown("playstation"), "by name, any case");
			CollectionAssert.AreEquivalent(new[] { "GB", "NES", "PS1" }, Shown("ares"), "by core");
			CollectionAssert.AreEquivalent(new[] { "SMS" }, Shown("SMS"), "by id");
			Assert.AreEqual(systems.Count, Shown("  ").Length, "an empty filter shows everything");
		}
	}
}
