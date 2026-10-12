using System.Linq;

using Chimera.Client.Common;

using Newtonsoft.Json.Linq;

namespace Chimera.Tests.Client.Common.Movie
{
	/// <summary>
	/// The MCP server's tools that the frontend runs (docs/mcp-server.md): they read what
	/// the open movie holds and answer in JSON, and say why when they cannot.
	/// </summary>
	[TestClass]
	public class McpHostToolsTests
	{
		[TestMethod]
		public void TheMarkersAreListedWithTheirFramesAndTexts()
		{
			var movie = TasMovieTests.MakeMovie(20);
			movie.Markers.Add(7, "the jump");
			movie.Markers.Add(15, "");

			var answer = JObject.Parse(McpHostTools.Run(movie, null, "list_markers", "{}", out var isError));

			Assert.IsFalse(isError);
			var markers = (JArray) answer["markers"]!;
			// every marker the movie holds is listed, in the movie's own order
			Assert.AreEqual(movie.Markers.Count, markers.Count);
			for (var i = 0; i < markers.Count; i++) Assert.AreEqual(movie.Markers[i].Frame, (int) markers[i]["frame"]!);
			// a movie has markers of its own; the two a person placed are told apart from them
			var placed = markers.Where(static m => (bool) m["placed_by_user"]!).ToList();
			Assert.AreEqual(2, placed.Count);
			Assert.AreEqual(7, (int) placed[0]["frame"]!);
			Assert.AreEqual("the jump", (string) placed[0]["text"]!);
			Assert.AreEqual(15, (int) placed[1]["frame"]!);
			Assert.AreEqual("", (string) placed[1]["text"]!);
			Assert.IsTrue(markers.Any(static m => !(bool) m["placed_by_user"]!), "the movie's own markers are listed too");
		}

		[TestMethod]
		public void TheBranchesAreListedNumberedFromOneWithTheCurrentOneSaid()
		{
			var movie = TasMovieTests.MakeMovie(20);
			movie.Branches.Add(new TasBranch { Frame = 4, TimeStamp = new(2026, 10, 12, 9, 30, 5), UserText = "before the door" });
			movie.Branches.Add(new TasBranch { Frame = 12, TimeStamp = new(2026, 10, 12, 9, 45, 0), UserText = null });
			movie.Branches.Current = 1;

			var answer = JObject.Parse(McpHostTools.Run(movie, null, "list_branches", "{}", out var isError));

			Assert.IsFalse(isError);
			var branches = (JArray) answer["branches"]!;
			Assert.AreEqual(2, branches.Count);
			Assert.AreEqual(1, (int) branches[0]["number"]!);
			Assert.AreEqual(4, (int) branches[0]["frame"]!);
			Assert.AreEqual("2026-10-12 09:30:05", (string) branches[0]["taken"]!);
			Assert.AreEqual("before the door", (string) branches[0]["note"]!);
			Assert.IsFalse((bool) branches[0]["current"]!);
			Assert.AreEqual(2, (int) branches[1]["number"]!);
			Assert.AreEqual("", (string) branches[1]["note"]!);
			Assert.IsTrue((bool) branches[1]["current"]!);
		}

		[TestMethod]
		public void AMovieWithNoBranchesAnswersAnEmptyList()
		{
			var answer = JObject.Parse(McpHostTools.Run(TasMovieTests.MakeMovie(3), null, "list_branches", "{}", out var isError));
			Assert.IsFalse(isError);
			Assert.AreEqual(0, ((JArray) answer["branches"]!).Count);
		}

		[TestMethod]
		public void WithNoProjectOpenEveryToolSaysSo()
		{
			foreach (var tool in new[] { "list_markers", "list_branches" })
			{
				var answer = McpHostTools.Run(null, null, tool, "{}", out var isError);
				Assert.IsTrue(isError, tool);
				Assert.AreEqual(McpHostTools.NoMovie, answer, tool);
			}
		}

		/// <summary>The window the tools ask things of, with nothing behind it but a record of what was asked.</summary>
		private sealed class FakeWindow : IMcpWindow
		{
			public int Frame { get; set; }
			public int SoughtTo = -1;
			public readonly System.Collections.Generic.List<string> Notes = new();
			public int Loaded;

			public void SeekTo(int frame) => SoughtTo = frame;

			public int SaveBranch(string note)
			{
				Notes.Add(note);
				return Notes.Count;
			}

			public void LoadBranch(int number) => Loaded = number;
		}

		/// <summary>A movie of that many empty frames with nothing in its undo history (making it is a step, otherwise).</summary>
		private static TasMovie Fresh(int frames)
		{
			var movie = TasMovieTests.MakeMovie(frames);
			movie.ChangeLog.Clear();
			return movie;
		}

		private static string[] Lines(ITasMovie movie)
			=> Enumerable.Range(0, movie.InputLogLength).Select(movie.GetInputLogEntry).ToArray();

		private static JObject Ok(ITasMovie movie, string tool, string arguments, IMcpWindow? window = null)
		{
			var answer = McpHostTools.Run(movie, window, tool, arguments, out var isError);
			Assert.IsFalse(isError, $"{tool} {arguments} was refused: {answer}");
			return JObject.Parse(answer!);
		}

		private static string Refused(ITasMovie movie, string tool, string arguments, IMcpWindow? window = null)
		{
			var answer = McpHostTools.Run(movie, window, tool, arguments, out var isError);
			Assert.IsTrue(isError, $"{tool} {arguments} was not refused: {answer}");
			return answer!;
		}

		[TestMethod]
		public void ButtonsArePressedOverARangeByNameAndOneUndoTakesItAllBack()
		{
			var movie = TasMovieTests.MakeMovie(10);
			var buttons = movie.Session.MovieController.Definition.BoolButtons;
			// a button the calls below never name, pressed by hand on one frame
			movie.SetBoolState(3, buttons[1], true);
			movie.ChangeLog.Clear();
			var before = Lines(movie);

			var answer = Ok(movie, "set_buttons", $"{{\"frame\":2,\"count\":3,\"buttons\":{{\"{buttons[0]}\":true}}}}");

			Assert.AreEqual(10, (int) answer["movie_length"]!);
			for (var frame = 0; frame < 10; frame++)
			{
				Assert.AreEqual(frame is >= 2 and < 5, movie.GetInputState(frame).IsPressed(buttons[0]), $"{buttons[0]} on frame {frame}");
				Assert.AreEqual(frame is 3, movie.GetInputState(frame).IsPressed(buttons[1]), $"{buttons[1]} was not named and must keep what it had, frame {frame}");
			}
			// released again by name on one frame
			Ok(movie, "set_buttons", $"{{\"frame\":3,\"buttons\":{{\"{buttons[0]}\":false}}}}");
			Assert.IsFalse(movie.GetInputState(3).IsPressed(buttons[0]));
			Assert.IsTrue(movie.GetInputState(3).IsPressed(buttons[1]));
			Assert.IsTrue(movie.GetInputState(4).IsPressed(buttons[0]));

			// each call is one step in the undo history
			Ok(movie, "undo", "{}");
			Assert.IsTrue(movie.GetInputState(3).IsPressed(buttons[0]));
			Ok(movie, "undo", "{}");
			CollectionAssert.AreEqual(before, Lines(movie));
			Assert.IsFalse(movie.ChangeLog.CanUndo);
		}

		[TestMethod]
		public void AButtonOrAnAxisThatDoesNotExistIsRefusedAndNothingChanges()
		{
			var movie = Fresh(5);
			var before = Lines(movie);
			StringAssert.Contains(Refused(movie, "set_buttons", "{\"frame\":0,\"buttons\":{\"No Such Button\":true}}"), "No Such Button");
			StringAssert.Contains(Refused(movie, "set_buttons", "{\"frame\":0,\"axes\":{\"No Such Axis\":3}}"), "No Such Axis");
			Refused(movie, "set_buttons", "{\"frame\":0}");
			Refused(movie, "set_buttons", "{\"frame\":-1,\"buttons\":{}}");
			CollectionAssert.AreEqual(before, Lines(movie));
			Assert.IsFalse(movie.ChangeLog.CanUndo, "a refused call must leave no step in the undo history");
		}

		[TestMethod]
		public void InputLinesAreWrittenAsGivenPastTheEndTooAndOneUndoTakesThemBack()
		{
			var movie = Fresh(6);
			var buttons = movie.Session.MovieController.Definition.BoolButtons;
			movie.SetBoolState(0, buttons[0], true);
			var pressedLine = movie.GetInputLogEntry(0);
			var emptyLine = movie.GetInputLogEntry(1);
			Assert.AreNotEqual(pressedLine, emptyLine);
			var before = Lines(movie);

			// two frames of the movie and two past its end
			var answer = Ok(movie, "set_input", new JObject
			{
				["frame"] = 4,
				["entries"] = new JArray(pressedLine, emptyLine, pressedLine, pressedLine),
			}.ToString());

			Assert.AreEqual(8, (int) answer["movie_length"]!);
			Assert.AreEqual(8, movie.InputLogLength);
			CollectionAssert.AreEqual(new[] { pressedLine, emptyLine, pressedLine, pressedLine }, Lines(movie).Skip(4).ToArray());
			CollectionAssert.AreEqual(before.Take(4).ToArray(), Lines(movie).Take(4).ToArray());

			Ok(movie, "undo", "{}");
			CollectionAssert.AreEqual(before, Lines(movie));
		}

		[TestMethod]
		public void ALineThatIsNotOneOfThisMoviesIsRefusedBeforeAnythingIsWritten()
		{
			var movie = Fresh(4);
			var good = movie.GetInputLogEntry(0);
			var before = Lines(movie);
			foreach (var bad in new[] { "", "|", good + "|", good.Substring(1), "not a line at all" })
			{
				var answer = Refused(movie, "set_input", new JObject { ["frame"] = 1, ["entries"] = new JArray(good, bad) }.ToString());
				StringAssert.Contains(answer, "Line 2");
			}
			Refused(movie, "set_input", "{\"frame\":0,\"entries\":[]}");
			Refused(movie, "set_input", "{\"frame\":0}");
			CollectionAssert.AreEqual(before, Lines(movie));
		}

		[TestMethod]
		public void FramesAreInsertedAndDeleted()
		{
			var movie = Fresh(6);
			var buttons = movie.Session.MovieController.Definition.BoolButtons;
			movie.SetBoolState(2, buttons[0], true);
			var before = Lines(movie);

			Assert.AreEqual(9, (int) Ok(movie, "insert_frames", "{\"frame\":2,\"count\":3}")["movie_length"]!);
			Assert.IsFalse(movie.GetInputState(2).IsPressed(buttons[0]));
			Assert.IsTrue(movie.GetInputState(5).IsPressed(buttons[0]), "the input from the frame on moves later");

			Assert.AreEqual(6, (int) Ok(movie, "delete_frames", "{\"frame\":2,\"count\":3}")["movie_length"]!);
			CollectionAssert.AreEqual(before, Lines(movie));
			// deleting past the end stops at the end
			Assert.AreEqual(2, (int) Ok(movie, "delete_frames", "{\"frame\":4,\"count\":50}")["deleted"]!);
			Assert.AreEqual(4, movie.InputLogLength);

			Refused(movie, "insert_frames", "{\"frame\":99}");
			Refused(movie, "delete_frames", "{\"frame\":99}");
		}

		[TestMethod]
		public void UndoWithNothingToUndoSaysSo()
		{
			StringAssert.Contains(Refused(Fresh(3), "undo", "{}"), "nothing to undo");
		}

		[TestMethod]
		public void AMarkerIsPlacedWithItsText()
		{
			var movie = Fresh(10);
			Ok(movie, "add_marker", "{\"frame\":4,\"text\":\"try the jump here\"}");
			var placed = movie.Markers.Where(static m => !m.IsPermanent).ToList();
			Assert.AreEqual(1, placed.Count);
			Assert.AreEqual(4, placed[0].Frame);
			Assert.AreEqual("try the jump here", placed[0].Message);
			Refused(movie, "add_marker", "{\"text\":\"no frame\"}");
		}

		[TestMethod]
		public void ASeekIsStartedInTheWindowAndAnsweredWhenItHasArrived()
		{
			var movie = Fresh(10);
			FakeWindow window = new() { Frame = 2 };

			// already there: answered at once, and the window is not asked to move
			Assert.AreEqual(2, (int) Ok(movie, "seek", "{\"frame\":2}", window)["frame"]!);
			Assert.AreEqual(-1, window.SoughtTo);

			// elsewhere: the window is asked, and there is no answer yet
			Assert.IsNull(McpHostTools.Run(movie, window, "seek", "{\"frame\":7}", out var isError));
			Assert.IsFalse(isError);
			Assert.AreEqual(7, window.SoughtTo);
			// arrived
			window.Frame = 7;
			Assert.AreEqual(7, (int) JObject.Parse(McpHostTools.SeekAnswer(window, 7, out isError))["frame"]!);
			Assert.IsFalse(isError);
			// stopped somewhere else: said, as a refusal
			window.Frame = 5;
			StringAssert.Contains(McpHostTools.SeekAnswer(window, 7, out isError), "stopped on frame 5");
			Assert.IsTrue(isError);

			// the movie's end is the furthest a seek can go; with no window there is nothing to move
			StringAssert.Contains(Refused(movie, "seek", "{\"frame\":11}", window), "10");
			Assert.AreEqual(10, (int) JObject.Parse("{\"frame\":10}")["frame"]!);
			Assert.IsNull(McpHostTools.Run(movie, window, "seek", "{\"frame\":10}", out _));
			StringAssert.Contains(Refused(movie, "seek", "{\"frame\":3}"), "TAStudio is not open");
		}

		[TestMethod]
		public void BranchesAreSavedAndLoadedThroughTheWindow()
		{
			var movie = Fresh(10);
			FakeWindow window = new();
			Assert.AreEqual(1, (int) Ok(movie, "save_branch", "{\"note\":\"before the door\"}", window)["number"]!);
			Assert.AreEqual(2, (int) Ok(movie, "save_branch", "{}", window)["number"]!);
			CollectionAssert.AreEqual(new[] { "before the door", "" }, window.Notes);

			// the fake window saved nothing into the movie, so there is no branch to load
			StringAssert.Contains(Refused(movie, "load_branch", "{\"number\":1}", window), "no branches");
			movie.Branches.Add(new TasBranch { Frame = 4 });
			Ok(movie, "load_branch", "{\"number\":1}", window);
			Assert.AreEqual(1, window.Loaded);
			StringAssert.Contains(Refused(movie, "load_branch", "{\"number\":2}", window), "between 1 and 1");
			StringAssert.Contains(Refused(movie, "save_branch", "{}"), "TAStudio is not open");
		}

		[TestMethod]
		public void BeforeTheFirstChangeTheWorkIsKeptAsABranch()
		{
			var movie = Fresh(10);
			FakeWindow window = new();
			var answer = McpHostTools.Run(movie, window, "keep_work", "{}", out var isError);
			Assert.IsFalse(isError);
			Assert.AreEqual("branch 1", answer);
			StringAssert.StartsWith(window.Notes[0], "Before the agent's changes, ");

			// with nothing open there is nothing to keep, and that is not a failure
			McpHostTools.Run(null, null, "keep_work", "{}", out isError);
			Assert.IsFalse(isError);
			// a movie that cannot be kept is: the engine then changes nothing
			McpHostTools.Run(movie, null, "keep_work", "{}", out isError);
			Assert.IsTrue(isError);
		}

		[TestMethod]
		public void AToolThatIsNotTheFrontendsIsRefusedByName()
		{
			var answer = McpHostTools.Run(Fresh(3), null, "no_such_tool", "{}", out var isError);
			Assert.IsTrue(isError);
			StringAssert.Contains(answer, "no_such_tool");
		}
	}
}
