using System.IO;
using System.Linq;

using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Waterbox;

using Newtonsoft.Json.Linq;

namespace Chimera.Tests.Client.Common.CorePackages
{
	/// <summary>
	/// The files a core is shown when it is asked about a game before any
	/// machine exists (<see cref="GameFilesForCore"/>). A disc is a .cue and
	/// the track files it names: a core shown the .cue alone cannot read the
	/// disc, which is how a Saturn core came to be unable to say which BIOS a
	/// game needs.
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
				var files = GameFilesForCore.WithCueTracks(Path.Combine(dir, "Game.cue"), null);
				CollectionAssert.AreEqual(
					new[] { "Game (Track 1).bin", "Game (Track 2).bin" },
					files.Select(static f => f.Name).ToArray(),
					"each track file, under the name the cue gives it");
				Assert.AreEqual(Path.Combine(dir, "Game (Track 2).bin"), files[1].Path, "from beside the cue");
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}

		[TestMethod]
		public void ATrackThatIsAlreadyListedOrIsNotThereIsNotAdded()
		{
			var dir = Folder();
			try
			{
				File.WriteAllText(Path.Combine(dir, "Game.cue"), Cue);
				File.WriteAllBytes(Path.Combine(dir, "Game (Track 1).bin"), [ 1 ]);
				// track 2 is missing from the folder; track 1 was listed by the caller
				CoreFile[] listed = [ new("Game (Track 1).bin", Path.Combine(dir, "Game (Track 1).bin")) ];
				var files = GameFilesForCore.WithCueTracks(Path.Combine(dir, "Game.cue"), listed);
				Assert.AreEqual(1, files.Count, "nothing listed twice, and nothing that is not on the disk");
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}

		[TestMethod]
		public void ASecondDiscsTracksComeToo()
		{
			var dir = Folder();
			try
			{
				File.WriteAllText(Path.Combine(dir, "Disc 2.cue"), "FILE \"Disc 2.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n");
				File.WriteAllBytes(Path.Combine(dir, "Disc 2.bin"), [ 1 ]);
				File.WriteAllBytes(Path.Combine(dir, "Disc 1.iso"), [ 1 ]);
				// the game is an .iso; the project's second disc is a cue among the other files
				CoreFile[] listed = [ new("Disc 2.cue", Path.Combine(dir, "Disc 2.cue")) ];
				var files = GameFilesForCore.WithCueTracks(Path.Combine(dir, "Disc 1.iso"), listed);
				CollectionAssert.AreEqual(new[] { "Disc 2.cue", "Disc 2.bin" }, files.Select(static f => f.Name).ToArray());
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}

		[TestMethod]
		public void TheWizardsDocumentBecomesTheSlotMapAndTheFiles()
		{
			var doc = JObject.Parse("""
				{"slots": {"disc": ["Game.cue"]}, "files": {"Game.cue": "/somewhere/Game.cue"}, "settings": {"machine": "saturn"}}
				""");
			var files = GameFilesForCore.FromSlotsDocument(doc);
			Assert.AreEqual("slots", files[0].Name, "the slot map is mounted as a run mounts it");
			Assert.AreEqual("""{"disc":["Game.cue"]}""", System.Text.Encoding.UTF8.GetString(files[0].Data!));
			Assert.AreEqual("Game.cue", files[1].Name);
			Assert.AreEqual("/somewhere/Game.cue", files[1].Path);
			Assert.AreEqual(2, files.Count, "the settings are not a file");
		}
	}
}
