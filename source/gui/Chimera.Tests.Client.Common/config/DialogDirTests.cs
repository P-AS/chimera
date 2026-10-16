using System.IO;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// Where a file dialog opens (chimera#189): a folder from Config > Paths when it is
	/// there, and never a folder that is not - the dialog helper refuses one in a DEBUG
	/// build, which took Export Save Data down, and Open ROM was passing the same folder.
	/// </summary>
	[TestClass]
	public class DialogDirTests
	{
		private static string NewDir()
		{
			var dir = Path.Combine(Path.GetTempPath(), "chimera-dialogdir-" + Path.GetRandomFileName());
			Directory.CreateDirectory(dir);
			return dir;
		}

		private static PathEntryCollection PathsWithRomDir(string system, string romDir)
		{
			PathEntryCollection paths = new();
			paths.Paths.Add(new(system, "Base", romDir));
			paths.Paths.Add(new(system, "ROM", romDir));
			return paths;
		}

		[TestMethod]
		public void TheFirstFolderThatIsThereIsWhereADialogOpens()
		{
			var there = NewDir();
			var alsoThere = NewDir();
			var missing = Path.Combine(there, "not made");
			try
			{
				Assert.AreEqual(there, PathEntryExtensions.FirstExistingDir(there));
				Assert.AreEqual(there, PathEntryExtensions.FirstExistingDir(missing, null, "", "   ", there, alsoThere), "what is missing, null or blank is passed over");
				Assert.AreEqual("", PathEntryExtensions.FirstExistingDir(missing), "and nothing there leaves the dialog to its own default");
				Assert.AreEqual("", PathEntryExtensions.FirstExistingDir());
				var file = Path.Combine(there, "a file");
				File.WriteAllText(file, "x");
				Assert.AreEqual("", PathEntryExtensions.FirstExistingDir(file), "a file is not a folder to open in");
			}
			finally
			{
				Directory.Delete(there, recursive: true);
				Directory.Delete(alsoThere, recursive: true);
			}
		}

		[TestMethod]
		public void ASystemsRomFolderThenTheLastRomsThenTheDialogsOwn()
		{
			var romDir = NewDir();
			var lastDir = NewDir();
			var missing = Path.Combine(romDir, "not made");
			try
			{
				var configured = PathsWithRomDir("TestSys", romDir);
				configured.LastRomPath = lastDir;
				Assert.AreEqual(romDir, configured.RomDialogDir("TestSys"), "the system's own folder, when it is there");

				var unmade = PathsWithRomDir("TestSys", missing);
				Assert.AreEqual(missing, unmade.RomAbsolutePath("TestSys"), "the folder Config > Paths names is not there");
				Assert.AreEqual("", unmade.RomDialogDir("TestSys"), "no ROM opened yet: \".\" is the unset value, not the working directory");
				unmade.LastRomPath = lastDir;
				Assert.AreEqual(lastDir, unmade.RomDialogDir("TestSys"), "else where the last ROM came from");
				unmade.LastRomPath = Path.Combine(lastDir, "gone");
				Assert.AreEqual("", unmade.RomDialogDir("TestSys"), "else the dialog's own default");

				PathEntryCollection recent = PathsWithRomDir("TestSys", romDir);
				recent.UseRecentForRoms = true;
				recent.LastRomPath = lastDir;
				Assert.AreEqual(lastDir, recent.RomDialogDir("TestSys"), "'use recent' asks for the last ROM's folder by name");
			}
			finally
			{
				Directory.Delete(romDir, recursive: true);
				Directory.Delete(lastDir, recursive: true);
			}
		}

		/// <summary>
		/// A scan that found firmware names the Firmware folder of Config &gt; Paths, but only while the
		/// folder named there is not there (issue #293): Scan Folder then opens where the files are, and
		/// a folder the person made and named is never taken from them.
		/// </summary>
		[TestMethod]
		public void AScannedFolderBecomesTheFirmwareFolderOnlyWhileThatIsNotThere()
		{
			var scanned = NewDir();
			var made = NewDir();
			var missing = Path.Combine(scanned, "not made");
			try
			{
				// a new collection carries every default entry, Firmware among them
				PathEntryCollection unmade = new();
				unmade[PathEntryCollection.GLOBAL, "Firmware"].Path = missing;
				Assert.IsFalse(unmade.AdoptFirmwareFolder(Path.Combine(scanned, "gone")), "a folder that is not there is not adopted");
				Assert.AreEqual(missing, unmade.FirmwareAbsolutePath());
				Assert.IsTrue(unmade.AdoptFirmwareFolder(scanned), "the named folder is not there, so the scanned one takes its place");
				Assert.AreEqual(scanned, unmade.FirmwareAbsolutePath());
				Assert.IsFalse(unmade.AdoptFirmwareFolder(made), "and once it is there, a later scan elsewhere leaves it");
				Assert.AreEqual(scanned, unmade.FirmwareAbsolutePath());

				PathEntryCollection named = new();
				named[PathEntryCollection.GLOBAL, "Firmware"].Path = made;
				Assert.IsFalse(named.AdoptFirmwareFolder(scanned), "a folder the person made is theirs");
				Assert.AreEqual(made, named.FirmwareAbsolutePath());
			}
			finally
			{
				Directory.Delete(scanned, recursive: true);
				Directory.Delete(made, recursive: true);
			}
		}
	}
}
