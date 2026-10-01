using System.Collections.Generic;
using System.IO;
using System.IO.Compression;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The release of a machine's game - Doom II v1.9 against Freedoom - asked
	/// beside the System on page one (<c>versionSetting</c>), its options narrowed
	/// by the chosen machine, and kept off the settings page like the machine.
	/// </summary>
	[TestClass]
	public class WizardVersionTests
	{
		private static string _dir = "";

		[ClassInitialize]
		public static void MakePlayground(TestContext _)
		{
			_dir = Path.Combine(Path.GetTempPath(), $"chimera-version-{System.Diagnostics.Process.GetCurrentProcess().Id}");
			Directory.CreateDirectory(_dir);
		}

		[ClassCleanup(ClassCleanupBehavior.EndOfClass)]
		public static void RemovePlayground() => Directory.Delete(_dir, recursive: true);

		private const string Config = """
			{
			  "coreName": "versionbox",
			  "video": { "width": 320, "height": 200 },
			  "audio": { "samplesPerFrame": 1260 },
			  "machineSetting": "game",
			  "versionSetting": "version",
			  "machines": [
			    { "id": "DOOM", "label": "Doom", "when": [ "doom" ],
			      "input": { "name": "Doom Controller", "buttons": [ "Fire" ] },
			      "settingOverrides": { "version": { "options": [ "doom-1.9", "doom-1.2" ], "default": "doom-1.9" } } },
			    { "id": "DOOM2", "label": "Doom II", "when": [ "doom2" ],
			      "input": { "name": "Doom Controller", "buttons": [ "Fire" ] },
			      "settingOverrides": { "version": { "options": [ "doom2-1.9", "freedoom2" ], "default": "freedoom2" } } }
			  ],
			  "settings": [
			    { "name": "game", "type": "enum", "options": [ "doom", "doom2" ], "default": "doom" },
			    { "name": "version", "type": "enum", "options": [ "doom-1.9", "doom-1.2", "doom2-1.9", "freedoom2" ], "default": "doom-1.9" },
			    { "name": "skill", "type": "enum", "options": [ "1", "2", "3", "4", "5" ], "default": "4" }
			  ]
			}
			""";

		private const string Slots = """
			{ "slots": [ { "id": "pwad", "title": "PWADs", "min": 0, "max": 8, "formats": [ "wad" ] } ] }
			""";

		private static string MakePackage(string name, string config)
		{
			var path = Path.Combine(_dir, name + ".chimeraCore");
			if (File.Exists(path)) return path;
			using var zip = ZipFile.Open(path, ZipArchiveMode.Create);
			void Add(string entry, string text)
			{
				using var writer = new StreamWriter(zip.CreateEntry(entry).Open());
				writer.Write(text);
			}
			Add("waterbox.config", config);
			Add("file_slots.json", Slots);
			Add("core.wbx", "not a real guest, and nothing here runs one");
			return path;
		}

		private static NewProjectWizard MakeForm(string name, string config)
		{
			List<DiscoveredCorePackage> cores =
			[
				new() { Name = name, Path = MakePackage(name, config), Systems = [ "DOOM" ], Version = "" },
			];
			NewProjectWizard form = new(cores, static _ => [ ]);
			form.Show();
			return form;
		}

		[TestMethod]
		public void TheVersionsAreTheChosenMachines()
		{
			using var form = MakeForm("versionbox", Config);

			CollectionAssert.AreEqual(new[] { "doom-1.9", "doom-1.2" }, form.VersionOptions, "Doom's releases, not every release");
			Assert.AreEqual("doom-1.9", form.ChosenVersion);

			form.ChooseMachineForTest("DOOM2");
			CollectionAssert.AreEqual(new[] { "doom2-1.9", "freedoom2" }, form.VersionOptions, "the box narrows with the System");
			Assert.AreEqual("freedoom2", form.ChosenVersion, "a release the new machine lacks gives way to its default");
			Assert.AreEqual("freedoom2", form.SettingValue("version"), "and the choice is a setting like any other");
		}

		[TestMethod]
		public void TheChosenVersionIsWhatTheSettingsSay()
		{
			using var form = MakeForm("versionbox", Config);
			form.ChooseVersionForTest("doom-1.2");
			Assert.AreEqual("doom-1.2", form.SettingValue("version"));
		}

		[TestMethod]
		public void ASeededProjectKeepsItsVersion()
		{
			using var form = MakeForm("versionbox", Config);
			form.SeedFrom(ProjectAnswers.For("versionbox", null, """{"game":"doom2","version":"doom2-1.9"}""", [ ]));

			Assert.AreEqual("DOOM2", form.ChosenMachine);
			Assert.AreEqual("doom2-1.9", form.ChosenVersion, "the project's release, not the machine's default");
		}

		[TestMethod]
		public void TheVersionIsNotAskedTwice()
		{
			using var form = MakeForm("versionbox", Config);
			form.UseDeclaration(TestPackages.Slots(Slots));
			form.UseSettingsFrom(Chimera.Emulation.Common.Waterbox.WaterboxConfig.FromJson(Config)!);
			CollectionAssert.DoesNotContain(form.ExposedSettingNames, "version", "it is on page one, beside the System");
			CollectionAssert.DoesNotContain(form.ExposedSettingNames, "game");
			CollectionAssert.Contains(form.ExposedSettingNames, "skill", "the rest of the settings are where they were");
		}

		[TestMethod]
		public void ACoreWithNoVersionShowsNoBox()
		{
			var plain = Config.Replace("\"versionSetting\": \"version\",", "");
			using var form = MakeForm("plainbox", plain);
			Assert.AreEqual(0, form.VersionOptions.Length);
			Assert.IsNull(form.ChosenVersion);
		}
	}
}
