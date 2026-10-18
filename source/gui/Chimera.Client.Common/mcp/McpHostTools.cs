#nullable enable

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;

using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace Chimera.Client.Common
{
	/// <summary>
	/// What the MCP server's tools need of the window that holds the movie (TAStudio): the
	/// things that are not the movie's own, because a person must see them happen - the
	/// playback position moving, a branch appearing in the Branches box.
	/// </summary>
	public interface IMcpWindow
	{
		/// <summary>The frame the machine is on.</summary>
		int Frame { get; }

		/// <summary>Starts moving the playback position to a frame; it arrives some passes of the main loop later.</summary>
		void SeekTo(int frame);

		/// <summary>Saves a branch as the Branches box does and gives it a note. Its number, counted from 1.</summary>
		int SaveBranch(string note);

		/// <summary>Loads the branch of that number, counted from 1, as the Branches box does.</summary>
		void LoadBranch(int number);

		/// <summary>
		/// advance_until: has the engine take the condition from the tool's arguments. False with the
		/// reason when it is not understood or the value cannot be read.
		/// </summary>
		bool SetCondition(string argumentsJson, out string why);

		/// <summary>Whether that condition is met on the frame the machine is on.</summary>
		bool ConditionMet { get; }

		/// <summary>Starts playing forward to a frame, to stop earlier when the condition is met.</summary>
		void PlayUntil(int frame);
	}

	/// <summary>
	/// The MCP server's tools that the frontend runs (docs/mcp-server.md). The engine runs
	/// every tool that needs only the machine; the ones here need what the frontend holds -
	/// the open movie with its branches, markers and undo history - and are handed over by
	/// the engine (EngineMcp.Service). Every change goes through the calls TAStudio itself
	/// uses, so it shows in the piano roll and is a step in the undo history.
	/// What an edit MEANS is not decided here: the engine checks set_input, set_buttons,
	/// insert_frames and delete_frames against the movie and the machine's controls and hands
	/// over a plain step - these lines from this frame, this many frames inserted or deleted -
	/// with the name the step has in the undo history. This only applies it.
	/// Each tool returns the text for the agent, JSON by convention.
	/// </summary>
	public static class McpHostTools
	{
		public const string NoMovie = "No project is open in Chimera.";

		/// <summary>The most frames one call may write, which keeps a slip of the agent's small.</summary>
		private const int MostFrames = 100000;

		/// <summary>
		/// Runs one tool. <paramref name="isError"/> is true when the text is a refusal. Null is
		/// returned for the two tools whose answer comes later, once the window has stopped moving:
		/// seek (answered with <see cref="SeekAnswer"/>) and advance_until (<see cref="UntilAnswer"/>).
		/// </summary>
		/// <param name="movie">the open project's movie, or null when none is open</param>
		/// <param name="window">the window that holds it, or null when there is none</param>
		public static string? Run(ITasMovie? movie, IMcpWindow? window, string tool, string argumentsJson, out bool isError)
		{
			isError = false;
			if (tool is "keep_work") return KeepWork(movie, window, out isError);
			if (movie is null) return Refuse(NoMovie, out isError);
			JObject arguments;
			try
			{
				arguments = JObject.Parse(argumentsJson);
			}
			catch (JsonException)
			{
				return Refuse("The arguments are not a JSON object.", out isError);
			}
			try
			{
				switch (tool)
				{
					case "list_markers":
						return ListMarkers(movie);
					case "list_branches":
						return ListBranches(movie);
					case "set_input":
						return SetLines(movie, arguments, out isError);
					case "insert_frames":
						return InsertFrames(movie, arguments, out isError);
					case "delete_frames":
						return DeleteFrames(movie, arguments, out isError);
					case "undo":
						return Undo(movie, out isError);
					case "add_marker":
						return AddMarker(movie, arguments, out isError);
					case "seek":
						return Seek(movie, window, arguments, out isError);
					case "advance_until":
						return AdvanceUntil(movie, window, arguments, argumentsJson, out isError);
					case "save_branch":
						return SaveBranch(window, arguments, out isError);
					case "load_branch":
						return LoadBranch(movie, window, arguments, out isError);
					default:
						return Refuse($"Chimera does not run the tool '{tool}' yet.", out isError);
				}
			}
			finally
			{
				// whatever happened, the next edit made by hand is a step of its own
				movie.ChangeLog.EndBatch();
			}
		}

		/// <summary>The answer to a seek, once the window has stopped moving: where the machine is now.</summary>
		public static string SeekAnswer(IMcpWindow window, int asked, out bool isError)
		{
			isError = window.Frame != asked;
			return isError
				? $"The playback position stopped on frame {window.Frame}, not on frame {asked}: the seek was interrupted."
				: new JObject { ["frame"] = window.Frame }.ToString(Formatting.None);
		}

		/// <summary>
		/// The answer to advance_until, once the window has stopped: where the machine is, whether the
		/// condition is met there, and what stopped the playing.
		/// </summary>
		/// <param name="start">the frame the playing began on</param>
		/// <param name="target">the frame it was to stop on at the latest</param>
		public static string UntilAnswer(IMcpWindow window, ITasMovie movie, int start, int target)
		{
			var met = window.ConditionMet;
			return new JObject
			{
				["frame"] = window.Frame,
				["met"] = met,
				["frames_played"] = window.Frame - start,
				["stopped_at"] = met ? "condition"
					: window.Frame >= movie.InputLogLength ? "movie_end"
					: window.Frame >= target ? "max_frames"
					: "interrupted",
			}.ToString(Formatting.None);
		}

		private static string Refuse(string why, out bool isError)
		{
			isError = true;
			return why;
		}

		/// <summary>A whole number argument in [low, high]; <paramref name="fallback"/> when it is left out and one is given.</summary>
		private static bool Number(JObject arguments, string name, int low, int high, int? fallback, out int value)
		{
			value = fallback ?? 0;
			var token = arguments[name];
			if (token is null || token.Type is JTokenType.Null) return fallback is not null;
			if (token.Type is not JTokenType.Integer) return false;
			var asked = (long) token;
			if (asked < low || asked > high) return false;
			value = (int) asked;
			return true;
		}

		/// <summary>Before the agent's first change: a branch of the movie as it is now.</summary>
		private static string KeepWork(ITasMovie? movie, IMcpWindow? window, out bool isError)
		{
			isError = false;
			if (movie is null) return "no project is open, so there is no work to keep";
			if (window is null) return Refuse("TAStudio is not open, so a branch could not be saved", out isError);
			var number = window.SaveBranch($"Before the agent's changes, {DateTime.Now.ToString("yyyy-MM-dd HH:mm", CultureInfo.InvariantCulture)}");
			return $"branch {number}";
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

		private static string Done(ITasMovie movie, JObject facts)
		{
			facts["movie_length"] = movie.InputLogLength;
			return facts.ToString(Formatting.None);
		}

		/// <summary>The name the engine gave the step, for the undo history.</summary>
		private static string Label(JObject step) => step["label"] is { Type: JTokenType.String } label ? (string) label! : "Agent";

		/// <summary>Lines of input written from a frame on, past the movie's end too. The engine has checked every line.</summary>
		private static string SetLines(ITasMovie movie, JObject step, out bool isError)
		{
			isError = false;
			if (!Number(step, "frame", 0, int.MaxValue - MostFrames, null, out var frame)
				|| step["entries"] is not JArray { Count: > 0 } entries || entries.Any(static e => e.Type is not JTokenType.String))
			{
				return Refuse("The step to apply is not one the engine makes.", out isError);
			}
			movie.ChangeLog.BeginNewBatch(Label(step));
			// one invalidation for the whole step, not one for each of up to
			// ten thousand lines (each a pass over the lag log, the history and
			// TAStudio's visible rows)
			movie.SingleInvalidation(() =>
			{
				for (var i = 0; i < entries.Count; i++) movie.PokeFrame(frame + i, (string) entries[i]!);
			});
			return Done(movie, new JObject { ["frame"] = frame, ["count"] = entries.Count });
		}

		private static string InsertFrames(ITasMovie movie, JObject step, out bool isError)
		{
			isError = false;
			if (!Number(step, "frame", 0, movie.InputLogLength, null, out var frame) || !Number(step, "count", 1, MostFrames, null, out var count))
			{
				return Refuse("The step to apply is not one the engine makes.", out isError);
			}
			movie.ChangeLog.BeginNewBatch(Label(step));
			movie.InsertEmptyFrame(frame, count);
			return Done(movie, new JObject { ["frame"] = frame, ["inserted"] = count });
		}

		private static string DeleteFrames(ITasMovie movie, JObject step, out bool isError)
		{
			isError = false;
			if (!Number(step, "frame", 0, Math.Max(0, movie.InputLogLength - 1), null, out var frame)
				|| !Number(step, "count", 1, Math.Max(1, movie.InputLogLength - frame), null, out var count))
			{
				return Refuse("The step to apply is not one the engine makes.", out isError);
			}
			movie.ChangeLog.BeginNewBatch(Label(step));
			movie.RemoveFrames(frame, frame + count);
			return Done(movie, new JObject { ["frame"] = frame, ["deleted"] = count });
		}

		private static string Undo(ITasMovie movie, out bool isError)
		{
			isError = false;
			if (!movie.ChangeLog.CanUndo) return Refuse("There is nothing to undo.", out isError);
			movie.ChangeLog.Undo();
			return Done(movie, new JObject { ["undone"] = true });
		}

		private static string AddMarker(ITasMovie movie, JObject arguments, out bool isError)
		{
			isError = false;
			if (!Number(arguments, "frame", 0, int.MaxValue - 1, null, out var frame)) return Refuse("'frame' must be a frame number, 0 or more.", out isError);
			var text = arguments["text"] is { Type: JTokenType.String } given ? (string) given! : "";
			movie.Markers.Add(frame, text);
			return new JObject { ["frame"] = frame, ["text"] = text }.ToString(Formatting.None);
		}

		private static string? Seek(ITasMovie movie, IMcpWindow? window, JObject arguments, out bool isError)
		{
			isError = false;
			if (window is null) return Refuse("TAStudio is not open, so the playback position cannot be moved.", out isError);
			if (!Number(arguments, "frame", 0, movie.InputLogLength, null, out var frame))
			{
				return Refuse($"'frame' must be between 0 and the movie's length, {movie.InputLogLength}.", out isError);
			}
			if (window.Frame == frame) return new JObject { ["frame"] = frame }.ToString(Formatting.None);
			window.SeekTo(frame);
			// answered when the window has arrived: McpHostTools.SeekAnswer
			return null;
		}

		private static string? AdvanceUntil(ITasMovie movie, IMcpWindow? window, JObject arguments, string argumentsJson, out bool isError)
		{
			isError = false;
			if (window is null) return Refuse("TAStudio is not open, so the movie cannot be played.", out isError);
			if (!Number(arguments, "max_frames", 1, MostFrames, 600, out var most)) return Refuse($"'max_frames' must be between 1 and {MostFrames}.", out isError);
			// what the condition means is the engine's: it reads the arguments itself
			if (!window.SetCondition(argumentsJson, out var why)) return Refuse(why, out isError);
			var start = window.Frame;
			if (window.ConditionMet) return UntilAnswer(window, movie, start, start);
			if (start >= movie.InputLogLength)
			{
				return Refuse($"The playback position is at the movie's end, frame {movie.InputLogLength}, so there is nothing left to play.", out isError);
			}
			window.PlayUntil(Math.Min(movie.InputLogLength, start + most));
			// answered when the window has stopped: McpHostTools.UntilAnswer
			return null;
		}

		private static string SaveBranch(IMcpWindow? window, JObject arguments, out bool isError)
		{
			isError = false;
			if (window is null) return Refuse("TAStudio is not open, so a branch cannot be saved.", out isError);
			var note = arguments["note"] is { Type: JTokenType.String } given ? (string) given! : "";
			return new JObject { ["number"] = window.SaveBranch(note) }.ToString(Formatting.None);
		}

		private static string LoadBranch(ITasMovie movie, IMcpWindow? window, JObject arguments, out bool isError)
		{
			isError = false;
			if (window is null) return Refuse("TAStudio is not open, so a branch cannot be loaded.", out isError);
			if (!Number(arguments, "number", 1, movie.Branches.Count, null, out var number))
			{
				return Refuse(movie.Branches.Count is 0
					? "The project has no branches."
					: $"'number' must be between 1 and {movie.Branches.Count}, the branches list_branches gives.", out isError);
			}
			window.LoadBranch(number);
			return Done(movie, new JObject { ["number"] = number, ["frame"] = window.Frame });
		}
	}
}
