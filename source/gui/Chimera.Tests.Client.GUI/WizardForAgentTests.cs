using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;

using Chimera.Client.Common;
using Chimera.Client.GUI;
using Chimera.Emulation.Common.Engine;

using Newtonsoft.Json.Linq;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The MCP server's create_project (issue 355): the New Project wizard, never shown, takes an agent's
	/// answers through its own pages and makes the project it would make by hand. What it would ask a person
	/// for it says instead: the firmware it could not find, and where it looked.
	/// </summary>
	[TestClass]
	public class WizardForAgentTests
	{
		private static string _dir = "";

		[ClassInitialize]
		public static void MakePlayground(TestContext _)
		{
			_dir = Path.Combine(Path.GetTempPath(), $"chimera-wizard-agent-{System.Guid.NewGuid():N}");
			Directory.CreateDirectory(_dir);
		}

		[ClassCleanup(ClassCleanupBehavior.EndOfClass)]
		public static void RemovePlayground() => Directory.Delete(_dir, recursive: true);

		private static readonly string BiosSha1 = ChimeraEngine.Sha1Hex(Encoding.ASCII.GetBytes("the boot rom"));

		/// <summary>Two machines, a slot for each kind of game, a setting, and a Boot ROM the cartridge machine needs.</summary>
		private static string MakePackage()
		{
			var path = Path.Combine(_dir, "agentbox.chimeraCore");
			if (File.Exists(path)) return path;
			using var zip = ZipFile.Open(path, ZipArchiveMode.Create);
			void Add(string name, string text)
			{
				using var writer = new StreamWriter(zip.CreateEntry(name).Open());
				writer.Write(text);
			}
			Add("waterbox.config", $$"""
				{
				  "coreName": "agentbox",
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
				    { "name": "region", "type": "enum", "options": ["ntsc", "pal"], "default": "ntsc" },
				    { "name": "seed", "type": "uint", "default": 1 }
				  ],
				  "firmware": [
				    { "id": "bios", "display": "Boot ROM", "name": "boot.rom", "size": 12, "sha1": "{{BiosSha1}}",
				      "requiredWhen": { "setting": "machine", "is": "cart" } }
				  ]
				}
				""");
			Add("file_slots.json", """
				{ "slots": [
				  { "id": "rom", "title": "Cartridge", "min": 0, "max": 1, "formats": ["crt"] },
				  { "id": "cdrom", "title": "Disc", "min": 0, "max": 1, "formats": ["iso"] }
				] }
				""");
			Add("core.wbx", "not a real guest, and nothing here runs one");
			return path;
		}

		private static string MakeFile(string name, string text)
		{
			var path = Path.Combine(_dir, name);
			File.WriteAllText(path, text);
			return path;
		}

		private static NewProjectWizard MakeForm(string? firmwareFolder = null)
		{
			List<DiscoveredCorePackage> cores =
			[
				new() { Name = "agentbox", Path = MakePackage(), Systems = [ "CART", "DISC" ], Version = "" },
			];
			return new(cores, static _ => [ ], firmwareSearchDirs: firmwareFolder is null ? [ ] : [ firmwareFolder ]);
		}

		private static NewProjectWizard.AgentOutcome Complete(NewProjectWizard form, string core = "agentbox", string? system = null,
			IReadOnlyList<string>? games = null, Dictionary<string, object>? settings = null, IReadOnlyList<(string, string)>? firmware = null)
			=> form.CompleteForAgent(core, null, system, [ ], games ?? [ ], settings ?? new(), firmware ?? [ ]);

		[TestMethod]
		public void TheProjectIsTheOneTheWizardMakes()
		{
			var folder = Path.Combine(_dir, "firmware-here");
			Directory.CreateDirectory(folder);
			File.WriteAllText(Path.Combine(folder, "any name.bin"), "the boot rom");
			var game = MakeFile("game.crt", "a cartridge");
			using var form = MakeForm(folder);

			var outcome = Complete(form, games: [ game ], settings: new() { ["region"] = "pal", ["seed"] = 4000000000L });

			Assert.IsTrue(outcome.Made, outcome.Refusal);
			using var project = form.CreatedProject!;
			Assert.AreEqual("agentbox", project.CoreName);
			Assert.AreEqual(1, project.FileCount);
			Assert.AreEqual("rom", project.FileSlot(0), "the game went in the slot that takes its kind of file");
			var settings = JObject.Parse(project.SettingsJson);
			Assert.AreEqual("pal", (string) settings["region"]!, "the agent's setting");
			Assert.AreEqual(4000000000L, (long) settings["seed"]!, "a uint past an int, kept whole");
			Assert.AreEqual("cart", (string) settings["machine"]!, "and every other one recorded at its value, as the wizard does");
			StringAssert.Contains(project.FirmwareJson, BiosSha1, "the Boot ROM found in the folder is pinned");
			CollectionAssert.AreEqual(new[] { "bios" }, form.ProvidedFirmwarePaths.Keys.ToArray(), "and handed back to be remembered");
		}

		[TestMethod]
		public void MissingFirmwareIsListedWithWhatWouldDoAndWhereItWasLooked()
		{
			var empty = Path.Combine(_dir, "no-firmware");
			Directory.CreateDirectory(empty);
			using var form = MakeForm(empty);

			var outcome = Complete(form, games: [ MakeFile("other.crt", "another cartridge") ]);

			Assert.IsFalse(outcome.Made);
			Assert.IsNull(form.CreatedProject);
			Assert.AreEqual(1, outcome.Missing.Count);
			Assert.AreEqual("bios", outcome.Missing[0].Id);
			Assert.AreEqual("Boot ROM", outcome.Missing[0].Name);
			Assert.AreEqual("boot.rom", outcome.Missing[0].Accepted.Single().Name);
			Assert.AreEqual(BiosSha1, outcome.Missing[0].Accepted.Single().Sha1);
			CollectionAssert.AreEqual(new[] { empty }, outcome.Searched.ToArray());
		}

		[TestMethod]
		public void AMachineIsChosenByWhatItIsCalled()
		{
			using var form = MakeForm();
			// the disc machine needs no Boot ROM, so nothing stands in the way
			var outcome = Complete(form, system: "disc console", games: [ MakeFile("game.iso", "a disc") ]);

			Assert.IsTrue(outcome.Made, outcome.Refusal);
			using var project = form.CreatedProject!;
			Assert.AreEqual("disc", (string) JObject.Parse(project.SettingsJson)["machine"]!);
			Assert.AreEqual("cdrom", project.FileSlot(0));
		}

		[TestMethod]
		public void WhatTheCoreDoesNotHaveIsNamed()
		{
			using (var form = MakeForm())
			{
				StringAssert.Contains(Complete(form, core: "no such core").Refusal, "list_cores");
			}
			using (var form = MakeForm())
			{
				var refused = Complete(form, system: "a toaster").Refusal;
				StringAssert.Contains(refused, "Cartridge Console");
				StringAssert.Contains(refused, "Disc Console");
			}
			using (var form = MakeForm())
			{
				StringAssert.Contains(Complete(form, system: "DISC", settings: new() { ["no such setting"] = 1L }).Refusal, "'no such setting'");
			}
			using (var form = MakeForm())
			{
				StringAssert.Contains(Complete(form, games: [ Path.Combine(_dir, "not there.crt") ]).Refusal, "There is no file");
			}
		}
	}
}
