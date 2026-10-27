#nullable enable

using System;
using System.Collections.Generic;
using System.Linq;

namespace Chimera.Client.Common
{
	/// <summary>
	/// Which build of a core runs, when several builds of it are installed. A system's core is not
	/// chosen here: a game runs only in a project, and the project names its core (user-decided,
	/// 2026-10-26). Startup discovery deliberately does not write here, so what happens to be
	/// sitting in <c>Cores/</c> cannot silently reassign what someone chose.
	/// </summary>
	public static class CoreChoices
	{
		/// <summary>
		/// Makes the package <paramref name="sha1"/> the build of <paramref name="coreName"/> that new
		/// projects use when several builds of that core are installed. Returns false if it already was.
		/// </summary>
		public static bool MakeDefaultBuild(Config config, string coreName, string sha1)
		{
			if (config.DefaultCoreBuilds.TryGetValue(coreName, out var existing)
				&& string.Equals(existing, sha1, StringComparison.OrdinalIgnoreCase))
			{
				return false;
			}
			config.DefaultCoreBuilds[coreName] = sha1;
			return true;
		}

		/// <summary>
		/// Which of several builds of one core to run (issue #63). The build a project pins, whenever it
		/// is among them - a project is its machine, and another build is another machine. Otherwise the
		/// build the user chose (File &gt; Open Core, or installing it in the core manager). Otherwise the
		/// most recently installed. Null only when there is no build at all.
		/// </summary>
		public static T? PickBuild<T>(IEnumerable<T> builds, Func<T, string?> sha1Of, Func<T, DateTime> installedAt,
			string? pinnedSha1, string? chosenSha1)
			where T : class
		{
			var list = builds.ToList();
			if (list.Count is 0) return null;
			bool Is(T build, string? sha1)
				=> !string.IsNullOrEmpty(sha1) && string.Equals(sha1Of(build), sha1, StringComparison.OrdinalIgnoreCase);
			return list.FirstOrDefault(b => Is(b, pinnedSha1))
				?? list.FirstOrDefault(b => Is(b, chosenSha1))
				?? list.OrderByDescending(installedAt).First();
		}
	}
}
