using System.Linq;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common.CorePackages
{
	/// <summary>
	/// Issue #67: a commit says which version a package is and nothing about which of two is
	/// newer. Pinned here: where the date comes from, how it is written, and the order versions
	/// are offered in.
	/// </summary>
	[TestClass]
	public class CoreVersionDatesTests
	{
		private static DiscoveredCorePackage Package(string name, string version, string? date = null, string path = "")
			=> new() { Name = name, Version = version, VersionDate = CoreVersionDates.Parse(date), Path = path, Sha1 = version };

		[TestMethod]
		public void ThePackagesOwnDateIsTheDate()
		{
			var package = Package("RPCS3", "1c4a0c476ea8", "2026-09-17T08:30:00Z");
			Assert.AreEqual(new DateTimeOffset(2026, 9, 17, 8, 30, 0, TimeSpan.Zero), CoreVersionDates.Of(package));
			StringAssert.EndsWith(package.DatedVersion, "  (1c4a0c47)");
			StringAssert.StartsWith(package.DatedVersion, "2026-09-1"); // the day, in local time
		}

		[TestMethod]
		public void TwoVersionsOfOneDayReadApart()
		{
			// the minute is written, in local time: a core often has several versions a day,
			// and the day alone made them look alike
			var morning = Package("SDLPoP2", "cd812c60fbe4", "2026-09-29T09:05:00Z");
			var evening = Package("SDLPoP2", "0fcd225aa792", "2026-09-29T17:40:00Z");
			Assert.AreNotEqual(morning.DatedVersion.Split(' ')[1], evening.DatedVersion.Split(' ')[1]);
			var local = new DateTimeOffset(2026, 9, 29, 9, 5, 0, TimeSpan.Zero).ToLocalTime();
			StringAssert.StartsWith(morning.DatedVersion, local.ToString("yyyy-MM-dd HH:mm", System.Globalization.CultureInfo.InvariantCulture));
			Assert.AreEqual("0fcd225a", CoreVersionDates.NewestFirst([ morning, evening ])[0].ShortVersion.Substring(0, 8),
				"the newest first, by the full timestamp");
		}

		[TestMethod]
		public void APackageThatStatesNoDateHasNone()
		{
			// nothing else is asked: there is no record of what cores published to look it up
			// in, because Chimera reaches for nothing over the network (user-decided, 2026-10-07)
			var undated = Package("RPCS3", "1c4a0c476ea8");
			Assert.IsNull(CoreVersionDates.Of(undated));
			Assert.AreEqual("1c4a0c47", undated.DatedVersion, "listed by its commit alone, not with a guess");
			Assert.IsNull(CoreVersionDates.Parse(null));
			Assert.IsNull(CoreVersionDates.Parse("last tuesday-ish"));
		}

		[TestMethod]
		public void VersionsOfOneCoreAreOfferedNewestFirstAndCoresStayWhereTheyWere()
		{
			var packages = new[]
			{
				Package("DOSBox-X", "aaaaaaaa", "2026-09-01T00:00:00Z"),
				Package("RPCS3", "bbbbbbbb", "2026-09-10T00:00:00Z"),
				Package("DOSBox-X", "cccccccc"),                          // undated: after the dated ones
				Package("RPCS3", "dddddddd", "2026-09-17T00:00:00Z"),
				Package("DOSBox-X", "eeeeeeee", "2026-09-12T00:00:00Z"),
				Package("DOSBox-X", "ffffffff"),                          // undated: keeps its place after cccccccc
			};
			var ordered = CoreVersionDates.NewestFirst(packages, static p => p.VersionDate).Select(static p => p.Version).ToArray();
			CollectionAssert.AreEqual(
				new[] { "eeeeeeee", "aaaaaaaa", "cccccccc", "ffffffff", "dddddddd", "bbbbbbbb" },
				ordered);
		}
	}
}
