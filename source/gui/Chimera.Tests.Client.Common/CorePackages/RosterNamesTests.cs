#nullable enable

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

using Chimera.Client.Common;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.Common.CorePackages
{
	/// <summary>
	/// <c>official-cores.json</c>, the list of the cores this project publishes.
	///
	/// Chimera does not read it and does not ship it: the frontend downloads
	/// nothing and lists only the packages in its cores folder (user-decided,
	/// 2026-10-07). The file stays in the repository as what CI fetches the
	/// published packages by (tools/fetch-cores.sh) and what the documentation
	/// lists, so what is checked here is the file: every system it names has a
	/// name (#172 - a list once read "WS, WSC, ZXS, MYV, CV ..."), and it still
	/// says what the packages say.
	/// </summary>
	[TestClass]
	public class RosterNamesTests
	{
		private sealed record Row(string Id, string Name, IReadOnlyList<(string Id, string Name)> Systems);

		private static IReadOnlyList<Row> Roster()
		{
			const string file = "official-cores.json";
			var dir = new DirectoryInfo(AppDomain.CurrentDomain.BaseDirectory);
			while (dir is not null && !File.Exists(Path.Combine(dir.FullName, file))) dir = dir.Parent;
			if (dir is null) Assert.Inconclusive($"{file} is not above the test binaries");
			var root = Newtonsoft.Json.Linq.JObject.Parse(File.ReadAllText(Path.Combine(dir!.FullName, file)));
			return (root["cores"] as Newtonsoft.Json.Linq.JArray ?? new())
				.OfType<Newtonsoft.Json.Linq.JObject>()
				.Select(static core => new Row(
					(string?) core["id"] ?? "",
					(string?) core["name"] ?? "",
					(core["systems"] as Newtonsoft.Json.Linq.JArray ?? new())
						// a row may give a bare id (format 1) or an { id, name } pair
						.Select(static s => s is Newtonsoft.Json.Linq.JObject o
							? ((string?) o["id"] ?? "", (string?) o["name"] ?? "")
							: ((string?) s ?? "", ""))
						.ToList()))
				.ToList();
		}

		[TestMethod]
		public void TheRosterListsCores()
			=> Assert.IsTrue(Roster().Count > 0 && Roster().All(static r => r.Id.Length is not 0 && r.Name.Length is not 0), "every row has an id and a name");

		[TestMethod]
		public void EveryRosterSystemHasAName()
		{
			var unnamed = Roster()
				.SelectMany(static core => core.Systems.Select(system => (Core: core.Id, System: system)))
				.Where(static x => string.IsNullOrWhiteSpace(x.System.Name) || string.IsNullOrWhiteSpace(x.System.Id))
				.Select(static x => $"{x.Core}: {x.System.Id}")
				.ToList();
			Assert.AreEqual(0, unnamed.Count, $"no name for: {string.Join(", ", unnamed)}");
		}

		/// <summary>
		/// Two cores that run the same system say the same name for it. Nothing
		/// forces them to - each core speaks for itself - but the roster is one
		/// list read by one person, and "Nintendo Entertainment System" beside
		/// "Famicom / NES" for the same id reads as two machines.
		/// </summary>
		[TestMethod]
		public void OneSystemHasOneNameAcrossTheRoster()
		{
			var split = Roster()
				.SelectMany(static core => core.Systems.Select(system => (Core: core.Id, system.Id, system.Name)))
				.GroupBy(static x => x.Id)
				.Where(static g => g.Select(static x => x.Name).Distinct().Count() > 1)
				.Select(static g => $"{g.Key}: {string.Join(" / ", g.Select(static x => $"\"{x.Name}\" ({x.Core})"))}")
				.ToList();
			Assert.AreEqual(0, split.Count, string.Join("; ", split));
		}

		/// <summary>
		/// A roster row's names are a copy of what the core's package says, made
		/// when the core joined. Where a package of that core is here to ask
		/// (build/Cores; in CI, every core's newest), the copy must still agree:
		/// a row that drifts names a machine the core no longer calls that.
		/// </summary>
		[TestMethod]
		public void TheRosterSaysWhatThePackagesSay()
		{
			if (InstalledPackages.Files.Count is 0) Assert.Inconclusive("no core packages in build/Cores (see tools/fetch-cores.sh)");
			var roster = Roster();
			List<string> drift = new();
			var asked = 0;
			foreach (var package in InstalledPackages.Files)
			{
				var text = InstalledPackages.ConfigOf(package);
				var cfg = text is null ? null : WaterboxConfig.FromJson(text);
				// a package from before cores named their systems has nothing to compare
				if (cfg?.SystemNames is not { Count: > 0 } declared) continue;
				var row = roster.FirstOrDefault(c => string.Equals(c.Name, cfg.CoreName, StringComparison.Ordinal));
				if (row is null) continue;
				foreach (var system in row.Systems)
				{
					if (!declared.TryGetValue(system.Id, out var said)) continue;
					asked++;
					if (said != system.Name) drift.Add($"{row.Id} {system.Id}: the roster says \"{system.Name}\", {InstalledPackages.NameOf(package)} says \"{said}\"");
				}
			}
			if (asked is 0) Assert.Inconclusive("no installed package names its systems yet");
			Assert.AreEqual(0, drift.Count, string.Join("; ", drift));
		}
	}
}
