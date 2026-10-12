#nullable enable

using System.Globalization;

using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace Chimera.Client.Common
{
	/// <summary>
	/// The MCP server's tools that the frontend runs (docs/mcp-server.md). The engine runs
	/// every tool that needs only the machine; the ones here need what the frontend holds -
	/// the open movie with its branches and markers - and are handed over by the engine
	/// (EngineMcp.Service). Each returns the text for the agent, JSON by convention.
	/// </summary>
	public static class McpHostTools
	{
		public const string NoMovie = "No project is open in Chimera.";

		/// <summary>Runs one tool. <paramref name="isError"/> is true when the text is a refusal.</summary>
		/// <param name="movie">the open project's movie, or null when none is open</param>
		public static string Run(ITasMovie? movie, string tool, string argumentsJson, out bool isError)
		{
			isError = false;
			if (movie is null)
			{
				isError = true;
				return NoMovie;
			}
			switch (tool)
			{
				case "list_markers":
					return ListMarkers(movie);
				case "list_branches":
					return ListBranches(movie);
				default:
					isError = true;
					return $"Chimera does not run the tool '{tool}' yet.";
			}
		}

		private static string ListMarkers(ITasMovie movie)
		{
			JArray markers = new();
			foreach (var marker in movie.Markers)
			{
				markers.Add(new JObject
				{
					["frame"] = marker.Frame,
					["text"] = marker.Message ?? "",
					// every movie has three of its own (the run's start, its last input, its end)
					["placed_by_user"] = !marker.IsPermanent,
				});
			}
			return new JObject { ["markers"] = markers }.ToString(Formatting.None);
		}

		private static string ListBranches(ITasMovie movie)
		{
			JArray branches = new();
			for (var i = 0; i < movie.Branches.Count; i++)
			{
				var branch = movie.Branches[i];
				branches.Add(new JObject
				{
					// numbered as the Branches box numbers them, from 1
					["number"] = i + 1,
					["frame"] = branch.Frame,
					["taken"] = branch.TimeStamp.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture),
					["note"] = branch.UserText ?? "",
					["current"] = movie.Branches.Current == i,
				});
			}
			return new JObject { ["branches"] = branches }.ToString(Formatting.None);
		}
	}
}
