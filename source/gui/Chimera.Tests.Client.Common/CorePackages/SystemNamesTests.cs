using System;
using System.IO;
using System.Linq;

using Chimera.Client.Common;

using Newtonsoft.Json.Linq;

namespace Chimera.Tests.Client.Common.CorePackages
{
	/// <summary>
	/// Every system a roster core runs reads as a name, not an id (#172): the
	/// Core Manager listed ares as "WS, WSC, ZXS, MYV, CV ..." because the
	/// table had never been told what those were.
	/// </summary>
	[TestClass]
	public class SystemNamesTests
	{
		/// <summary>Ids that are already what anybody would call the machine.</summary>
		private static readonly string[] AlreadyNames = [ "Doom", "Flash", "MSX", "MSX2", "Syndicate", "Wii" ];

		private static string? Roster()
		{
			var dir = new DirectoryInfo(AppDomain.CurrentDomain.BaseDirectory);
			while (dir is not null && !File.Exists(Path.Combine(dir.FullName, "official-cores.json"))) dir = dir.Parent;
			return dir is null ? null : Path.Combine(dir.FullName, "official-cores.json");
		}

		[TestMethod]
		public void EveryRosterSystemHasAName()
		{
			var roster = Roster();
			if (roster is null) Assert.Inconclusive("official-cores.json is not above the test binaries");
			var ids = JObject.Parse(File.ReadAllText(roster!))["cores"]!
				.SelectMany(static core => core["systems"]!.Values<string>())
				.Distinct()
				.ToList();
			var unnamed = ids.Where(id => SystemNames.Of(id!) == id && !AlreadyNames.Contains(id)).ToList();
			Assert.AreEqual(0, unnamed.Count, $"no name for: {string.Join(", ", unnamed)}");
		}

		[TestMethod]
		public void AnIdNobodyNamedStillReads() => Assert.AreEqual("XYZ9", SystemNames.Of("XYZ9"));
	}
}
