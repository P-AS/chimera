using System.IO;
using System.Linq;

using Chimera.Emulation.Common.Waterbox;

using Newtonsoft.Json.Linq;

namespace Chimera.Tests.Client.Common.CorePackages
{
	/// <summary>
	/// The project a core is asked about before the real one is made
	/// (<see cref="GameFilesForCore"/>): a game runs only in a project, so a
	/// suggestion, a compile session and a movie import are asked of one made
	/// from the files picked so far. A disc is a .cue and the track files it
	/// names: a core shown the .cue alone cannot read the disc, which is how a
	/// Saturn core came to be unable to say which BIOS a game needs.
	/// </summary>
	[TestClass]
	public class GameFilesForCoreTests
	{
		private static string Folder()
		{
			var dir = Path.Combine(Path.GetTempPath(), "chimera-gamefiles-" + Path.GetRandomFileName());
			Directory.CreateDirectory(dir);
			return dir;
		}

		/// <summary>A package folder with nothing in it but its name and version.</summary>
		private static string Package(string dir)
		{
			var package = Path.Combine(dir, "package");
			Directory.CreateDirectory(package);
			File.WriteAllBytes(Path.Combine(package, "core.wbx"), [ ]);
			File.WriteAllText(Path.Combine(package, "waterbox.config"), """{"coreName": "Test Core", "version": "7"}""");
			return package;
		}

		private const string Cue = "FILE \"Game (Track 1).bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
			+ "FILE \"Game (Track 2).bin\" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n";

		[TestMethod]
		public void ACuesTrackFilesComeWithIt()
		{
			var dir = Folder();
			try
			{
				File.WriteAllText(Path.Combine(dir, "Game.cue"), Cue);
				File.WriteAllBytes(Path.Combine(dir, "Game (Track 1).bin"), [ 1 ]);
				File.WriteAllBytes(Path.Combine(dir, "Game (Track 2).bin"), [ 2 ]);
				using var project = GameFilesForCore.ProjectFor(Package(dir), [ ("disc", "Game.cue", Path.Combine(dir, "Game.cue")) ], null);
				CollectionAssert.AreEqual(
					new[] { "Game.cue", "Game (Track 1).bin", "Game (Track 2).bin" },
					Enumerable.Range(0, project.FileCount).Select(project.FileName).ToArray(),
					"each track file, under the name the cue gives it");
				Assert.AreEqual("support", project.FileSlot(2), "a track is the cue's, not a slot's");
				Assert.AreEqual(Path.Combine(dir, "Game (Track 2).bin"), project.FileSourcePath(2), "from beside the cue");
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}

		[TestMethod]
		public void TheWizardsDocumentBecomesTheProject()
		{
			var dir = Folder();
			try
			{
				var game = Path.Combine(dir, "Game.iso");
				File.WriteAllBytes(game, [ 1, 2, 3 ]);
				var doc = new JObject
				{
					["slots"] = JObject.Parse("""{"disc": ["Game.iso", "Missing.iso"]}"""),
					["files"] = new JObject { ["Game.iso"] = game },
					["settings"] = JObject.Parse("""{"machine": "saturn"}"""),
				};
				using var project = GameFilesForCore.ProjectFor(Package(dir), doc);
				Assert.AreEqual("Test Core", project.CoreName, "the package's core");
				Assert.AreEqual("7", project.CoreVersion);
				Assert.AreEqual(1, project.FileCount, "a name with no path is not a file");
				Assert.AreEqual("Game.iso", project.FileName(0));
				Assert.AreEqual("disc", project.FileSlot(0), "in the slot it was picked for");
				Assert.AreEqual("saturn", (string?)JObject.Parse(project.SettingsJson)["machine"], "the settings chosen so far");
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}
	}
}
