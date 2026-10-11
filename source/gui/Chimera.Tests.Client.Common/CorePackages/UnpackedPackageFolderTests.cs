using System.IO;
using System.IO.Compression;
using System.Linq;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common.CorePackages
{
	/// <summary>
	/// The folder a core package is unpacked into has a short name. On Windows a
	/// path of 260 characters or more is refused, and a package with a long
	/// licence file name would not load when the data folder's path was long
	/// (found 2026-10-10 with the xemu package).
	/// </summary>
	[TestClass]
	public class UnpackedPackageFolderTests
	{
		private const string Commit = "42c56e8d13b90b6f2ebfb61f2e84331689f25e64";
		private const string Sha1 = "A2F607C9C485819EEC6221F65656BC5E3839148B";
		private const string OtherSha1 = "4C7EF56952FBA72BCFBFE479F2E0C99AD0F026F8";

		[TestMethod]
		public void TheFoldersNameLeavesTheCommitOut()
		{
			Assert.AreEqual("xemu", CorePackageLoader.ShortPackageName("xemu-" + Commit));
			Assert.AreEqual("srb2-x64", CorePackageLoader.ShortPackageName("srb2-" + Commit + "-x64"));
			Assert.AreEqual("my-core", CorePackageLoader.ShortPackageName("my-core"));
			// a name that is nothing but a commit keeps it: an empty name is no name
			Assert.AreEqual(Commit, CorePackageLoader.ShortPackageName(Commit));
			Assert.AreEqual(Path.Combine("root", "xemu-" + Sha1), CorePackageLoader.UnpackedDirFor("root", "xemu-" + Commit, Sha1));
		}

		private static string MakePackage(string dir, string fileName, string content)
		{
			var path = Path.Combine(dir, fileName);
			using var zip = ZipFile.Open(path, ZipArchiveMode.Create);
			using (var w = new StreamWriter(zip.CreateEntry("waterbox.config").Open())) w.Write(content);
			// the longest licence file name of the xemu package, 76 characters
			using (var w = new StreamWriter(zip.CreateEntry("licenses/musl-powf-pow-waterbox-det-pow.c-from-ARM-optimized-routines-det-pow.c").Open())) w.Write("x");
			return path;
		}

		[TestMethod]
		public void APackageIsUnpackedIntoTheShortFolderAndOldOnesOfTheSameFileGo()
		{
			var root = Path.Combine(Path.GetTempPath(), "chimera-unpack-" + Path.GetRandomFileName());
			var cache = Path.Combine(root, "UnpackedCores");
			Directory.CreateDirectory(cache);
			try
			{
				var name = "xemu-" + Commit;
				var package = MakePackage(root, name + ".chimeraCore", "{}");

				// what earlier builds and other packages left in the cache
				var oldLong = Path.Combine(cache, $"{name}-{OtherSha1}");
				var otherBuild = Path.Combine(cache, "xemu-" + OtherSha1);
				var otherCore = Path.Combine(cache, "xemux-" + OtherSha1);
				foreach (var dir in new[] { oldLong, otherBuild, otherCore }) Directory.CreateDirectory(dir);
				File.WriteAllText(Path.Combine(otherBuild, CorePackageLoader.UnpackedFromFileName), "xemu-0000000000000000000000000000000000000000");

				var unpacked = CorePackageLoader.ExtractZipToCache(package, Sha1, cache);
				Assert.AreEqual(Path.Combine(cache, "xemu-" + Sha1), unpacked);
				Assert.IsTrue(File.Exists(Path.Combine(unpacked, "waterbox.config")));
				Assert.IsTrue(File.Exists(Path.Combine(unpacked, "licenses", "musl-powf-pow-waterbox-det-pow.c-from-ARM-optimized-routines-det-pow.c")));
				Assert.AreEqual(name, File.ReadAllText(Path.Combine(unpacked, CorePackageLoader.UnpackedFromFileName)));
				Assert.IsFalse(Directory.Exists(oldLong), "the folder with the old long name of this file should be gone");
				Assert.IsTrue(Directory.Exists(otherBuild), "another build of the core is another file and keeps its folder");
				Assert.IsTrue(Directory.Exists(otherCore), "another core keeps its folder");
				Assert.AreEqual(0, Directory.GetDirectories(cache, "tmp-*").Length, "no folder is left from the unpacking");

				// the same file again: the folder that is there is used
				Assert.AreEqual(unpacked, CorePackageLoader.ExtractZipToCache(package, Sha1, cache));

				// the same file name with other contents: the new folder replaces the one that came from it
				var newer = CorePackageLoader.ExtractZipToCache(package, OtherSha1.Replace('4', '5'), cache);
				Assert.AreNotEqual(unpacked, newer);
				Assert.IsFalse(Directory.Exists(unpacked), "what the same file unpacked to before should be gone");
				Assert.IsTrue(Directory.Exists(otherBuild));
			}
			finally
			{
				Directory.Delete(root, recursive: true);
			}
		}

		[TestMethod]
		public void NothingOnTheWayIsLongerThanTheFolderItEndsIn()
		{
			// the folder while unpacking (tmp- and twelve characters) against the shortest final name (one letter, a dash, forty)
			Assert.IsTrue(("tmp-" + Path.GetRandomFileName()).Length <= "x-".Length + 40);
			// and the saving for a published package: the commit and its dash
			var before = ("xemu-" + Commit + "-" + Sha1).Length;
			var after = Path.GetFileName(CorePackageLoader.UnpackedDirFor("root", "xemu-" + Commit, Sha1)).Length;
			Assert.AreEqual(41, before - after);
		}
	}
}
