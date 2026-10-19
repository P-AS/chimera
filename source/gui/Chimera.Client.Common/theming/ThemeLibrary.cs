#nullable enable

using System.Collections.Generic;
using System.IO;
using System.Linq;

using Chimera.Common;
using Chimera.Common.PathExtensions;

namespace Chimera.Client.Common
{
	/// <summary>A theme file that would not load, and why - kept so the frontend can say so once, in one place.</summary>
	public sealed record ThemeLoadFailure(string Path, string Message);

	/// <summary>
	/// The themes this build can offer, and which one is on.
	///
	/// Two are compiled in and always there; the rest are files in the themes
	/// folder (Config &gt; Paths, Themes), which is the Themes folder beside
	/// Chimera.exe unless somebody chose another, and in the folder themes were
	/// kept in before that one existed. A file that will not parse is left out and
	/// its reason kept in <see cref="Failures"/> - it is not applied half-way and
	/// it does not stop the frontend from starting.
	/// </summary>
	public static class ThemeLibrary
	{
		/// <summary>
		/// What a config that has never chosen a theme starts with (user-decided,
		/// 2026-09-20). A config that HAS been written before is left alone - see
		/// <see cref="Config.ResolveTheme"/> - because changing somebody's colours
		/// under them on an update is a surprise, not a feature.
		/// </summary>
		public const string NewConfigThemeName = "Dark";

		/// <summary>
		/// What anything falls back to when the theme it wanted is not there: a
		/// theme file somebody deleted, a name nothing recognises, a built-in that
		/// would not parse. Light, because it is always present and is exactly the
		/// desktop's own colours, so it cannot itself be the thing that is wrong.
		/// </summary>
		public const string FallbackThemeName = "Light";

		private static readonly Lock Sync = new();

		private static List<Theme> _themes = new();

		private static List<ThemeLoadFailure> _failures = new();

		private static Theme? _current;

		private static string _directory = "";

		/// <summary>The theme everything paints with. Never null once anything has asked for it.</summary>
		public static Theme Current
		{
			get
			{
				lock (Sync)
				{
					if (_current is null)
					{
						EnsureLoadedLocked();
						_current = FindLocked(FallbackThemeName) ?? _themes[0];
					}
					return _current;
				}
			}
		}

		/// <summary>Raised after <see cref="Current"/> changes, so open windows can repaint.</summary>
		public static event Action<Theme>? Changed;

		/// <summary>Every theme on offer, built-ins first, then files, each named once.</summary>
		public static IReadOnlyList<Theme> All
		{
			get { lock (Sync) { EnsureLoadedLocked(); return _themes.ToList(); } }
		}

		/// <summary>The theme files that would not load, and the reason for each.</summary>
		public static IReadOnlyList<ThemeLoadFailure> Failures
		{
			get { lock (Sync) { EnsureLoadedLocked(); return _failures.ToList(); } }
		}

		/// <summary>
		/// Where a person puts a theme they wrote or was given, and where the themes
		/// Chimera ships with are: the Themes path in Config &gt; Paths, set at
		/// startup by <see cref="UseDirectory"/>. Before that, and in a program that
		/// never sets it, the Themes folder beside Chimera.exe.
		/// </summary>
		public static string ThemesDirectory
		{
			get { lock (Sync) return _directory.Length is 0 ? DefaultThemesDirectory : _directory; }
		}

		/// <summary>The folder the bundle ships its themes in, beside Chimera.exe.</summary>
		public static string DefaultThemesDirectory => Path.Combine(PathUtils.ExeDirectoryPath, "Themes");

		/// <summary>
		/// Where themes were kept before there was a folder beside Chimera.exe: under
		/// the data directory. Still read, after <see cref="ThemesDirectory"/>, so a
		/// theme somebody wrote there is not lost; nothing is written there any more.
		/// </summary>
		public static string FormerThemesDirectory => Path.Combine(ProjectCache.DataHome, "Themes");

		/// <summary>
		/// Reads themes from another folder from now on. If the themes were already
		/// read and the folder is a different one, they are read again, keeping the
		/// current theme when the new folder has it too. Says whether they were.
		/// </summary>
		public static bool UseDirectory(string? directory)
		{
			bool reread;
			lock (Sync)
			{
				var before = _directory.Length is 0 ? DefaultThemesDirectory : _directory;
				_directory = directory?.Trim() ?? "";
				var after = _directory.Length is 0 ? DefaultThemesDirectory : _directory;
				reread = _themes.Count is not 0 && !SameDirectory(before, after);
			}
			if (reread) Reload();
			return reread;
		}

		/// <summary>Whether a theme is one of the two compiled into Chimera rather than a file.</summary>
		public static bool IsBuiltIn(Theme theme) => theme.Origin.StartsWith(BuiltInOrigin, StringComparison.Ordinal);

		private const string BuiltInOrigin = "built-in theme ";

		public static Theme? Find(string? name)
		{
			if (string.IsNullOrWhiteSpace(name)) return null;
			lock (Sync) { EnsureLoadedLocked(); return FindLocked(name!); }
		}

		/// <summary>
		/// Turns a theme on. An unknown name falls back to <see cref="FallbackThemeName"/>,
		/// which is what happens when a config names a theme file that has since been
		/// deleted. Returns the theme that is now on.
		/// </summary>
		public static Theme Select(string? name)
		{
			Theme chosen;
			lock (Sync)
			{
				EnsureLoadedLocked();
				chosen = FindLocked(name ?? "") ?? FindLocked(FallbackThemeName) ?? _themes[0];
				if (ReferenceEquals(chosen, _current)) return chosen;
				_current = chosen;
			}
			Changed?.Invoke(chosen);
			return chosen;
		}

		/// <summary>Re-reads the themes folder, keeping the current theme if it is still there.</summary>
		public static void Reload()
		{
			string? wanted;
			lock (Sync)
			{
				wanted = _current?.Name;
				_themes = new();
				_failures = new();
				_current = null;
				EnsureLoadedLocked();
			}
			Select(wanted);
		}

		/// <summary>
		/// The built-ins plus whatever is in a folder, as a pair of lists, without
		/// touching the theme that is on. This is what the loading tests use: the
		/// library itself is one static thing per process, and a test that swapped
		/// it out would be answering questions another test had asked.
		/// </summary>
		public static (IReadOnlyList<Theme> Themes, IReadOnlyList<ThemeLoadFailure> Failures) Read(string? directory)
		{
			List<Theme> themes = new();
			List<ThemeLoadFailure> failures = new();
			LoadBuiltIns(themes);
			LoadDirectory(directory, themes, failures);
			return (themes, failures);
		}

		/// <summary>Loads the built-ins and then the folder, and makes that the library.</summary>
		public static void LoadFrom(string? directory)
		{
			lock (Sync)
			{
				(var themes, var failures) = Read(directory);
				_themes = themes.ToList();
				_failures = failures.ToList();
				_current = FindLocked(_current?.Name ?? FallbackThemeName) ?? FindLocked(FallbackThemeName) ?? _themes[0];
			}
			Changed?.Invoke(Current);
		}

		private static void EnsureLoadedLocked()
		{
			if (_themes.Count is not 0) return;
			LoadBuiltIns(_themes);
			var folder = _directory.Length is 0 ? DefaultThemesDirectory : _directory;
			LoadDirectory(folder, _themes, _failures);
			var former = SafeFormerThemesDirectory();
			if (former is not null && !SameDirectory(former, folder)) LoadDirectory(former, _themes, _failures);
		}

		private static string? SafeFormerThemesDirectory()
		{
			try
			{
				return FormerThemesDirectory;
			}
			catch (Exception)
			{
				// no data directory yet (a unit test, a first run): nothing to read there
				return null;
			}
		}

		private static bool SameDirectory(string a, string b)
		{
			try
			{
				a = Path.GetFullPath(a).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
				b = Path.GetFullPath(b).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
			}
			catch (Exception)
			{
				// a path that cannot even be made whole is compared as it was written
			}
			return string.Equals(a, b, OSTailoredCode.IsUnixHost ? StringComparison.Ordinal : StringComparison.OrdinalIgnoreCase);
		}

		private static void LoadBuiltIns(List<Theme> into)
		{
			foreach (var name in new[] { "light", "dark" })
			{
				using var stream = typeof(ThemeLibrary).Assembly.EmbeddedResourceStream($"Chimera.Client.Common.Resources.themes.{name}.json");
				using StreamReader reader = new(stream);
				into.Add(ThemeFile.Parse(reader.ReadToEnd(), $"{BuiltInOrigin}\"{name}\"", n => In(into, n)));
			}
		}

		private static void LoadDirectory(string? directory, List<Theme> into, List<ThemeLoadFailure> failures)
		{
			if (string.IsNullOrWhiteSpace(directory) || !Directory.Exists(directory)) return;
			// sorted so that two files claiming the same name resolve the same way every run
			foreach (var path in Directory.GetFiles(directory!, "*.json").OrderBy(static p => p, StringComparer.Ordinal))
			{
				try
				{
					var theme = ThemeFile.Parse(File.ReadAllText(path), Path.GetFileName(path), n => In(into, n));
					if (In(into, theme.Name) is not null)
					{
						failures.Add(new(path, $"{Path.GetFileName(path)}: there is already a theme called \"{theme.Name}\"; rename it in the file's \"name\""));
						continue;
					}
					into.Add(theme);
				}
				catch (ThemeFormatException ex)
				{
					failures.Add(new(path, ex.Message));
				}
				catch (IOException ex)
				{
					failures.Add(new(path, $"{Path.GetFileName(path)}: {ex.Message}"));
				}
				catch (UnauthorizedAccessException ex)
				{
					failures.Add(new(path, $"{Path.GetFileName(path)}: {ex.Message}"));
				}
			}
		}

		private static Theme? In(IReadOnlyList<Theme> themes, string name)
			=> themes.FirstOrDefault(t => string.Equals(t.Name, name, StringComparison.OrdinalIgnoreCase));

		private static Theme? FindLocked(string name) => In(_themes, name);
	}
}
