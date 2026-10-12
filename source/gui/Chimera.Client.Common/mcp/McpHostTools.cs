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
						return SetInput(movie, arguments, out isError);
					case "set_buttons":
						return SetButtons(movie, arguments, out isError);
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

		private static string SetInput(ITasMovie movie, JObject arguments, out bool isError)
		{
			isError = false;
			if (!Number(arguments, "frame", 0, int.MaxValue - MostFrames, null, out var frame)) return Refuse("'frame' must be a frame number, 0 or more.", out isError);
			if (arguments["entries"] is not JArray { Count: > 0 and <= 1000 } list || list.Any(static e => e.Type is not JTokenType.String))
			{
				return Refuse("'entries' must be between 1 and 1000 lines of input, as get_input returns them.", out isError);
			}
			var entries = list.Select(static e => (string) e!).ToList();
			// Every line is checked before one is written: it must have the shape of this
			// movie's lines, which is the shape of the empty one.
			var shape = LogEntryGenerator.EmptyEntry(movie.Session.MovieController);
			for (var i = 0; i < entries.Count; i++)
			{
				if (!SameShape(entries[i], shape))
				{
					return Refuse($"Line {i + 1} of 'entries' does not have the shape of this movie's input lines. An empty one is: {shape}", out isError);
				}
			}
			movie.ChangeLog.BeginNewBatch($"Agent: set input, frames {frame} to {frame + entries.Count - 1}");
			for (var i = 0; i < entries.Count; i++) movie.PokeFrame(frame + i, entries[i]);
			return Done(movie, new JObject { ["frame"] = frame, ["count"] = entries.Count });
		}

		/// <summary>
		/// Whether a line has the columns of the movie's own: the same length, the bars in the same
		/// places, and where the empty line has a digit or a sign (an axis), only those or spaces.
		/// </summary>
		private static bool SameShape(string entry, string shape)
		{
			if (entry.Length != shape.Length) return false;
			for (var i = 0; i < shape.Length; i++)
			{
				if ((shape[i] is '|') != (entry[i] is '|')) return false;
				if (shape[i] is '.' && entry[i] is ' ' or ',') return false;
			}
			return true;
		}

		private static string SetButtons(ITasMovie movie, JObject arguments, out bool isError)
		{
			isError = false;
			if (!Number(arguments, "frame", 0, int.MaxValue - MostFrames, null, out var frame)) return Refuse("'frame' must be a frame number, 0 or more.", out isError);
			if (!Number(arguments, "count", 1, MostFrames, 1, out var count)) return Refuse($"'count' must be between 1 and {MostFrames}.", out isError);
			var definition = movie.Session.MovieController.Definition;
			List<(string Name, bool Pressed)> buttons = new();
			List<(string Name, int Value)> axes = new();
			if (arguments["buttons"] is JObject askedButtons)
			{
				foreach (var asked in askedButtons.Properties())
				{
					if (!definition.BoolButtons.Contains(asked.Name)) return Refuse($"There is no button called '{asked.Name}'. 'controls' names them.", out isError);
					if (asked.Value.Type is not JTokenType.Boolean) return Refuse($"The button '{asked.Name}' must be true (pressed) or false (released).", out isError);
					buttons.Add((asked.Name, (bool) asked.Value));
				}
			}
			if (arguments["axes"] is JObject askedAxes)
			{
				foreach (var asked in askedAxes.Properties())
				{
					if (!definition.Axes.ContainsKey(asked.Name)) return Refuse($"There is no axis called '{asked.Name}'. 'controls' names them.", out isError);
					if (asked.Value.Type is not JTokenType.Integer) return Refuse($"The axis '{asked.Name}' must be a whole number.", out isError);
					axes.Add((asked.Name, (int) asked.Value));
				}
			}
			if (buttons.Count is 0 && axes.Count is 0) return Refuse("Name at least one button in 'buttons' or one axis in 'axes'.", out isError);
			movie.ChangeLog.BeginNewBatch($"Agent: set controls, frames {frame} to {frame + count - 1}");
			foreach (var (name, pressed) in buttons) movie.SetBoolStates(frame, count, name, pressed);
			foreach (var (name, value) in axes) movie.SetAxisStates(frame, count, name, value);
			return Done(movie, new JObject { ["frame"] = frame, ["count"] = count });
		}

		private static string InsertFrames(ITasMovie movie, JObject arguments, out bool isError)
		{
			isError = false;
			if (!Number(arguments, "frame", 0, movie.InputLogLength, null, out var frame)) return Refuse($"'frame' must be between 0 and the movie's length, {movie.InputLogLength}.", out isError);
			if (!Number(arguments, "count", 1, MostFrames, 1, out var count)) return Refuse($"'count' must be between 1 and {MostFrames}.", out isError);
			movie.ChangeLog.BeginNewBatch($"Agent: insert {count} frames at {frame}");
			movie.InsertEmptyFrame(frame, count);
			return Done(movie, new JObject { ["frame"] = frame, ["inserted"] = count });
		}

		private static string DeleteFrames(ITasMovie movie, JObject arguments, out bool isError)
		{
			isError = false;
			if (!Number(arguments, "frame", 0, Math.Max(0, movie.InputLogLength - 1), null, out var frame)) return Refuse($"'frame' must be a frame of the movie, which has {movie.InputLogLength}.", out isError);
			if (!Number(arguments, "count", 1, MostFrames, 1, out var count)) return Refuse($"'count' must be between 1 and {MostFrames}.", out isError);
			if (movie.InputLogLength is 0) return Refuse("The movie has no frames.", out isError);
			count = Math.Min(count, movie.InputLogLength - frame);
			movie.ChangeLog.BeginNewBatch($"Agent: delete {count} frames at {frame}");
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
