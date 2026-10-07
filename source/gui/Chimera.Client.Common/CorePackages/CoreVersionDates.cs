#nullable enable

using System;
using System.Collections.Generic;
using System.Linq;

namespace Chimera.Client.Common
{
	/// <summary>
	/// When a core version was made (issue #67). A commit says which version a package IS and
	/// nothing about which of two is newer, so every place that lists a version shows its date too.
	///
	/// The package says (<c>versionDate</c>, stamped by the core's build script), and nothing
	/// else is asked: Chimera reaches for nothing over the network (user-decided, 2026-10-07).
	/// A package from before packages said has no date, and is listed without one rather than
	/// with a guess; the file's own time in particular is when it was copied here, which is not
	/// the question.
	/// </summary>
	public static class CoreVersionDates
	{
		public static DateTimeOffset? Parse(string? text)
			=> DateTimeOffset.TryParse(
				text,
				System.Globalization.CultureInfo.InvariantCulture,
				System.Globalization.DateTimeStyles.AssumeUniversal,
				out var parsed) ? parsed : null;

		/// <summary>
		/// A version's date as every list writes it: the local day and minute. The minute is what
		/// tells apart several versions made on one day.
		/// </summary>
		public static string Format(DateTimeOffset when)
			=> when.ToLocalTime().ToString("yyyy-MM-dd HH:mm", System.Globalization.CultureInfo.InvariantCulture);

		public static DateTimeOffset? Of(DiscoveredCorePackage package) => package.VersionDate;

		/// <summary>
		/// The order versions are offered in: cores where they were, and the versions of one core
		/// newest first - so the first of a core is its latest, which is what a picker opens on and
		/// what "the first that matches" finds. A version with no known date goes after those that
		/// have one, and otherwise nothing moves.
		/// </summary>
		public static IReadOnlyList<DiscoveredCorePackage> NewestFirst(IEnumerable<DiscoveredCorePackage> packages)
			=> NewestFirst(packages, Of);

		public static IReadOnlyList<DiscoveredCorePackage> NewestFirst(
			IEnumerable<DiscoveredCorePackage> packages, Func<DiscoveredCorePackage, DateTimeOffset?> dateOf)
		{
			var list = packages.ToList();
			var firstSeen = list
				.Select(static (p, i) => (p.Name, i))
				.GroupBy(static x => x.Name, StringComparer.OrdinalIgnoreCase)
				.ToDictionary(static g => g.Key, static g => g.Min(static x => x.i), StringComparer.OrdinalIgnoreCase);
			return list
				.OrderBy(p => firstSeen[p.Name]) // stable: equal keys keep the order they came in
				.ThenByDescending(p => dateOf(p) ?? DateTimeOffset.MinValue)
				.ToList();
		}
	}
}
