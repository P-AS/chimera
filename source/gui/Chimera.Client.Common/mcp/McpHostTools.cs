#nullable enable

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
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

		/// <summary>
		/// capture_series: plays the movie to each of these frames in turn, and on each has the engine take its
		/// picture. The call is answered after the last (<see cref="McpHostTools.PicturesAnswer"/>).
		/// </summary>
		void TakePictures(IReadOnlyList<int> frames);
	}

	/// <summary>
	/// What the MCP server's tools need of Chimera's main window, with or without a project open:
	/// pausing and running the machine, saving and closing the project, and where things are kept.
	/// </summary>
	public interface IMcpChimera
	{
		/// <summary>The frame the machine is on, or -1 when no machine is open.</summary>
		int Frame { get; }

		bool Paused { get; }

		void Pause();

		void Resume();

		/// <summary>What server_info answers: this build and the folders it uses.</summary>
		JObject ServerInfo();

		/// <summary>The folder a project saved by a relative path goes in: the Movies folder.</summary>
		string ProjectsFolder { get; }

		/// <summary>Saves the open project to that file, as File &gt; Save Project As does once a file is chosen. Null when saved, else why not.</summary>
		string? SaveProject(string path);

		/// <summary>Closes the open project and its machine without asking anything. Null when closed, else why not.</summary>
		string? CloseProject();

		/// <summary>create_project: a project made as the New Project wizard makes it, from these answers, and opened.</summary>
		McpProjectMade CreateProject(McpProjectRequest request);

		/// <summary>Every Lua function, as the Lua Console documents them; null when Lua cannot run here.</summary>
		IReadOnlyList<LibraryFunction>? LuaFunctions();

		/// <summary>run_lua: code run as a line typed into the Lua Console is; what it returned, the error it raised, and what the console printed.</summary>
		(object[] Results, string? Error, string Output) RunLua(string code);

		/// <summary>lua_script: a script file started in the Lua Console, or stopped; whether it runs now, and what the console printed.</summary>
		(bool Running, string Output) LuaScript(string path, bool stop);
	}

	/// <summary>What an agent's create_project asks for (docs/mcp-server.md).</summary>
	public sealed class McpProjectRequest
	{
		public string Core { get; init; } = "";

		public string? System { get; init; }

		/// <summary>Files by slot.</summary>
		public IReadOnlyList<(string Slot, string Path)> Files { get; init; } = [ ];

		/// <summary>Files with no slot named: each goes where its kind of file goes.</summary>
		public IReadOnlyList<string> Games { get; init; } = [ ];

		public IReadOnlyDictionary<string, object> Settings { get; init; } = new Dictionary<string, object>();

		/// <summary>Firmware files by id.</summary>
		public IReadOnlyList<(string Id, string Path)> Firmware { get; init; } = [ ];

		/// <summary>A folder to look for firmware in, beside where Chimera looks.</summary>
		public string? FirmwareFolder { get; init; }

		/// <summary>The open project may lose changes that are not saved.</summary>
		public bool DiscardUnsaved { get; init; }
	}

	/// <summary>What create_project made: the project opened, or why not.</summary>
	public sealed class McpProjectMade
	{
		/// <summary>What the agent is told of the project opened; null when none was.</summary>
		public JObject? Opened { get; init; }

		public string? Refusal { get; init; }

		/// <summary>When firmware was missing: each file, with the files that would do; and where it was looked for.</summary>
		public JArray? Missing { get; init; }

		public JArray? Searched { get; init; }
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

		private const string NoChimera = "Chimera's main window cannot be asked.";

		/// <summary>What a project file is called.</summary>
		private const string ProjectExtension = ".chimeraProject";

		/// <summary>The most frames one call may write, which keeps a slip of the agent's small.</summary>
		private const int MostFrames = 100000;

		/// <summary>
		/// Runs one tool. <paramref name="isError"/> is true when the text is a refusal. Null is
		/// returned for the two tools whose answer comes later, once the window has stopped moving:
		/// seek (answered with <see cref="SeekAnswer"/>) and advance_until (<see cref="UntilAnswer"/>).
		/// </summary>
		/// <param name="movie">the open project's movie, or null when none is open</param>
		/// <param name="window">the window that holds it, or null when there is none</param>
		/// <param name="chimera">Chimera's main window, for the tools that do not need a project; null in a test that has none</param>
		public static string? Run(ITasMovie? movie, IMcpWindow? window, string tool, string argumentsJson, out bool isError, IMcpChimera? chimera = null)
		{
			isError = false;
			if (tool is "keep_work") return KeepWork(movie, window, out isError);
			JObject arguments;
			try
			{
				arguments = JObject.Parse(argumentsJson);
			}
			catch (JsonException)
			{
				return Refuse("The arguments are not a JSON object.", out isError);
			}
			// the tools of the main window: they need no project, or only the project as a file
			switch (tool)
			{
				case "server_info":
					return chimera is null ? Refuse(NoChimera, out isError) : chimera.ServerInfo().ToString(Formatting.None);
				case "pause":
					return PauseOrResume(chimera, pause: true, out isError);
				case "resume":
					return PauseOrResume(chimera, pause: false, out isError);
				case "save_project":
					return SaveProject(movie, chimera, arguments, out isError);
				case "close_tastudio":
					return CloseProject(movie, chimera, arguments, out isError);
				case "create_project":
					return CreateProject(movie, chimera, arguments, out isError);
				case "lua_functions":
					return LuaFunctions(chimera, arguments, out isError);
				case "run_lua":
					return RunLua(chimera, arguments, out isError);
				case "lua_script":
					return LuaScript(chimera, arguments, out isError);
			}
			if (movie is null) return Refuse(NoMovie, out isError);
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
					case "capture_series":
						return CaptureSeries(movie, window, arguments, out isError);
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

		private static string PauseOrResume(IMcpChimera? chimera, bool pause, out bool isError)
		{
			isError = false;
			if (chimera is null) return Refuse(NoChimera, out isError);
			if (chimera.Frame < 0) return Refuse(NoMovie, out isError);
			if (pause) chimera.Pause();
			else chimera.Resume();
			return new JObject { ["paused"] = chimera.Paused, ["frame"] = chimera.Frame }.ToString(Formatting.None);
		}

		/// <summary>
		/// save_project: where it was saved or opened, or the path given - which is not a file already there
		/// unless 'overwrite' says so, since nothing asks. A relative path is in the Movies folder, and a
		/// path without the project extension gets it.
		/// </summary>
		private static string SaveProject(ITasMovie? movie, IMcpChimera? chimera, JObject arguments, out bool isError)
		{
			isError = false;
			if (chimera is null) return Refuse(NoChimera, out isError);
			if (movie is null) return Refuse(NoMovie, out isError);
			var own = string.IsNullOrEmpty(movie.Filename) ? null : Path.GetFullPath(movie.Filename);
			string path;
			if (arguments["path"] is { Type: JTokenType.String } given && ((string) given!).Trim() is { Length: not 0 } asked)
			{
				if (!asked.EndsWith(ProjectExtension, StringComparison.OrdinalIgnoreCase)) asked += ProjectExtension;
				try
				{
					path = Path.GetFullPath(Path.IsPathRooted(asked) ? asked : Path.Combine(chimera.ProjectsFolder, asked));
				}
				catch (Exception ex) when (ex is ArgumentException or NotSupportedException or PathTooLongException)
				{
					return Refuse($"'{asked}' is not a path a file can have: {ex.Message}", out isError);
				}
			}
			else if (own is not null && File.Exists(own))
			{
				path = own;
			}
			else
			{
				return Refuse("This project has not been saved yet: give 'path'.", out isError);
			}
			var overwrite = arguments["overwrite"] is { Type: JTokenType.Boolean } flag && (bool) flag!;
			if (!overwrite && !string.Equals(path, own, StringComparison.OrdinalIgnoreCase) && File.Exists(path))
			{
				return Refuse($"A file is already at '{path}', so the project was not saved. Give overwrite: true to replace it.", out isError);
			}
			if (Path.GetDirectoryName(path) is { } folder && !Directory.Exists(folder))
			{
				return Refuse($"The folder '{folder}' does not exist, so the project was not saved.", out isError);
			}
			if (chimera.SaveProject(path) is { } failed) return Refuse($"The project was not saved: {failed}", out isError);
			return new JObject
			{
				["saved"] = path,
				["movie_length"] = movie.InputLogLength,
				["unsaved_changes"] = movie.Changes,
			}.ToString(Formatting.None);
		}

		/// <summary>close_tastudio: a project with unsaved changes is closed only when the agent says they may be lost.</summary>
		private static string CloseProject(ITasMovie? movie, IMcpChimera? chimera, JObject arguments, out bool isError)
		{
			isError = false;
			if (chimera is null) return Refuse(NoChimera, out isError);
			if (movie is null) return Refuse(NoMovie, out isError);
			var discard = arguments["discard_unsaved"] is { Type: JTokenType.Boolean } flag && (bool) flag!;
			var lost = movie.Changes;
			if (lost && !discard)
			{
				return Refuse("The project has changes that are not saved, so it was not closed. Save it with save_project, "
					+ "or give discard_unsaved: true to close it without them.", out isError);
			}
			// what is thrown away is thrown away here, so closing asks nothing
			if (lost) movie.ClearChanges();
			if (chimera.CloseProject() is { } failed) return Refuse($"The project was not closed: {failed}", out isError);
			JObject closed = new() { ["closed"] = true, ["machine_open"] = chimera.Frame >= 0 };
			if (lost) closed["note"] = "The changes that were not saved are lost.";
			return closed.ToString(Formatting.None);
		}

		/// <summary>
		/// create_project: the agent's answers read and handed to the window, which makes the project as its
		/// New Project wizard does. A project open with changes that are not saved stays, unless the agent said
		/// they may be lost. Missing firmware is an answer of its own: what is missing, and where it was looked for.
		/// </summary>
		private static string CreateProject(ITasMovie? movie, IMcpChimera? chimera, JObject arguments, out bool isError)
		{
			isError = false;
			if (chimera is null) return Refuse(NoChimera, out isError);
			if (arguments["core"] is not { Type: JTokenType.String } core || ((string) core!).Trim().Length is 0)
			{
				return Refuse("Give 'core': a core's name or file, as list_cores gives them.", out isError);
			}
			var discard = arguments["discard_unsaved"] is { Type: JTokenType.Boolean } flag && (bool) flag!;
			if (movie is { Changes: true } && !discard)
			{
				return Refuse("The project open now has changes that are not saved, so no other was made. Save it with "
					+ "save_project, or give discard_unsaved: true to let them go.", out isError);
			}
			List<(string, string)> files = new();
			if (arguments["files"] is { } slots)
			{
				if (slots is not JObject bySlot || bySlot.Properties().Any(static p => p.Value is not JArray list || list.Any(static f => f.Type is not JTokenType.String)))
				{
					return Refuse("'files' is an object from slots to lists of paths, such as {\"rom\": [\"game.nes\"]}.", out isError);
				}
				foreach (var slot in bySlot.Properties()) files.AddRange(((JArray) slot.Value).Select(f => (slot.Name, (string) f!)));
			}
			List<string> games = new();
			switch (arguments["game"])
			{
				case null:
					break;
				case { Type: JTokenType.String } one:
					games.Add((string) one!);
					break;
				case JArray list when list.All(static g => g.Type is JTokenType.String):
					games.AddRange(list.Select(static g => (string) g!));
					break;
				default:
					return Refuse("'game' is a path, or a list of paths.", out isError);
			}
			Dictionary<string, object> settings = new();
			if (arguments["settings"] is { } given)
			{
				if (given is not JObject values || values.Properties().Any(static p => p.Value is not JValue { Value: not null }))
				{
					return Refuse("'settings' is an object from settings' names to their values.", out isError);
				}
				foreach (var pair in values.Properties()) settings[pair.Name] = ((JValue) pair.Value).Value!;
			}
			List<(string, string)> firmware = new();
			if (arguments["firmware"] is { } byId)
			{
				if (byId is not JObject ids || ids.Properties().Any(static p => p.Value.Type is not JTokenType.String))
				{
					return Refuse("'firmware' is an object from firmware ids to paths.", out isError);
				}
				firmware.AddRange(ids.Properties().Select(static p => (p.Name, (string) p.Value!)));
			}
			var folder = arguments["firmware_folder"] is { Type: JTokenType.String } dir ? (string) dir! : null;
			if (folder is not null && !Directory.Exists(folder)) return Refuse($"There is no folder at '{folder}'.", out isError);

			var made = chimera.CreateProject(new()
			{
				Core = ((string) core!).Trim(),
				System = arguments["system"] is { Type: JTokenType.String } system ? (string) system! : null,
				Files = files,
				Games = games,
				Settings = settings,
				Firmware = firmware,
				FirmwareFolder = folder,
				DiscardUnsaved = discard,
			});
			if (made.Missing is { } missing)
			{
				isError = true;
				return new JObject
				{
					["error"] = "missing_firmware",
					["message"] = made.Refusal,
					["missing"] = missing,
					["searched"] = made.Searched ?? new JArray(),
				}.ToString(Formatting.None);
			}
			if (made.Opened is null) return Refuse(made.Refusal ?? "No project was made.", out isError);
			return made.Opened.ToString(Formatting.None);
		}

		private const string NoLua = "Lua cannot run in this Chimera: its library could not be loaded.";

		/// <summary>
		/// lua_functions: the functions Chimera's Lua offers, as the Lua Console documents them, by library or by
		/// words in their names and descriptions (issue 355: every Lua function is offered through the server).
		/// </summary>
		private static string LuaFunctions(IMcpChimera? chimera, JObject arguments, out bool isError)
		{
			isError = false;
			if (chimera is null) return Refuse(NoChimera, out isError);
			if (chimera.LuaFunctions() is not { } functions) return Refuse(NoLua, out isError);
			var library = arguments["library"] is { Type: JTokenType.String } given ? (string) given! : null;
			var search = arguments["search"] is { Type: JTokenType.String } words ? (string) words! : null;
			if (!Number(arguments, "limit", 1, 1000, 200, out var limit)) return Refuse("'limit' must be between 1 and 1000.", out isError);
			var matching = functions
				.Where(f => library is null || string.Equals(f.Library, library, StringComparison.OrdinalIgnoreCase))
				.Where(f => search is null || $"{f.Library}.{f.Name} {f.Description}".IndexOf(search, StringComparison.OrdinalIgnoreCase) >= 0)
				.OrderBy(static f => f.Library, StringComparer.Ordinal).ThenBy(static f => f.Name, StringComparer.Ordinal)
				.ToList();
			JArray listed = new();
			foreach (var function in matching.Take(limit))
			{
				JObject item = new()
				{
					["name"] = $"{function.Library}.{function.Name}",
					["parameters"] = function.ParameterList,
					["returns"] = function.ReturnType,
					["description"] = function.Description,
				};
				if (function.Example is { Length: not 0 } example) item["example"] = example;
				if (function.IsUnavailable) item["unavailable"] = "it raises an error in every core";
				if (function.IsDeprecated) item["deprecated"] = true;
				listed.Add(item);
			}
			return new JObject
			{
				["total"] = matching.Count,
				["libraries"] = new JArray(functions.Select(static f => f.Library).Distinct().OrderBy(static l => l, StringComparer.Ordinal)),
				["functions"] = listed,
			}.ToString(Formatting.None);
		}

		/// <summary>
		/// run_lua: the code run as the Lua Console's input line runs it, which a person sees there. What it
		/// returned comes back as JSON; a table as an object, or as a list when its keys are 1 to n.
		/// </summary>
		private static string RunLua(IMcpChimera? chimera, JObject arguments, out bool isError)
		{
			isError = false;
			if (chimera is null) return Refuse(NoChimera, out isError);
			if (arguments["code"] is not { Type: JTokenType.String } given || ((string) given!).Trim().Length is 0)
			{
				return Refuse("Give 'code': the Lua to run, such as \"return emu.framecount()\".", out isError);
			}
			if (chimera.LuaFunctions() is null) return Refuse(NoLua, out isError);
			var (results, error, output) = chimera.RunLua((string) given!);
			if (error is not null)
			{
				isError = true;
				return new JObject { ["error"] = error, ["output"] = output }.ToString(Formatting.None);
			}
			return new JObject
			{
				["results"] = new JArray(results.Select(static r => LuaValue(r, 0))),
				["output"] = output,
			}.ToString(Formatting.None);
		}

		/// <summary>lua_script: a script file started in the Lua Console, which then runs frame after frame, or stopped.</summary>
		private static string LuaScript(IMcpChimera? chimera, JObject arguments, out bool isError)
		{
			isError = false;
			if (chimera is null) return Refuse(NoChimera, out isError);
			if (arguments["path"] is not { Type: JTokenType.String } given || ((string) given!).Trim().Length is 0)
			{
				return Refuse("Give 'path': the .lua file to run.", out isError);
			}
			var path = (string) given!;
			var stop = arguments["stop"] is { Type: JTokenType.Boolean } flag && (bool) flag!;
			if (!stop && !File.Exists(path)) return Refuse($"There is no file at '{path}'.", out isError);
			if (chimera.LuaFunctions() is null) return Refuse(NoLua, out isError);
			var (running, output) = chimera.LuaScript(path, stop);
			return new JObject { ["script"] = path, ["running"] = running, ["output"] = output }.ToString(Formatting.None);
		}

		/// <summary>The most tables within tables a Lua value is followed into.</summary>
		private const int LuaDepth = 8;

		/// <summary>A value Lua returned, as JSON.</summary>
		public static JToken LuaValue(object? value, int depth)
		{
			switch (value)
			{
				case null:
					return JValue.CreateNull();
				case bool or string or long or int:
					return new JValue(value);
				case double number:
					// Lua's numbers are doubles; a whole one is said as one
					return number == Math.Floor(number) && Math.Abs(number) < 9e15 ? new JValue((long) number) : new JValue(number);
				case NLua.LuaTable table:
				{
					if (depth >= LuaDepth) return "(a table, too deep to show)";
					var keys = table.Keys.Cast<object>().ToList();
					var whole = keys.Select(static k => k is long l ? l : k is double d && d == Math.Floor(d) ? (long) d : 0L).ToList();
					if (keys.Count is not 0 && whole.OrderBy(static k => k).SequenceEqual(Enumerable.Range(1, keys.Count).Select(static k => (long) k)))
					{
						return new JArray(whole.OrderBy(static k => k).Select(k => LuaValue(table[keys[whole.IndexOf(k)]], depth + 1)));
					}
					// by name: Lua keeps a table's keys in no order, and an answer should read the same each time
					JObject fields = new();
					foreach (var key in keys.OrderBy(static k => Convert.ToString(k, CultureInfo.InvariantCulture), StringComparer.Ordinal))
					{
						fields[Convert.ToString(key, CultureInfo.InvariantCulture) ?? ""] = LuaValue(table[key], depth + 1);
					}
					return fields;
				}
				case NLua.LuaFunction:
					return "(a function)";
				default:
					return Convert.ToString(value, CultureInfo.InvariantCulture) ?? "";
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

		/// <summary>
		/// capture_series, as the engine planned it: the frames to take pictures of. Frames past the movie's end
		/// are added first, empty and in one step of the undo history, so that playing reaches every one.
		/// </summary>
		private static string? CaptureSeries(ITasMovie movie, IMcpWindow? window, JObject step, out bool isError)
		{
			isError = false;
			if (window is null) return Refuse("TAStudio is not open, so the movie cannot be played.", out isError);
			if (step["frames"] is not JArray { Count: > 0 } list || list.Any(static f => f.Type is not JTokenType.Integer))
			{
				return Refuse("The step to apply is not one the engine makes.", out isError);
			}
			var frames = list.Select(static f => (int) f).ToList();
			var last = frames.Max();
			if (last > movie.InputLogLength)
			{
				movie.ChangeLog.BeginNewBatch(Label(step));
				movie.InsertEmptyFrame(movie.InputLogLength, last - movie.InputLogLength);
			}
			window.TakePictures(frames);
			// answered after the last picture: McpHostTools.PicturesAnswer
			return null;
		}

		/// <summary>The answer to capture_series once the last picture is taken; the engine adds the pictures.</summary>
		public static string PicturesAnswer(IMcpWindow window, ITasMovie movie)
			=> new JObject { ["frame"] = window.Frame, ["movie_length"] = movie.InputLogLength }.ToString(Formatting.None);

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
