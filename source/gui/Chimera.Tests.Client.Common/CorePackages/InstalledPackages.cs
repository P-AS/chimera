#nullable enable

using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text.RegularExpressions;

namespace Chimera.Tests.Client.Common.CorePackages
{
	/// <summary>
	/// The core packages this checkout has, for the tests that check the frontend
	/// against real ones.
	///
	/// Chimera ships no cores and no longer carries them as submodules (see
	/// docs/core-manager.md), so these read PACKAGES - whatever is in build/Cores -
	/// rather than sources. In CI that directory is filled by
	/// <c>tools/fetch-cores.sh</c>, which downloads what each core published; on a
	/// developer's machine it is whatever they built or installed.
	///
	/// When it is empty the tests that use it are inconclusive rather than green: a
	/// check that silently passes when its subject is missing is worse than no
	/// check.
	/// </summary>
	public static class InstalledPackages
	{
		/// <summary>
		/// Where to look. <c>CHIMERA_CORES_DIR</c> wins, so a CI job can point the
		/// tests at packages it put somewhere else; otherwise it is build/Cores of the
		/// checkout this assembly was built in, found by walking up for the file only
		/// the repository root has.
		/// </summary>
		public static string? Directory
		{
			get
			{
				var explicitDir = Environment.GetEnvironmentVariable("CHIMERA_CORES_DIR");
				if (!string.IsNullOrWhiteSpace(explicitDir)) return explicitDir;
				var dir = new DirectoryInfo(AppDomain.CurrentDomain.BaseDirectory);
				while (dir is not null && !File.Exists(Path.Combine(dir.FullName, "official-cores.json")))
				{
					dir = dir.Parent;
				}
				return dir is null ? null : Path.Combine(dir.FullName, "build", "Cores");
			}
		}

		/// <summary>Every package file found, path-sorted. Empty when there are none.</summary>
		public static IReadOnlyList<string> Files
		{
			get
			{
				var dir = Directory;
				if (dir is null || !System.IO.Directory.Exists(dir)) return [ ];
				return System.IO.Directory.EnumerateFiles(dir, "*.chimeraCore")
					.OrderBy(static p => p, StringComparer.OrdinalIgnoreCase)
					.ToList();
			}
		}

		/// <summary>
		/// One package a core: its newest. Chimera keeps a core's older versions
		/// beside the newest so that a project opens with the version it was made
		/// with (docs/core-manager.md), and an older version can never be fixed, so
		/// a check of what a core publishes judges the newest. The core is the
		/// file's name without its version ("dolphin-30c1fc02...", "dolphin"); the
		/// newest is the one whose waterbox.config has the latest versionDate, one
		/// with none counting as older than any with one, and the file's time
		/// breaking a tie.
		/// </summary>
		public static IReadOnlyList<string> Newest
			=> Files
				.GroupBy(static p => VersionSuffix.Replace(NameOf(p), ""), StringComparer.OrdinalIgnoreCase)
				.Select(static g => g
					.OrderByDescending(static p => VersionDate(p))
					.ThenByDescending(static p => File.GetLastWriteTimeUtc(p))
					.First())
				.OrderBy(static p => p, StringComparer.OrdinalIgnoreCase)
				.ToList();

		private static readonly Regex VersionSuffix = new("-[0-9a-f]{7,40}$", RegexOptions.IgnoreCase);

		/// <summary>The versionDate a package's waterbox.config says, or empty.</summary>
		private static string VersionDate(string packagePath)
		{
			var config = ConfigOf(packagePath);
			if (config is null) return "";
			var m = Regex.Match(config, "\"versionDate\"\\s*:\\s*\"([^\"]*)\"");
			return m.Success ? m.Groups[1].Value : "";
		}

		/// <summary>The text of one entry of a package, or null if it has none.</summary>
		public static string? EntryText(string packagePath, string entry)
		{
			try
			{
				using var zip = ZipFile.OpenRead(packagePath);
				var found = zip.GetEntry(entry);
				if (found is null) return null;
				using var reader = new StreamReader(found.Open());
				return reader.ReadToEnd();
			}
			catch (Exception)
			{
				return null;
			}
		}

		/// <summary>A package's <c>waterbox.config</c>, or null.</summary>
		public static string? ConfigOf(string packagePath) => EntryText(packagePath, "waterbox.config");

		/// <summary>How a package is named in a failure message.</summary>
		public static string NameOf(string packagePath) => Path.GetFileNameWithoutExtension(packagePath);
	}
}
