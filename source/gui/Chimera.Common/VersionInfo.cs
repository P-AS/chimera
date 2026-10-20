using System.IO;

using Chimera.Common.StringExtensions;

namespace Chimera.Common
{
	public static partial class VersionInfo
	{
		public static readonly string HomePage = "https://github.com/ToolAssisted-run/chimera";

		public static readonly string? CustomBuildString;

		public static readonly string UserAgentEscaped;

		static VersionInfo()
		{
			var path = Path.Combine(
				AppContext.BaseDirectory.RemoveSuffix(Path.DirectorySeparatorChar),
				"dll",
				"custombuild.txt"
			);
			if (File.Exists(path))
			{
				var lines = File.ReadAllLines(path);
				if (lines.Length > 0)
				{
					CustomBuildString = lines[0];
				}
			}
			UserAgentEscaped = $"{
				(string.IsNullOrWhiteSpace(CustomBuildString) ? "Chimera" : CustomBuildString!.OnlyAlphanumeric())
			}/{GIT_SHORTHASH}";
		}

		/// <summary>The project's site.</summary>
		public static readonly string SiteURI = "https://toolassisted.run";

		/// <summary>The commit this build was made from, on the web.</summary>
		public static readonly string CommitURI = $"https://github.com/ToolAssisted-run/chimera/commit/{GIT_HASH}";

		/// <summary>
		/// What this build is called wherever it is named - the About box, a bug
		/// report: its commit, and when that commit was made, on the
		/// <see cref="EuropeanClock"/> every date Chimera shows is written in (issue 331).
		/// </summary>
		public static string GetBuildName()
			=> BuildName(GIT_SHORTHASH, GIT_COMMITTIME);

		/// <summary><see cref="GetBuildName"/> for a commit and its time as the build stamps it (UTC).</summary>
		public static string BuildName(string shortHash, string commitTimeUtc)
			=> DateTimeOffset.TryParseExact(commitTimeUtc, "yyyy-MM-dd HH:mm", System.Globalization.CultureInfo.InvariantCulture,
				System.Globalization.DateTimeStyles.AssumeUniversal, out var when)
				? $"Commit {shortHash} ({EuropeanClock.Format(when)})"
				: $"Commit {shortHash} ({commitTimeUtc})"; // a day alone, from a build that could not ask git the time

		/// <summary>
		/// Chimera has no versions: a build is identified by its commit and that
		/// commit's date (never a build wall-clock, which would break reproducible
		/// builds).
		/// </summary>
		public static string GetEmuVersion()
			=> $"Commit {GIT_SHORTHASH}";

	}
}
