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

			var answer = JObject.Parse(McpHostTools.Run(movie, "list_markers", "{}", out var isError));

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

			var answer = JObject.Parse(McpHostTools.Run(movie, "list_branches", "{}", out var isError));

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
			var answer = JObject.Parse(McpHostTools.Run(TasMovieTests.MakeMovie(3), "list_branches", "{}", out var isError));
			Assert.IsFalse(isError);
			Assert.AreEqual(0, ((JArray) answer["branches"]!).Count);
		}

		[TestMethod]
		public void WithNoProjectOpenEveryToolSaysSo()
		{
			foreach (var tool in new[] { "list_markers", "list_branches" })
			{
				var answer = McpHostTools.Run(null, tool, "{}", out var isError);
				Assert.IsTrue(isError, tool);
				Assert.AreEqual(McpHostTools.NoMovie, answer, tool);
			}
		}

		[TestMethod]
		public void AToolThatIsNotTheFrontendsIsRefusedByName()
		{
			var answer = McpHostTools.Run(TasMovieTests.MakeMovie(3), "no_such_tool", "{}", out var isError);
			Assert.IsTrue(isError);
			StringAssert.Contains(answer, "no_such_tool");
		}
	}
}
