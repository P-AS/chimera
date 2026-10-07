#nullable enable

using System;
using System.IO;
using System.Linq;

using Chimera.Common;
using Chimera.Common.PathExtensions;

namespace Chimera.Client.Common
{
	/// <summary>
	/// Where core packages are looked for: ONE folder.
	///
	/// Chimera downloads nothing (user-decided, 2026-10-07; docs/core-manager.md).
	/// A core is a file somebody downloaded from its project or built, and put
	/// here. The folder is <c>Cores/</c> beside the executable - it comes with the
	/// bundle, empty - unless the user names another (<see cref="Config.CoresFolder"/>,
	/// set from File &gt; Core Manager), which is how one set of cores is kept across
	/// any number of unpacked Chimeras.
	/// </summary>
	public static class CoresFolder
	{
		/// <summary><c>Cores/</c> beside the executable.</summary>
		public static string Default
			=> Path.Combine(PathUtils.ExeDirectoryPath, CorePackageDiscovery.DefaultDirName);

		/// <summary>The folder this configuration designates.</summary>
		public static string For(Config config) => Resolve(config.CoresFolder);

		/// <summary>
		/// A configured folder as a path: blank is <see cref="Default"/>, a relative
		/// one is relative to the executable (so a portable install can say
		/// <c>../Cores</c>), and one that is no path at all is the default too -
		/// a setting somebody mistyped must not leave them with no cores folder.
		/// </summary>
		public static string Resolve(string? configured)
		{
			if (string.IsNullOrWhiteSpace(configured)) return Default;
			try
			{
				return Path.GetFullPath(Path.Combine(PathUtils.ExeDirectoryPath, configured!.Trim()));
			}
			catch (Exception)
			{
				return Default;
			}
		}

		/// <summary>
		/// What to keep in the configuration for a folder somebody chose: nothing
		/// when it is the default, so a bundle that is moved keeps finding the
		/// <c>Cores/</c> that moved with it.
		/// </summary>
		public static string ToConfigure(string chosen)
			=> Same(chosen, Default) ? "" : chosen;

		/// <summary>Whether <paramref name="path"/> is a file directly in <paramref name="folder"/>: the only files the manager removes.</summary>
		public static bool Holds(string folder, string path)
		{
			try
			{
				return Path.GetDirectoryName(Path.GetFullPath(path)) is { } dir && Same(dir, folder);
			}
			catch (Exception)
			{
				return false;
			}
		}

		/// <summary>
		/// Where versions of Chimera that downloaded cores put them: <c>Cores</c> in
		/// the data directory. It is NOT searched any more. It is named here so the
		/// manager can tell somebody who has cores there that they are there, and
		/// offer to use that folder - the alternative being a Chimera that opens
		/// after an update with every core apparently gone.
		/// </summary>
		public static string Former
			=> Path.Combine(ProjectCache.DataHome, CorePackageDiscovery.DefaultDirName);

		/// <summary>How many packages <see cref="Former"/> holds, when it is not the folder in use; 0 otherwise.</summary>
		public static int LeftInFormer(string current)
		{
			try
			{
				return !Same(Former, current) && Directory.Exists(Former)
					? Directory.EnumerateFiles(Former, "*" + CorePackageDiscovery.Extension).Count()
					: 0;
			}
			catch (Exception)
			{
				return 0;
			}
		}

		private static bool Same(string a, string b)
		{
			try
			{
				static string Trimmed(string p) => Path.GetFullPath(p).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
				return string.Equals(Trimmed(a), Trimmed(b), OSTailoredCode.IsUnixHost ? StringComparison.Ordinal : StringComparison.OrdinalIgnoreCase);
			}
			catch (Exception)
			{
				return false;
			}
		}
	}
}
