using System.Linq;

using Chimera.Client.GUI;
using Chimera.Emulation.Common.Engine;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Issue 370 (owner, 2026-10-25): an open project's settings are changed in a window of
	/// their own, without a new project. It shows what the New Project wizard shows for the
	/// same files - plus the renderer, which the wizard asks on its first page, and without
	/// the machine, which goes with the project's files - and offers to restart only when
	/// something the machine is made from changed.
	/// </summary>
	[TestClass]
	public class ProjectSettingsFormTests
	{
		private static WaterboxConfig Cfg()
			=> TestPackages.Config("""
				{
				  "coreName": "settings-shaped",
				  "video": { "width": 256, "height": 240 },
				  "audio": { "samplesPerFrame": 1024 },
				  "input": { "buttons": [] },
				  "machineSetting": "machine",
				  "machines": [
				    { "id": "CART", "label": "Cartridge Console", "when": [ "cart" ] },
				    { "id": "DISC", "label": "Disc Console", "when": [ "disc" ] }
				  ],
				  "settings": [
				    { "name": "machine", "type": "enum", "options": ["cart", "disc"], "default": "cart" },
				    { "name": "renderer", "type": "enum", "options": ["software", "vulkan-hw"], "default": "software" },
				    { "name": "antiAliasing", "type": "enum", "options": ["Auto", "Disabled"], "default": "Auto" },
				    { "name": "bigCartRam", "type": "int", "default": 64,
				      "exposedWhen": { "slot": "rom", "extension": "big" } }
				  ]
				}
				""");

		private static ProjectSettingsForm MakeForm(WaterboxConfig cfg, string slotsJson = """{"rom":["game.crt"]}""", string gameSettings = "")
		{
			var game = WaterboxConfig.ParseGameSettings(gameSettings, cfg.Settings!.Select(static d => d.Key));
			ProjectSettingsForm form = new(cfg, new WaterboxCoreSettings(), game, slotsJson, EngineProject.ClockStartDefault);
			form.Show();
			return form;
		}

		[TestMethod]
		public void TheRendererIsHereAndTheMachineIsNot()
		{
			using var form = MakeForm(Cfg());
			CollectionAssert.Contains(form.ShownSettings.ToArray(), "renderer", "the renderer is what #370 wanted to try");
			CollectionAssert.Contains(form.ShownSettings.ToArray(), "antiAliasing");
			CollectionAssert.DoesNotContain(form.ShownSettings.ToArray(), "machine", "the machine goes with the project's files");
		}

		[TestMethod]
		public void TheProjectsFilesDecideWhatIsShown()
		{
			using var crt = MakeForm(Cfg(), """{"rom":["game.crt"]}""");
			CollectionAssert.DoesNotContain(crt.ShownSettings.ToArray(), "bigCartRam");
			using var big = MakeForm(Cfg(), """{"rom":["game.big"]}""");
			CollectionAssert.Contains(big.ShownSettings.ToArray(), "bigCartRam", "a setting a slot exposes, as in the wizard");
		}

		[TestMethod]
		public void TheGamesOwnSettingsAreShownLast()
		{
			using var form = MakeForm(Cfg(), gameSettings: """[{ "name": "dipFreePlay", "type": "bool", "default": false }]""");
			Assert.AreEqual("dipFreePlay", form.ShownSettings[^1]);
		}

		[TestMethod]
		public void ARestartIsOfferedOnlyForAChange()
		{
			using var form = MakeForm(Cfg());
			Assert.IsFalse(form.Changed);
			Assert.IsFalse(form.RestartOffered, "nothing changed yet");

			form.SetValueForTest("antiAliasing", "Disabled");
			Assert.IsTrue(form.Changed);
			Assert.IsTrue(form.RestartOffered);

			form.SetValueForTest("antiAliasing", "Auto");
			Assert.IsFalse(form.Changed, "the default typed back is the machine it was");
			Assert.IsFalse(form.RestartOffered);
		}

		[TestMethod]
		public void TheClocksStartIsPartOfTheMachine()
		{
			using var form = MakeForm(Cfg());
			form.ClockTextForTest = "2009-02-13 23:31:30";
			Assert.IsTrue(form.Changed);
			Assert.AreEqual(1234567890L, form.ClockStart);
			Assert.IsTrue(form.RestartOffered);

			form.ClockTextForTest = "not a date";
			Assert.IsFalse(form.RestartOffered, "a start that cannot be read is not offered");
		}
	}
}
