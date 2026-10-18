using System.IO;
using System.Linq;
using System.Text;

using Chimera.Client.GUI;
using Chimera.Emulation.Common.Engine;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Issue #293: a PlayStation 2 project asks for its Boot ROM by file name, from a list of seventy, and
	/// only the next page said whether that file was here. The settings page now lists first the values
	/// whose firmware is on hand, marked found, and shows each value by the label the package gives it.
	/// The value stored is still the value.
	/// </summary>
	[TestClass]
	public class WizardFirmwareChoicesTests
	{
		private static string _dir = "";

		[ClassInitialize]
		public static void MakePlayground(TestContext _)
		{
			_dir = Path.Combine(Path.GetTempPath(), $"chimera-wizard-choices-{System.Guid.NewGuid():N}");
			Directory.CreateDirectory(_dir);
		}

		[ClassCleanup(ClassCleanupBehavior.EndOfClass)]
		public static void RemovePlayground() => Directory.Delete(_dir, recursive: true);

		private static string Sha1(string text) => ChimeraEngine.Sha1Hex(Encoding.ASCII.GetBytes(text));

		/// <summary>three Boot ROMs, one BIOS each; two labelled; and a setting that decides no file</summary>
		private static WaterboxConfig MakeCfg() => TestPackages.Config($$"""
			{
			  "coreName": "PCSX2-shaped",
			  "systemId": "PS2",
			  "video": { "width": 640, "height": 448 },
			  "audio": { "samplesPerFrame": 1024 },
			  "input": { "buttons": [] },
			  "settings": [
			    { "name": "bios", "display": "Boot ROM", "type": "enum",
			      "options": ["ps2-0100j-20000117", "ps2-0160e-20011004", "ps2-0230a-20080220"],
			      "optionLabels": { "ps2-0160e-20011004": "Europe v1.60 (2001-10-04)", "ps2-0230a-20080220": "USA v2.30 (2008-02-20)" },
			      "default": "ps2-0230a-20080220" },
			    { "name": "dithering", "type": "enum", "options": ["off", "unscaled", "scaled"],
			      "optionLabels": { "unscaled": "Unscaled" }, "default": "unscaled" }
			  ],
			  "firmware": [
			    { "id": "bios.bin", "name": "ps2-0100j-20000117.bin", "sha1": "{{Sha1("JP 1.00")}}",
			      "requiredWhen": { "setting": "bios", "is": "ps2-0100j-20000117" } },
			    { "id": "bios.bin", "name": "ps2-0160e-20011004.bin", "sha1": "{{Sha1("EU 1.60")}}",
			      "requiredWhen": { "setting": "bios", "is": "ps2-0160e-20011004" } },
			    { "id": "bios.bin", "name": "ps2-0230a-20080220.bin", "sha1": "{{Sha1("US 2.30")}}",
			      "requiredWhen": { "setting": "bios", "is": "ps2-0230a-20080220" } }
			  ]
			}
			""");

		private static NewProjectWizard MakeForm(string firmwareFolder)
		{
			NewProjectWizard form = new([ ], static _ => [ ], firmwareSearchDirs: [ firmwareFolder ]);
			form.Show();
			form.UseSettingsFrom(MakeCfg());
			return form;
		}

		[TestMethod]
		public void TheBootRomsOnHandComeFirstMarkedAndEveryOneByItsLabel()
		{
			var folder = Path.Combine(_dir, "found");
			Directory.CreateDirectory(folder);
			// the European BIOS, under a name nobody would guess: the hash is what counts
			File.WriteAllText(Path.Combine(folder, "my bios.bin"), "EU 1.60");
			using var form = MakeForm(folder);

			var (shown, order) = form.SettingChoicesForTest("bios");
			CollectionAssert.AreEqual(new[]
			{
				"Europe v1.60 (2001-10-04)" + WaterboxSettingsBase.FoundMark,
				"ps2-0100j-20000117",
				"USA v2.30 (2008-02-20)",
			}, order.ToArray(), "found first, then the rest in the declared order; a value with no label is shown as it is");
			Assert.AreEqual("USA v2.30 (2008-02-20)", shown, "the default, by its label, not found");

			form.SetSettingValue("bios", "ps2-0160e-20011004");
			Assert.AreEqual("Europe v1.60 (2001-10-04)" + WaterboxSettingsBase.FoundMark, form.SettingChoicesForTest("bios").Shown);
		}

		[TestMethod]
		public void ASettingThatDecidesNoFileKeepsItsOrderAndTakesItsLabels()
		{
			var folder = Path.Combine(_dir, "unrelated");
			Directory.CreateDirectory(folder);
			File.WriteAllText(Path.Combine(folder, "my bios.bin"), "EU 1.60");
			using var form = MakeForm(folder);

			var (shown, order) = form.SettingChoicesForTest("dithering");
			CollectionAssert.AreEqual(new[] { "off", "Unscaled", "scaled" }, order.ToArray());
			Assert.AreEqual("Unscaled", shown);
		}

		[TestMethod]
		public void NothingOnHandIsTheDeclaredOrderUnmarked()
		{
			var folder = Path.Combine(_dir, "empty");
			Directory.CreateDirectory(folder);
			using var form = MakeForm(folder);

			CollectionAssert.AreEqual(new[]
			{
				"ps2-0100j-20000117",
				"Europe v1.60 (2001-10-04)",
				"USA v2.30 (2008-02-20)",
			}, form.SettingChoicesForTest("bios").Order.ToArray());
		}
	}
}
