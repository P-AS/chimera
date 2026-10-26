using System.Collections.Generic;

using Chimera.Emulation.Common.Engine;

using Newtonsoft.Json.Linq;

namespace Chimera.Emulation.Common.Waterbox
{
	/// <summary>
	/// The project a core is asked about before the real one is made: its
	/// SuggestSettings answer (suggested settings, the game's own settings, the
	/// firmware this game needs), the share of a game's code a compile session
	/// compiles, and what a movie made elsewhere amounts to. A game runs only
	/// in a project (user-decided, 2026-10-26), so these questions are asked of
	/// one too: a project of the files picked so far, in their slots, with the
	/// settings chosen so far.
	///
	/// A game is often more than the file that names it: a disc is a .cue and
	/// the track files it lists, which the engine adds to a project beside the
	/// cue. A core asked about the .cue alone could not read the disc - a
	/// Saturn core cannot tell which region's BIOS the disc needs.
	/// </summary>
	public static class GameFilesForCore
	{
		/// <summary>
		/// A project of the package's core holding the files given, each in its
		/// slot, and the settings given. Every file is hashed as it is added,
		/// as the New Project window's own Create does (the engine keeps the
		/// hash, so the real project costs no second read). The core is named
		/// but not pinned by hash: the package given is the one asked.
		/// </summary>
		/// <exception cref="System.InvalidOperationException">the package cannot be read, or a file cannot be added</exception>
		public static EngineProject ProjectFor(string packagePath, IEnumerable<(string Slot, string Name, string Path)> files, string? settingsJson)
		{
			string coreName = "", version = "";
			using (var package = EnginePackage.Open(packagePath)
				?? throw new System.InvalidOperationException($"{packagePath}: not a core package"))
			{
				if (package.EntryText(WaterboxCoreFactory.ConfigFileName) is { } configText)
				{
					var config = JObject.Parse(configText);
					coreName = (string?)config["coreName"] ?? "";
					version = (string?)config["version"] ?? "";
				}
			}
			var project = EngineProject.New();
			try
			{
				project.SetCore(coreName, version, "");
				foreach (var (slot, name, path) in files) project.FileAdd(name, slot, path);
				if (settingsJson is { Length: not 0 }) project.SetSettingsJson(settingsJson);
				return project;
			}
			catch
			{
				project.Dispose();
				throw;
			}
		}

		/// <summary>
		/// The same from the document the New Project window writes for a helper
		/// process: <c>{"slots": {slot: [names]}, "files": {name: path},
		/// "settings": {...}}</c>.
		/// </summary>
		public static EngineProject ProjectFor(string packagePath, JObject doc)
		{
			var paths = doc["files"] as JObject;
			var files = new List<(string Slot, string Name, string Path)>();
			if (doc["slots"] is JObject slots)
			{
				foreach (var slot in slots.Properties())
				{
					if (slot.Value is not JArray names) continue;
					foreach (var name in names)
					{
						var file = (string?)name ?? "";
						if (paths?[file] is JValue { Type: JTokenType.String } path) files.Add((slot.Name, file, (string)path!));
					}
				}
			}
			var settings = doc["settings"] is JObject chosen ? chosen.ToString(Newtonsoft.Json.Formatting.None) : null;
			return ProjectFor(packagePath, files, settings);
		}
	}
}
