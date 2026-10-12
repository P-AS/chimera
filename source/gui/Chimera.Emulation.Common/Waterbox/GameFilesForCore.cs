using System.Collections.Generic;
using System.IO;
using System.Linq;

using Chimera.Emulation.Common.Engine;

using Newtonsoft.Json.Linq;

namespace Chimera.Emulation.Common.Waterbox
{
	/// <summary>
	/// The files a core is shown when it is asked about a game before any
	/// machine exists (its SuggestSettings answer: suggested settings, the
	/// game's own settings, the firmware this game needs).
	///
	/// A game is often more than the file that names it: a disc is a .cue and
	/// the track files it lists, and a project may hold other discs and save
	/// data. A core asked about the .cue alone cannot read the disc - a Saturn
	/// core cannot tell which region's BIOS the disc needs. So the question is
	/// asked with the files a run would have: every file of the project under
	/// its name, the slot map ("slots"), and the track files of every cue.
	/// </summary>
	public static class GameFilesForCore
	{
		/// <summary>A project's files as the engine would mount them: the slot map, and each file that has been found, under its name.</summary>
		public static List<CoreFile> FromProject(EngineProject project)
		{
			var files = new List<CoreFile> { new("slots", System.Text.Encoding.UTF8.GetBytes(project.SlotsJson)) };
			for (var i = 0; i < project.FileCount; i++)
			{
				var path = project.FileSourcePath(i);
				if (path is { Length: > 0 } && File.Exists(path)) files.Add(new(project.FileName(i), path));
			}
			return files;
		}

		/// <summary>
		/// The same from the document the new-project wizard writes for a helper
		/// process: <c>{"slots": {slot: [names]}, "files": {name: path}}</c>.
		/// </summary>
		public static List<CoreFile> FromSlotsDocument(JObject doc)
		{
			var files = new List<CoreFile>
			{
				new("slots", System.Text.Encoding.UTF8.GetBytes((doc["slots"] ?? new JObject()).ToString(Newtonsoft.Json.Formatting.None))),
			};
			if (doc["files"] is JObject listed)
			{
				foreach (var file in listed.Properties())
				{
					if (file.Value.Type is JTokenType.String) files.Add(new(file.Name, file.Value.ToString()));
				}
			}
			return files;
		}

		/// <summary>
		/// Adds the track files of every cue among the game and the files given:
		/// each file a cue names that lies beside it and is not already listed,
		/// under the name the cue gives it. A project lists them itself (they
		/// are its support files); a game opened by itself, or a wizard that has
		/// not made its project yet, does not.
		/// </summary>
		public static List<CoreFile> WithCueTracks(string? gamePath, IReadOnlyList<CoreFile>? files)
		{
			var all = files?.ToList() ?? [ ];
			var cues = all.Where(static f => f.Path is not null).Select(static f => f.Path!).ToList();
			if (gamePath is { Length: > 0 }) cues.Insert(0, gamePath);
			foreach (var cue in cues.Where(static p => p.EndsWith(".cue", System.StringComparison.OrdinalIgnoreCase)).Distinct().ToList())
			{
				byte[] bytes;
				try { bytes = File.ReadAllBytes(cue); }
				catch (IOException) { continue; }
				catch (System.UnauthorizedAccessException) { continue; }
				var folder = Path.GetDirectoryName(Path.GetFullPath(cue)) ?? "";
				foreach (var name in EngineCue.References(bytes))
				{
					if (all.Any(f => f.Name == name)) continue;
					var track = Path.Combine(folder, name);
					if (File.Exists(track)) all.Add(new(name, track));
				}
			}
			return all;
		}
	}
}
