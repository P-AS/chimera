using System.Collections.Generic;
using System.IO;
using System.Linq;

using Newtonsoft.Json.Linq;

using Chimera.Client.Common;
using Chimera.Emulation.Common;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// The Config > Firmware survey: every installed core's declarations, answered
	/// from the Firmware folder and from what a person chose, and nothing kept
	/// between two surveys - a core that is gone takes its rows with it, while
	/// what was chosen for it waits in the config for its return (issue #30).
	/// </summary>
	[TestClass]
	public class FirmwareSurveyTests
	{
		private string _dir = "";
		private string _firmware = "";

		[TestInitialize]
		public void Setup()
		{
			_dir = Path.Combine(Path.GetTempPath(), "chimera-survey-" + Path.GetRandomFileName());
			_firmware = Path.Combine(_dir, "Firmware");
			Directory.CreateDirectory(_firmware);
		}

		[TestCleanup]
		public void Teardown()
		{
			try { Directory.Delete(_dir, recursive: true); } catch { }
		}

		private string FileOf(string dir, string name, int size, byte seed)
		{
			var bytes = new byte[size];
			for (var i = 0; i < size; i++) bytes[i] = (byte)(seed + i);
			var path = Path.Combine(dir, name);
			File.WriteAllBytes(path, bytes);
			return path;
		}

		private static CoreFirmwareDecl Pinned(string id, string label, int size, string sha1)
			=> new() { Id = id, Display = "Console BIOS", Label = label, Size = size, Sha1 = sha1, Name = $"{label}.bin" };

		private static CoreFirmwareDecl Unpinned(string id, string name, int size)
			=> new() { Id = id, Display = id, Size = size, Name = name };

		private static DiscoveredCorePackage Package(string name)
			=> new() { Name = name, Path = $"/cores/{name}.chimeraCore", Sha1 = new string('A', 40) };

		private static IReadOnlyList<FirmwareSurveyGroup> Survey(
			Config config,
			IReadOnlyList<DiscoveredCorePackage> packages,
			IReadOnlyDictionary<string, (IReadOnlyList<CoreFirmwareDecl>, JArray)> declared,
			string firmwareFolder)
		{
			var index = FirmwareSurvey.BuildIndex(config, firmwareFolder, packages.Select(static p => p.Name));
			return FirmwareSurvey.Build(config, packages,
				p => declared.TryGetValue(p.Name, out var d) ? d : ([ ], new JArray()),
				firmwareFolder, index);
		}

		/// <summary>
		/// The Firmware folder is searched with its sub-folders, as Scan Folder
		/// searches one: firmware sorted into a folder a company and a system was
		/// found by Scan Folder and not when the window opened (issue 359). A file
		/// found below it is said to be in the Firmware folder.
		/// </summary>
		[TestMethod]
		public void TheFirmwareFolderIsSearchedWithItsSubFolders()
		{
			var deep = Path.Combine(_firmware, "Sony", "PlayStation", "SCPH-1001");
			Directory.CreateDirectory(deep);
			var file = FileOf(deep, "scph1001.bin", 512, 7);
			var sha1 = CoreFirmwareStore.Sha1Of(File.ReadAllBytes(file));
			var named = FileOf(Path.Combine(_firmware, "Sony"), "Complex_4627.bin", 64, 3);
			var package = Package("ps1");
			var declared = new Dictionary<string, (IReadOnlyList<CoreFirmwareDecl>, JArray)>
			{
				["ps1"] = (new[] { Pinned("bios", "scph1001", 512, sha1), Unpinned("flash", "complex_4627.bin", 64) }, new JArray()),
			};
			var rows = Survey(new Config(), [ package ], declared, _firmware).Single().Rows;
			var bios = rows.Single(static r => r.Decl.Id == "bios");
			Assert.AreEqual(file, bios.Path, "the file in a sub-folder of the Firmware folder was not found");
			Assert.AreEqual(FirmwareWhere.FirmwareFolder, bios.Where, "a file below the Firmware folder is in it");
			var flash = rows.Single(static r => r.Decl.Id == "flash");
			Assert.AreEqual(named, flash.Path, "a file found by its name below the Firmware folder");
			Assert.AreEqual(FirmwareWhere.FirmwareFolder, flash.Where);
		}

		[TestMethod]
		public void TwoVersionsOfACoreAreOneGroup()
		{
			// Issue #60: a second version of xemu installed beside the first listed
			// its firmware a second time, with nothing to tell the two apart.
			var older = new DiscoveredCorePackage { Name = "xemu", Path = "/cores/xemu-old.chimeraCore", Sha1 = new string('A', 40) };
			var newer = new DiscoveredCorePackage { Name = "xemu", Path = "/cores/xemu-new.chimeraCore", Sha1 = new string('B', 40) };
			var other = Package("ares");
			var bios = Pinned("bios", "retail", 1024, new string('1', 40));
			var eeprom = Unpinned("eeprom", "eeprom.bin", 256);
			var hdd = Unpinned("hdd", "hdd.img", 2048);   /* only the newer version asks for it */
			var declared = new Dictionary<string, (IReadOnlyList<CoreFirmwareDecl>, JArray)>
			{
				[older.Path] = (new[] { bios, eeprom }, new JArray()),
				[newer.Path] = (new[] { bios, eeprom, hdd }, new JArray()),
				[other.Path] = (new[] { Unpinned("psx", "scph.bin", 512) }, new JArray()),
			};
			var packages = new[] { older, other, newer };
			var config = new Config();
			var groups = FirmwareSurvey.Build(config, packages,
				p => declared[p.Path],
				_firmware, FirmwareSurvey.BuildIndex(config, _firmware, packages.Select(static p => p.Name)));

			Assert.AreEqual(2, groups.Count, "one group per core, however many versions are installed");
			var xemu = groups.Single(static g => g.CoreName == "xemu");
			CollectionAssert.AreEqual(new[] { "bios", "eeprom", "hdd" }, xemu.Rows.Select(static r => r.Decl.Id).ToArray(),
				"every declaration any version makes, once each");
		}

		/// <summary>
		/// A firmware with no hash - a console's own dump - is found by the size and
		/// the name hints its core declares when exactly one file fits, and when
		/// several do, the row says how many and nothing is guessed (issue 349: a
		/// folder of melonDS files named NDS_Bios7i.bin, DSi_Nand_USA.bin and so on,
		/// none of them under the name the core declares).
		/// </summary>
		[TestMethod]
		public void AFirmwareWithNoHashIsFoundBySizeAndNameHint()
		{
			FileOf(_firmware, "NDS_Bios7i.bin", 4096, 1);
			FileOf(_firmware, "NDS_Bios9i.bin", 4096, 2);
			FileOf(_firmware, "DSi_Firmware.bin", 2048, 3);
			FileOf(_firmware, "DSi_Nand_USA.bin", 3000, 4);
			FileOf(_firmware, "DSi_Nand_EUR.bin", 3000, 5);
			FileOf(_firmware, "NDS_Firmware_1.bin", 1024, 6);
			FileOf(_firmware, "NDS_Firmware_2.bin", 1024, 7);
			CoreFirmwareDecl Hinted(string id, string name, int size, params string[] hints)
				=> new() { Id = id, Display = id, Size = size, Name = name, NameHints = hints.ToList() };
			var decls = new[]
			{
				Hinted("bios7i.bin", "bios7i.bin", 4096, "7i"),
				Hinted("bios9i.bin", "bios9i.bin", 4096, "9i"),
				Hinted("dsi_firmware.bin", "dsifirmware.bin", 2048, "dsi"),
				Hinted("dsi_nand.bin", "nand.bin", 0, "nand"),
				Hinted("firmware.bin", "firmware.bin", 0, "firmware"),
				// a hash, when there is one, is the only thing that answers
				new CoreFirmwareDecl { Id = "bios7.bin", Display = "bios7", Size = 4096, Name = "bios7.bin", Sha1 = new string('1', 40), NameHints = [ "7i" ] },
			};
			var melonds = Package("melonDS");
			var config = new Config();
			var rows = Survey(config, [ melonds ],
				new Dictionary<string, (IReadOnlyList<CoreFirmwareDecl>, JArray)> { ["melonDS"] = (decls, new JArray()) },
				_firmware).Single().Rows.ToDictionary(static r => r.Decl.Id);

			foreach (var (id, file) in new[] { ("bios7i.bin", "NDS_Bios7i.bin"), ("bios9i.bin", "NDS_Bios9i.bin"), ("dsi_firmware.bin", "DSi_Firmware.bin") })
			{
				Assert.IsTrue(rows[id].OnHand, $"{id}: one file has its size and a name with its hint - {rows[id].StatusText}");
				Assert.AreEqual(file, Path.GetFileName(rows[id].Path), id);
				Assert.AreEqual("found by size and name", rows[id].StatusText, id);
			}
			Assert.IsFalse(rows["dsi_nand.bin"].OnHand, "two NAND images: which console's is the person's to say");
			Assert.AreEqual("2 candidates - choose one with Set...", rows["dsi_nand.bin"].StatusText);
			Assert.IsFalse(rows["firmware.bin"].OnHand, "NDS_Firmware_1, _2 and DSi_Firmware all say firmware");
			Assert.AreEqual("3 candidates - choose one with Set...", rows["firmware.bin"].StatusText);
			Assert.IsFalse(rows["bios7.bin"].OnHand, "a pinned entry is not found by a hint");
			Assert.AreEqual("not found", rows["bios7.bin"].StatusText);
		}

		[TestMethod]
		public void TheGameCoresComeAfterEveryEmulator()
		{
			// docs/game-cores.md: a game core's files are its firmware, listed apart
			var pop = new DiscoveredCorePackage { Name = "Apple", Path = "/cores/pop.chimeraCore", Sha1 = new string('C', 40), IsGameCore = true };
			var packages = new[] { pop, Package("xemu"), Package("ares") };
			var config = new Config();
			var groups = FirmwareSurvey.Build(config, packages, static _ => ([ ], new JArray()),
				_firmware, FirmwareSurvey.BuildIndex(config, _firmware, packages.Select(static p => p.Name)));
			CollectionAssert.AreEqual(new[] { "ares", "xemu", "Apple" }, groups.Select(static g => g.CoreName).ToArray(),
				"a game core sorts after the emulators whatever its name");
			CollectionAssert.AreEqual(new[] { false, false, true }, groups.Select(static g => g.IsGameCore).ToArray());
		}

		[TestMethod]
		public void TheFolderAnswersByHashAndByName()
		{
			var dump = FileOf(_firmware, "whatever-it-was-called.bin", 4096, seed: 3);
			var sha1 = CoreFirmwareStore.Sha1Of(File.ReadAllBytes(dump));
			FileOf(_firmware, "mcpx_1.0.bin", 512, seed: 9);

			var packages = new[] { Package("PCSX2"), Package("xemu") };
			var declared = new Dictionary<string, (IReadOnlyList<CoreFirmwareDecl>, JArray)>
			{
				["PCSX2"] = ([ Pinned("bios.bin", "USA v2.30", 4096, sha1), Pinned("bios.bin", "Japan v1.00", 4096, new string('B', 40)) ], new JArray()),
				["xemu"] = ([ Unpinned("mcpx", "mcpx_1.0.bin", 512), Unpinned("hdd", "xbox_hdd.qcow2", 0) ], new JArray()),
			};
			var groups = Survey(new Config(), packages, declared, _firmware);

			Assert.AreEqual(2, groups.Count);
			var ps2 = groups.Single(static g => g.CoreName == "PCSX2");
			Assert.AreEqual(CoreFirmwareState.Good, ps2.Rows[0].State, "the dump is found by its hash under any name");
			Assert.AreEqual(FirmwareWhere.FirmwareFolder, ps2.Rows[0].Where);
			Assert.AreEqual(CoreFirmwareState.Missing, ps2.Rows[1].State);
			StringAssert.Contains(ps2.Summary, "1 of 2 dumps on hand");
			Assert.IsTrue(ps2.Complete, "one dump of the one id is enough");

			var xbox = groups.Single(static g => g.CoreName == "xemu");
			Assert.AreEqual(CoreFirmwareState.Good, xbox.Rows[0].State, "an unpinned file is found by the name the core declares");
			Assert.AreEqual("found by name", xbox.Rows[0].StatusText);
			Assert.AreEqual(CoreFirmwareState.Missing, xbox.Rows[1].State);
			Assert.IsFalse(xbox.Complete);
			StringAssert.Contains(xbox.Summary, "not pinned by hash");
		}

		[TestMethod]
		public void AChosenFileIsRememberedUnderItsOwnDump()
		{
			var elsewhere = Path.Combine(_dir, "elsewhere");
			Directory.CreateDirectory(elsewhere);
			var usa = FileOf(elsewhere, "usa.bin", 4096, seed: 1);
			var japan = FileOf(elsewhere, "japan.bin", 4096, seed: 2);
			var usaSha = CoreFirmwareStore.Sha1Of(File.ReadAllBytes(usa));
			var japanSha = CoreFirmwareStore.Sha1Of(File.ReadAllBytes(japan));
			var declUsa = Pinned("bios.bin", "USA", 4096, usaSha);
			var declJapan = Pinned("bios.bin", "Japan", 4096, japanSha);

			Config config = new();
			CoreFirmwareStore.Remember(config, "PCSX2", declUsa, usa);
			CoreFirmwareStore.Remember(config, "PCSX2", declJapan, japan);
			Assert.AreEqual(2, config.CoreFirmware.Count, "two dumps of one id are two memories");
			Assert.AreEqual(usa, CoreFirmwareStore.GetPath(config, "PCSX2", declUsa));
			Assert.AreEqual(japan, CoreFirmwareStore.GetPath(config, "PCSX2", declJapan));
			CollectionAssert.AreEquivalent(new[] { usa, japan }, CoreFirmwareStore.RememberedPaths(config, "PCSX2").ToArray());

			var packages = new[] { Package("PCSX2") };
			var declared = new Dictionary<string, (IReadOnlyList<CoreFirmwareDecl>, JArray)> { ["PCSX2"] = ([ declUsa, declJapan ], new JArray()) };
			var groups = Survey(config, packages, declared, _firmware);
			Assert.IsTrue(groups[0].Rows.All(static r => r.State is CoreFirmwareState.Good));
			Assert.IsTrue(groups[0].Rows.All(static r => r.Where is FirmwareWhere.Chosen), "found where they were chosen, not in the folder");

			// the in-use key for the id points at the USA dump; the Japan
			// declaration is still answered by its own memory
			CoreFirmwareStore.SetPath(config, "PCSX2", "bios.bin", usa);
			Assert.AreEqual(japan, CoreFirmwareStore.GetPath(config, "PCSX2", declJapan));
			Assert.AreEqual(CoreFirmwareState.Good, CoreFirmwareStore.Describe(config, "PCSX2", declJapan).State);
		}

		[TestMethod]
		public void AChosenFileThatIsGoneSaysSo()
		{
			var gone = Path.Combine(_dir, "gone.bin");
			var decl = Unpinned("eeprom", "eeprom.bin", 256);
			Config config = new();
			CoreFirmwareStore.Remember(config, "xemu", decl, gone);
			var groups = Survey(config, new[] { Package("xemu") },
				new Dictionary<string, (IReadOnlyList<CoreFirmwareDecl>, JArray)> { ["xemu"] = ([ decl ], new JArray()) }, _firmware);
			Assert.AreEqual(CoreFirmwareState.Unreadable, groups[0].Rows[0].State);
			Assert.AreEqual("chosen file is gone", groups[0].Rows[0].StatusText);
		}

		[TestMethod]
		public void ARemovedCoreTakesItsRowsButNotItsMemory()
		{
			var dump = FileOf(_firmware, "disksys.rom", 8192, seed: 5);
			var decl = Pinned("bios", "Nintendo FDS", 8192, CoreFirmwareStore.Sha1Of(File.ReadAllBytes(dump)));
			Config config = new();
			CoreFirmwareStore.Remember(config, "QuickerNesHawk", decl, dump);
			CoreFirmwareStore.SetPath(config, "QuickerNesHawk", "bios", dump);
			CoreFirmwareStore.SetPath(config, "PCSX2", "bios.bin", dump);
			var declared = new Dictionary<string, (IReadOnlyList<CoreFirmwareDecl>, JArray)> { ["QuickerNesHawk"] = ([ decl ], new JArray()) };

			var both = Survey(config, new[] { Package("QuickerNesHawk"), Package("PCSX2") }, declared, _firmware);
			Assert.AreEqual(2, both.Count);
			Assert.AreEqual(3, config.CoreFirmware.Count, "every key belongs to an installed core");

			// the NES core is deleted from the Cores folder
			var after = Survey(config, new[] { Package("PCSX2") }, declared, _firmware);
			Assert.AreEqual(1, after.Count, "its rows are gone");
			Assert.AreEqual("PCSX2", after[0].CoreName);
			Assert.AreEqual(3, config.CoreFirmware.Count, "what was chosen for it is kept, for the day it is put back");

			// and put back
			var back = Survey(config, new[] { Package("QuickerNesHawk"), Package("PCSX2") }, declared, _firmware);
			Assert.AreEqual(CoreFirmwareState.Good, back.Single(static g => g.CoreName == "QuickerNesHawk").Rows[0].State,
				"its dump is found again without anyone pointing at it");
		}

		[TestMethod]
		public void APackageThatCannotBeReadIsLeftOut()
		{
			var broken = new DiscoveredCorePackage { Name = "broken", Path = "/cores/broken.chimeraCore", Error = "not a zip" };
			var groups = Survey(new Config(), new[] { broken, Package("PCSX2") },
				new Dictionary<string, (IReadOnlyList<CoreFirmwareDecl>, JArray)>(), _firmware);
			Assert.AreEqual(1, groups.Count);
			Assert.AreEqual("needs no firmware", groups[0].Summary);
		}

		[TestMethod]
		public void ConditionsAreSaidInWords()
		{
			Assert.AreEqual("bios = ps2-0220e-20060210", FirmwareSurvey.ConditionText(JToken.Parse("""{"setting":"bios","is":"ps2-0220e-20060210"}""")));
			Assert.AreEqual("a file in cd", FirmwareSurvey.ConditionText(JToken.Parse("""{"slot":"cd"}""")));
			Assert.AreEqual("a .cue file in cd", FirmwareSurvey.ConditionText(JToken.Parse("""{"slot":"cd","extension":"cue"}""")));
			Assert.AreEqual("region in us, jp", FirmwareSurvey.ConditionText(JToken.Parse("""{"setting":"region","in":["us","jp"]}""")));
			Assert.AreEqual(
				"systemHardware = genesis and (a file in cd and (region = autodetect or region = usa))",
				FirmwareSurvey.ConditionText(JToken.Parse("""{"all":[{"setting":"systemHardware","is":"genesis"},{"all":[{"slot":"cd"},{"any":[{"setting":"region","is":"autodetect"},{"setting":"region","is":"usa"}]}]}]}""")));
			Assert.AreEqual("not machine = fds", FirmwareSurvey.ConditionText(JToken.Parse("""{"not":{"setting":"machine","is":"fds"}}""")));
		}

		[TestMethod]
		public void ConditionsRideAlongByIndex()
		{
			var raw = JArray.Parse("""[{"id":"bios","requiredWhen":{"setting":"bios","is":"real"}},{"id":"flash"}]""");
			var declared = new Dictionary<string, (IReadOnlyList<CoreFirmwareDecl>, JArray)>
			{
				["Flycast"] = ([ Unpinned("bios", "dc_boot.bin", 0), Unpinned("flash", "dc_flash.bin", 0) ], raw),
			};
			var groups = Survey(new Config(), new[] { Package("Flycast") }, declared, _firmware);
			Assert.AreEqual("bios = real", groups[0].Rows[0].Condition);
			Assert.AreEqual("", groups[0].Rows[1].Condition);
		}
	}
}
