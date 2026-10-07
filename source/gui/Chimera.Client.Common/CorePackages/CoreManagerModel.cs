#nullable enable

using System;
using System.Collections.Generic;
using System.Linq;

namespace Chimera.Client.Common
{
	/// <summary>One core as the manager sees it: the packages of it that are in the cores folder.</summary>
	public sealed class CoreManagerRow
	{
		/// <summary>Every version of this core that is here, newest first. Never empty: a row is a core that is here.</summary>
		public IReadOnlyList<DiscoveredCorePackage> Installed { get; init; } = [ ];

		public string Name => Installed.FirstOrDefault()?.Name ?? "";

		public IReadOnlyList<string> Systems => Installed.FirstOrDefault()?.Systems ?? [ ];

		/// <summary>
		/// What to call one of this core's systems: what a package of it says, else
		/// the id. Nothing here knows a machine: the words are the core's own.
		/// </summary>
		public string SystemNameOf(string systemId)
		{
			foreach (var package in Installed)
			{
				var named = package.SystemNameOf(systemId);
				if (named != systemId) return named;
			}
			return systemId;
		}

		/// <summary>Every system the core runs, spelled out, in order.</summary>
		public string SystemsSpelled => string.Join(", ", Systems.Select(SystemNameOf));

		public bool IsInstalled => Installed.Count is not 0;

		/// <summary>True for a game core (docs/game-cores.md), listed after every emulator; the package says so.</summary>
		public bool IsGameCore => Installed.Any(static p => p.IsGameCore);

		/// <summary>Every version's file, for removing the core entire.</summary>
		public IReadOnlyList<string> InstalledPaths => Installed.Select(static p => p.Path).ToList();

		/// <summary>When the newest version here was made, if its package says.</summary>
		public DateTimeOffset? BuiltAt => Installed.Select(CoreVersionDates.Of).FirstOrDefault(static d => d is not null);

		/// <summary>How big the newest version is on disk, in bytes; 0 when it cannot be measured.</summary>
		public long SizeBytes => IsInstalled ? FileSize(Installed[0].Path) : 0;

		private static long FileSize(string path)
		{
			try
			{
				System.IO.FileInfo info = new(path);
				return info.Exists ? info.Length : 0;
			}
			catch (Exception)
			{
				return 0; // a size is a nicety; nothing here is worth an exception
			}
		}
	}

	/// <summary>
	/// What File &gt; Core Manager shows: the core packages that are in the cores
	/// folder, one row per core. Chimera downloads nothing and knows of no core it
	/// has not been given (user-decided, 2026-10-07), so the list is the folder's
	/// contents and nothing more. Kept out of the form so what somebody is told
	/// can be tested without a window.
	/// </summary>
	public static class CoreManagerModel
	{
		/// <summary>
		/// One row per core name - several versions of one core are one row - the
		/// emulators first, then the game cores, each by name. A package that could
		/// not be read is left out: it is nobody's core, and discovery already says
		/// what is wrong with it where packages are opened.
		/// </summary>
		public static IReadOnlyList<CoreManagerRow> Build(IEnumerable<DiscoveredCorePackage> discovered)
			=> discovered
				.Where(static p => p.Error is null)
				.GroupBy(static p => p.Name, StringComparer.OrdinalIgnoreCase)
				.Select(static group => new CoreManagerRow { Installed = CoreVersionDates.NewestFirst(Newest(group.ToList())) })
				.OrderBy(static r => r.IsGameCore ? 1 : 0)
				.ThenBy(static r => r.Name, StringComparer.OrdinalIgnoreCase)
				.ToList();

		/// <summary>
		/// Versions newest first where no package says when it was made: by the
		/// file's time, then the version string. <see cref="CoreVersionDates.NewestFirst(IEnumerable{DiscoveredCorePackage})"/>
		/// then puts the dated ones ahead in date order and leaves the rest as they are.
		/// </summary>
		private static List<DiscoveredCorePackage> Newest(List<DiscoveredCorePackage> packages)
			=> packages
				.OrderByDescending(static p => WrittenAt(p.Path))
				.ThenByDescending(static p => p.Version, StringComparer.OrdinalIgnoreCase)
				.ToList();

		private static DateTime WrittenAt(string path)
		{
			try
			{
				return System.IO.File.Exists(path) ? System.IO.File.GetLastWriteTimeUtc(path) : DateTime.MinValue;
			}
			catch (Exception)
			{
				return DateTime.MinValue;
			}
		}
	}
}
