using System.Collections.Generic;
using System.IO;
using System.Linq;

using Chimera.Client.Common;

using Newtonsoft.Json.Linq;

namespace Chimera.Tests.Client.Common.Movie
{
	/// <summary>
	/// The MCP server's tools of Chimera's main window (issue 355): pausing and running the machine,
	/// saving the project to a file the agent names, closing it, and saying where things are. Nothing
	/// they do may ask a question, so what would be asked is decided by the arguments: a file that is
	/// already there is kept unless 'overwrite' says otherwise, and unsaved changes are lost only when
	/// 'discard_unsaved' says they may be.
	/// </summary>
	[TestClass]
	public class McpMainWindowToolsTests
	{
		private sealed class FakeChimera : IMcpChimera
		{
			public int Frame { get; set; } = 5;
			public bool Paused { get; set; } = true;
			public readonly List<string> Saved = new();
			public int Closed;
			public string? SaveFails;

			public void Pause() => Paused = true;

			public void Resume() => Paused = false;

			public JObject ServerInfo() => new() { ["window"] = true };

			public string ProjectsFolder { get; set; } = "";

			public string? SaveProject(string path)
			{
				if (SaveFails is not null) return SaveFails;
				Saved.Add(path);
				File.WriteAllText(path, "saved");
				return null;
			}

			public string? CloseProject()
			{
				Closed++;
				Frame = -1;
				return null;
			}

			public McpProjectRequest? Asked;
			public McpProjectMade Made = new() { Opened = new JObject { ["core"] = "Synth" } };

			public McpProjectMade CreateProject(McpProjectRequest request)
			{
				Asked = request;
				return Made;
			}
		}

		private string _folder = "";

		[TestInitialize]
		public void AFolderOfItsOwn() => Directory.CreateDirectory(_folder = Path.Combine(Path.GetTempPath(), "chimera-mcp-main-" + Path.GetRandomFileName()));

		[TestCleanup]
		public void Gone() => Directory.Delete(_folder, recursive: true);

		private static (bool IsError, string Text) Run(ITasMovie? movie, IMcpChimera chimera, string tool, string arguments = "{}")
		{
			var text = McpHostTools.Run(movie, null, tool, arguments, out var isError, chimera)!;
			return (isError, text);
		}

		[TestMethod]
		public void PauseAndResumeAnswerWhereTheMachineIs()
		{
			FakeChimera chimera = new() { Paused = false, Frame = 42 };
			var (isError, text) = Run(TasMovieTests.MakeMovie(3), chimera, "pause");
			Assert.IsFalse(isError, text);
			Assert.IsTrue((bool) JObject.Parse(text)["paused"]!);
			Assert.AreEqual(42, (int) JObject.Parse(text)["frame"]!);
			(isError, text) = Run(TasMovieTests.MakeMovie(3), chimera, "resume");
			Assert.IsFalse(isError, text);
			Assert.IsFalse((bool) JObject.Parse(text)["paused"]!);
			Assert.IsFalse(chimera.Paused);

			// with no machine there is nothing to pause
			(isError, text) = Run(null, new FakeChimera { Frame = -1 }, "pause");
			Assert.IsTrue(isError);
			Assert.AreEqual(McpHostTools.NoMovie, text);
		}

		[TestMethod]
		public void ServerInfoNeedsNoProject()
		{
			var (isError, text) = Run(null, new FakeChimera { Frame = -1 }, "server_info");
			Assert.IsFalse(isError, text);
			Assert.IsTrue((bool) JObject.Parse(text)["window"]!);
		}

		[TestMethod]
		public void ANameIsSavedInTheMoviesFolderWithTheProjectsExtension()
		{
			var movie = TasMovieTests.MakeMovie(3);
			FakeChimera chimera = new() { ProjectsFolder = _folder };
			var (isError, text) = Run(movie, chimera, "save_project", """{ "path": "Ninja Gaiden (U) agent" }""");
			Assert.IsFalse(isError, text);
			var expected = Path.Combine(_folder, "Ninja Gaiden (U) agent.chimeraProject");
			CollectionAssert.AreEqual(new[] { expected }, chimera.Saved);
			Assert.AreEqual(expected, (string) JObject.Parse(text)["saved"]!);
		}

		[TestMethod]
		public void AFileThatIsThereIsKeptUnlessOverwriteIsGiven()
		{
			var movie = TasMovieTests.MakeMovie(3);
			FakeChimera chimera = new() { ProjectsFolder = _folder };
			var there = Path.Combine(_folder, "there.chimeraProject");
			File.WriteAllText(there, "the person's own");

			var (isError, text) = Run(movie, chimera, "save_project", new JObject { ["path"] = there }.ToString());
			Assert.IsTrue(isError);
			StringAssert.Contains(text, "overwrite: true");
			Assert.AreEqual(0, chimera.Saved.Count);
			Assert.AreEqual("the person's own", File.ReadAllText(there), "nothing was written");

			(isError, text) = Run(movie, chimera, "save_project", new JObject { ["path"] = there, ["overwrite"] = true }.ToString());
			Assert.IsFalse(isError, text);
			CollectionAssert.AreEqual(new[] { there }, chimera.Saved);
		}

		[TestMethod]
		public void WithNoPathTheProjectIsSavedWhereItIs()
		{
			var movie = TasMovieTests.MakeMovie(3);
			FakeChimera chimera = new() { ProjectsFolder = _folder };
			// never saved: there is nowhere to save it to
			var (isError, text) = Run(movie, chimera, "save_project");
			Assert.IsTrue(isError);
			StringAssert.Contains(text, "give 'path'");

			// its own file is no file of someone else's: saving over it needs no 'overwrite'
			var own = Path.Combine(_folder, "own.chimeraProject");
			File.WriteAllText(own, "before");
			movie.Filename = own;
			(isError, text) = Run(movie, chimera, "save_project");
			Assert.IsFalse(isError, text);
			(isError, text) = Run(movie, chimera, "save_project", new JObject { ["path"] = own }.ToString());
			Assert.IsFalse(isError, text);
			CollectionAssert.AreEqual(new[] { own, own }, chimera.Saved);
		}

		[TestMethod]
		public void AFolderThatIsNotThereOrAFailedSaveIsSaid()
		{
			var movie = TasMovieTests.MakeMovie(3);
			FakeChimera chimera = new() { ProjectsFolder = _folder };
			var (isError, text) = Run(movie, chimera, "save_project", new JObject { ["path"] = Path.Combine(_folder, "no such folder", "p") }.ToString());
			Assert.IsTrue(isError);
			StringAssert.Contains(text, "does not exist");

			chimera.SaveFails = "the disk is full";
			(isError, text) = Run(movie, chimera, "save_project", """{ "path": "p" }""");
			Assert.IsTrue(isError);
			StringAssert.Contains(text, "the disk is full");
		}

		[TestMethod]
		public void UnsavedChangesAreLostOnlyWhenTheAgentSaysSo()
		{
			var movie = TasMovieTests.MakeMovie(3);
			Assert.IsTrue(movie.Changes, "a movie that was just made has changes nobody saved");
			FakeChimera chimera = new();

			var (isError, text) = Run(movie, chimera, "close_tastudio");
			Assert.IsTrue(isError);
			StringAssert.Contains(text, "discard_unsaved: true");
			Assert.AreEqual(0, chimera.Closed);
			Assert.IsTrue(movie.Changes, "nothing was thrown away");

			(isError, text) = Run(movie, chimera, "close_tastudio", """{ "discard_unsaved": true }""");
			Assert.IsFalse(isError, text);
			Assert.AreEqual(1, chimera.Closed);
			Assert.IsFalse(movie.Changes, "cleared first, so closing has nothing to ask");
			var answer = JObject.Parse(text);
			Assert.IsTrue((bool) answer["closed"]!);
			Assert.IsFalse((bool) answer["machine_open"]!);
			StringAssert.Contains((string) answer["note"]!, "lost");
		}

		[TestMethod]
		public void CreateProjectHandsTheWindowTheAgentsAnswers()
		{
			FakeChimera chimera = new();
			var game = Path.Combine(_folder, "game.nes");
			File.WriteAllText(game, "a game");
			var (isError, text) = Run(null, chimera, "create_project", new JObject
			{
				["core"] = " Synth ",
				["system"] = "NES",
				["game"] = game,
				["files"] = new JObject { ["disk"] = new JArray(game, game) },
				["settings"] = new JObject { ["region"] = "pal", ["seed"] = 4000000000L },
				["firmware"] = new JObject { ["bios"] = game },
				["firmware_folder"] = _folder,
			}.ToString());
			Assert.IsFalse(isError, text);
			Assert.AreEqual("Synth", (string) JObject.Parse(text)["core"]!, "the window's answer is the agent's");
			var asked = chimera.Asked!;
			Assert.AreEqual("Synth", asked.Core);
			Assert.AreEqual("NES", asked.System);
			CollectionAssert.AreEqual(new[] { game }, asked.Games.ToArray());
			CollectionAssert.AreEqual(new[] { ("disk", game), ("disk", game) }, asked.Files.ToArray());
			Assert.AreEqual("pal", asked.Settings["region"]);
			Assert.AreEqual(4000000000L, asked.Settings["seed"]);
			CollectionAssert.AreEqual(new[] { ("bios", game) }, asked.Firmware.ToArray());
			Assert.AreEqual(_folder, asked.FirmwareFolder);
			Assert.IsFalse(asked.DiscardUnsaved);
		}

		[TestMethod]
		public void AProjectWithUnsavedChangesIsNotReplacedUnlessTheAgentSaysSo()
		{
			var movie = TasMovieTests.MakeMovie(3);
			FakeChimera chimera = new();
			var (isError, text) = Run(movie, chimera, "create_project", """{ "core": "Synth" }""");
			Assert.IsTrue(isError);
			StringAssert.Contains(text, "discard_unsaved: true");
			Assert.IsNull(chimera.Asked, "the window was not asked");

			(isError, text) = Run(movie, chimera, "create_project", """{ "core": "Synth", "discard_unsaved": true }""");
			Assert.IsFalse(isError, text);
			Assert.IsTrue(chimera.Asked!.DiscardUnsaved, "the window lets them go once the new project is made, not before");
			Assert.IsTrue(movie.Changes);
		}

		[TestMethod]
		public void MissingFirmwareIsAnAnswerOfItsOwn()
		{
			FakeChimera chimera = new()
			{
				Made = new()
				{
					Refusal = "Firmware the project needs was not found: Boot ROM.",
					Missing = new JArray(new JObject { ["id"] = "bios", ["name"] = "Boot ROM" }),
					Searched = new JArray(_folder),
				},
			};
			var (isError, text) = Run(null, chimera, "create_project", """{ "core": "Synth" }""");
			Assert.IsTrue(isError);
			var answer = JObject.Parse(text);
			Assert.AreEqual("missing_firmware", (string) answer["error"]!);
			Assert.AreEqual("bios", (string) answer["missing"]![0]!["id"]!);
			Assert.AreEqual(_folder, (string) answer["searched"]![0]!);

			chimera.Made = new() { Refusal = "There is no core called 'Nope'." };
			(isError, text) = Run(null, chimera, "create_project", """{ "core": "Nope" }""");
			Assert.IsTrue(isError);
			Assert.AreEqual("There is no core called 'Nope'.", text);
		}

		[TestMethod]
		public void CreateProjectArgumentsOfTheWrongShapeAreRefused()
		{
			FakeChimera chimera = new();
			foreach (var (arguments, said) in new[]
			{
				("{}", "Give 'core'"),
				("""{ "core": "Synth", "files": ["game.nes"] }""", "'files' is an object"),
				("""{ "core": "Synth", "game": 5 }""", "'game' is a path"),
				("""{ "core": "Synth", "settings": { "region": [1] } }""", "'settings' is an object"),
				("""{ "core": "Synth", "firmware": { "bios": 1 } }""", "'firmware' is an object"),
				("""{ "core": "Synth", "firmware_folder": "/no/such/folder" }""", "There is no folder"),
			})
			{
				var (isError, text) = Run(null, chimera, "create_project", arguments);
				Assert.IsTrue(isError, arguments);
				StringAssert.Contains(text, said, arguments);
			}
			Assert.IsNull(chimera.Asked, "nothing reached the window");
		}

		[TestMethod]
		public void ASavedProjectClosesWithNothingToSay()
		{
			var movie = TasMovieTests.MakeMovie(3);
			movie.ClearChanges();
			FakeChimera chimera = new();
			var (isError, text) = Run(movie, chimera, "close_tastudio");
			Assert.IsFalse(isError, text);
			Assert.AreEqual(1, chimera.Closed);
			Assert.IsNull(JObject.Parse(text)["note"]);
		}
	}
}
