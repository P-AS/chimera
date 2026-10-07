using System;
using System.IO;
using System.Linq;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common.CorePackages
{
	/// <summary>
	/// Where core packages are looked for (user-decided, 2026-10-07): one folder,
	/// <c>Cores/</c> beside the executable unless the user names another. Chimera
	/// downloads nothing, so there is no second place it fills.
	/// </summary>
	[TestClass]
	public class CoresFolderTests
	{
		private static string NewDir()
		{
			var dir = Path.Combine(Path.GetTempPath(), "chimera-coresfolder-" + Path.GetRandomFileName());
			Directory.CreateDirectory(dir);
			return dir;
		}

		[TestMethod]
		public void ItIsCoresBesideTheExecutableUntilSomebodySaysOtherwise()
		{
			Config config = new();
			Assert.AreEqual("", config.CoresFolder);
			Assert.AreEqual(CoresFolder.Default, CoresFolder.For(config));
			Assert.AreEqual("Cores", Path.GetFileName(CoresFolder.Default));
			CollectionAssert.AreEqual(new[] { CoresFolder.Default }, CorePackageDiscovery.SearchPaths(config).ToList(),
				"and it is the only folder searched: not the data directory's, where versions that downloaded cores kept them");
		}

		[TestMethod]
		public void AFolderTheUserNamesReplacesIt()
		{
			var mine = NewDir();
			try
			{
				Config config = new() { CoresFolder = mine };
				Assert.AreEqual(Path.GetFullPath(mine), CoresFolder.For(config));
				CollectionAssert.AreEqual(new[] { Path.GetFullPath(mine) }, CorePackageDiscovery.SearchPaths(config).ToList(), "instead of the default, not as well as");

				config.CorePackagePaths.Add("/somewhere/else");
				CollectionAssert.AreEqual(new[] { Path.GetFullPath(mine), "/somewhere/else" }, CorePackageDiscovery.SearchPaths(config).ToList(), "further search directories still only add");
			}
			finally
			{
				Directory.Delete(mine, recursive: true);
			}
		}

		[TestMethod]
		public void ARelativeFolderIsBesideTheExecutableAndABadOneIsTheDefault()
		{
			Assert.AreEqual(Path.GetFullPath(Path.Combine(Path.GetDirectoryName(CoresFolder.Default)!, "..", "Shared")), CoresFolder.Resolve(Path.Combine("..", "Shared")));
			Assert.AreEqual(CoresFolder.Default, CoresFolder.Resolve("   "));
			Assert.AreEqual(CoresFolder.Default, CoresFolder.Resolve("bad\0path"), "a setting that is no path must not leave somebody with no cores folder");
		}

		[TestMethod]
		public void ChoosingTheDefaultKeepsNothing()
		{
			Assert.AreEqual("", CoresFolder.ToConfigure(CoresFolder.Default), "so a bundle that is moved keeps finding the Cores/ that moved with it");
			Assert.AreEqual("", CoresFolder.ToConfigure(CoresFolder.Default + Path.DirectorySeparatorChar));
			var mine = NewDir();
			try
			{
				Assert.AreEqual(mine, CoresFolder.ToConfigure(mine));
			}
			finally
			{
				Directory.Delete(mine, recursive: true);
			}
		}

		[TestMethod]
		public void OnlyAFileInTheFolderItselfIsTheManagersToRemove()
		{
			var folder = NewDir();
			try
			{
				Assert.IsTrue(CoresFolder.Holds(folder, Path.Combine(folder, "ares-1.chimeraCore")));
				Assert.IsTrue(CoresFolder.Holds(folder + Path.DirectorySeparatorChar, Path.Combine(folder, "ares-1.chimeraCore")));
				Assert.IsFalse(CoresFolder.Holds(folder, Path.Combine(folder, "deeper", "ares-1.chimeraCore")));
				Assert.IsFalse(CoresFolder.Holds(folder, Path.Combine(Path.GetTempPath(), "ares-1.chimeraCore")), "a package found in a further search directory is somebody's own arrangement");
			}
			finally
			{
				Directory.Delete(folder, recursive: true);
			}
		}

		[TestMethod]
		[DoNotParallelize] // the environment is the process's, not the test's
		public void PackagesLeftWhereEarlierVersionsDownloadedThemAreCounted()
		{
			var home = NewDir();
			var before = Environment.GetEnvironmentVariable("CHIMERA_DATA_HOME");
			Environment.SetEnvironmentVariable("CHIMERA_DATA_HOME", home);
			try
			{
				var former = CoresFolder.Former;
				Assert.AreEqual(Path.Combine(home, "Cores"), former);
				Assert.AreEqual(0, CoresFolder.LeftInFormer(CoresFolder.Default), "no such folder, nothing left in it");
				Directory.CreateDirectory(former);
				File.WriteAllText(Path.Combine(former, "ares-1.chimeraCore"), "x");
				File.WriteAllText(Path.Combine(former, "gpgx-2.chimeraCore"), "x");
				File.WriteAllText(Path.Combine(former, "notes.txt"), "x");
				Assert.AreEqual(2, CoresFolder.LeftInFormer(CoresFolder.Default));
				Assert.AreEqual(0, CoresFolder.LeftInFormer(former), "once that folder IS the cores folder there is nothing to say");
			}
			finally
			{
				Environment.SetEnvironmentVariable("CHIMERA_DATA_HOME", before);
				Directory.Delete(home, recursive: true);
			}
		}
	}
}
