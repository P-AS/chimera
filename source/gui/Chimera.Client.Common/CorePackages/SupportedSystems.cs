#nullable enable

using System;
using System.Collections.Generic;
using System.Linq;

namespace Chimera.Client.Common
{
	/// <summary>
	/// Every system the listed cores run, once each, with the cores that run it
	/// (#172): the Core Manager lists cores, and a person looking for a machine
	/// had to read every core's line to find it - ares alone runs twenty-five.
	/// </summary>
	public static class SupportedSystems
	{
		public sealed class Entry
		{
			public string Id { get; init; } = "";

			/// <summary>The full name, as <see cref="SystemNames"/> spells it.</summary>
			public string Name { get; init; } = "";

			/// <summary>The cores that run it, by name, installed or not.</summary>
			public IReadOnlyList<string> Cores { get; init; } = [ ];

			/// <summary>Whether one of those cores is installed.</summary>
			public bool Installed { get; init; }
		}

		/// <summary>The systems of these cores, sorted by name.</summary>
		public static IReadOnlyList<Entry> From(IEnumerable<CoreManagerRow> rows)
			=> From(rows.Select(static r => (r.Name, r.Systems, r.IsInstalled)));

		public static IReadOnlyList<Entry> From(IEnumerable<(string Core, IReadOnlyList<string> Systems, bool Installed)> cores)
		{
			Dictionary<string, (SortedSet<string> Cores, bool Installed)> by = new(StringComparer.OrdinalIgnoreCase);
			foreach (var (core, systems, installed) in cores)
			{
				foreach (var id in systems.Where(static s => !string.IsNullOrWhiteSpace(s)))
				{
					if (!by.TryGetValue(id, out var entry)) entry = (new SortedSet<string>(StringComparer.OrdinalIgnoreCase), false);
					entry.Cores.Add(core);
					by[id] = (entry.Cores, entry.Installed || installed);
				}
			}
			return by
				.Select(static pair => new Entry
				{
					Id = pair.Key,
					Name = SystemNames.Of(pair.Key),
					Cores = pair.Value.Cores.ToList(),
					Installed = pair.Value.Installed,
				})
				.OrderBy(static e => e.Name, StringComparer.OrdinalIgnoreCase)
				.ThenBy(static e => e.Id, StringComparer.OrdinalIgnoreCase)
				.ToList();
		}

		/// <summary>Whether an entry answers a filter: its name, its id or one of its cores holds the text.</summary>
		public static bool Matches(Entry entry, string? filter)
		{
			if (string.IsNullOrWhiteSpace(filter)) return true;
			var text = filter!.Trim();
			return entry.Name.IndexOf(text, StringComparison.OrdinalIgnoreCase) >= 0
				|| entry.Id.IndexOf(text, StringComparison.OrdinalIgnoreCase) >= 0
				|| entry.Cores.Any(c => c.IndexOf(text, StringComparison.OrdinalIgnoreCase) >= 0);
		}
	}
}
