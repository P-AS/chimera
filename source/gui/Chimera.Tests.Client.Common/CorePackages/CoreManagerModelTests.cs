using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common.CorePackages
{
	/// <summary>
	/// What File &gt; Core Manager tells somebody: the core packages that are in
	/// the cores folder, and nothing else - Chimera downloads nothing and knows of
	/// no core it has not been given (user-decided, 2026-10-07). Tested without a
	/// window, so the window can stay thin.
	/// </summary>
	[TestClass]
	public class CoreManagerModelTests
	{
		private static DiscoveredCorePackage Package(string name, string version, string? path = null, bool game = false, DateTimeOffset? built = null, params string[] systems)
			=> new()
			{
				Name = name,
				Version = version,
				Path = path ?? $"/cores/{name}-{version}.chimeraCore",
				Sha1 = new string('a', 40),
				IsGameCore = game,
				VersionDate = built,
				Systems = systems.ToList(),
			};

		[TestMethod]
		public void TheListIsWhatIsInTheFolderAndNothingElse()
		{
			Assert.AreEqual(0, CoreManagerModel.Build([ ]).Count, "no packages, no rows: there is no list of cores that exist somewhere else");
			var rows = CoreManagerModel.Build([ Package("Genesis Plus GX", "aaaaaaaa", systems: "GEN") ]);
			Assert.AreEqual(1, rows.Count);
			Assert.IsTrue(rows[0].IsInstalled);
			Assert.AreEqual("Genesis Plus GX", rows[0].Name);
			CollectionAssert.AreEqual(new[] { "GEN" }, rows[0].Systems.ToList());
		}

		[TestMethod]
		public void VersionsOfOneCoreAreOneRowNewestFirst()
		{
			var older = new DateTimeOffset(2026, 9, 1, 5, 0, 0, TimeSpan.Zero);
			var newer = new DateTimeOffset(2026, 9, 5, 5, 0, 0, TimeSpan.Zero);
			var rows = CoreManagerModel.Build(
			[
				Package("Genesis Plus GX", "aaaaaaaa", built: older),
				Package("genesis plus gx", "bbbbbbbb", built: newer),
				Package("Genesis Plus GX", "cccccccc"),
			]);
			Assert.AreEqual(1, rows.Count, "three versions of one core are one row, whatever the case of the name");
			CollectionAssert.AreEqual(new[] { "bbbbbbbb", "aaaaaaaa", "cccccccc" }, rows[0].Installed.Select(static p => p.Version).ToList(),
				"by the date each package states; one that states none comes after those that do");
			Assert.AreEqual(newer, rows[0].BuiltAt);
		}

		[TestMethod]
		public void EmulatorsComeBeforeGamesAndEachByName()
		{
			var rows = CoreManagerModel.Build(
			[
				Package("Zork", "1", game: true),
				Package("snes9x", "1"),
				Package("SDLPoP", "1", game: true),
				Package("Ares", "1"),
			]);
			CollectionAssert.AreEqual(new[] { "Ares", "snes9x", "SDLPoP", "Zork" }, rows.Select(static r => r.Name).ToList());
			CollectionAssert.AreEqual(new[] { false, false, true, true }, rows.Select(static r => r.IsGameCore).ToList());
		}

		[TestMethod]
		public void APackageThatCouldNotBeReadIsNobodysCore()
		{
			var rows = CoreManagerModel.Build(
			[
				Package("Ares", "1"),
				new DiscoveredCorePackage { Name = "broken", Path = "/cores/broken.chimeraCore", Error = "not a zip" },
			]);
			CollectionAssert.AreEqual(new[] { "Ares" }, rows.Select(static r => r.Name).ToList());
		}

		[TestMethod]
		public void ASystemIsCalledWhatThePackageCallsIt()
		{
			DiscoveredCorePackage named = new()
			{
				Name = "Vita3K",
				Version = "1",
				Path = "/cores/vita3k-1.chimeraCore",
				Systems = [ "PSV", "XYZ9" ],
				SystemNames = new Dictionary<string, string> { ["PSV"] = "PlayStation Vita" },
			};
			var row = CoreManagerModel.Build([ named ])[0];
			Assert.AreEqual("PlayStation Vita", row.SystemNameOf("PSV"));
			Assert.AreEqual("XYZ9", row.SystemNameOf("XYZ9"), "an id nobody named still reads, as the id");
			Assert.AreEqual("PlayStation Vita, XYZ9", row.SystemsSpelled);
		}

		[TestMethod]
		public void TheSizeIsTheFilesOwn()
		{
			var file = Path.Combine(Path.GetTempPath(), $"chimera-size-{Guid.NewGuid():N}.chimeraCore");
			File.WriteAllBytes(file, new byte[3000]);
			try
			{
				Assert.AreEqual(3000, CoreManagerModel.Build([ Package("Ares", "1", path: file) ])[0].SizeBytes);
				Assert.AreEqual(0, CoreManagerModel.Build([ Package("Ares", "1", path: file + ".gone") ])[0].SizeBytes, "a file that cannot be measured has no size, not an error");
			}
			finally
			{
				File.Delete(file);
			}
		}
	}
}
