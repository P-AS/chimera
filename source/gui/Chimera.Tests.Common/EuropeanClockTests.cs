using Chimera.Common;

namespace Chimera.Tests.Common;

/// <summary>
/// Issue 331: a build's and a core version's date are written in Central European time, with
/// the zone, the same on every computer. Pinned here: the two clocks and the hours they change.
/// </summary>
[TestClass]
public class EuropeanClockTests
{
	private static string At(int year, int month, int day, int hour, int minute)
		=> EuropeanClock.Format(new DateTimeOffset(year, month, day, hour, minute, 0, TimeSpan.Zero));

	[TestMethod]
	public void SummerIsTwoHoursAheadOfUtc()
		=> Assert.AreEqual("2026-10-18 18:35 CEST", At(2026, 10, 18, 16, 35)); // the xemu commit of issue 331

	[TestMethod]
	public void WinterIsOneHourAhead()
		=> Assert.AreEqual("2026-01-15 13:00 CET", At(2026, 1, 15, 12, 0));

	[TestMethod]
	public void TheClockChangesAtOneUtcOnTheLastSundays()
	{
		// 2026: summer time from Sunday 29 March to Sunday 25 October
		Assert.AreEqual("2026-03-29 01:59 CET", At(2026, 3, 29, 0, 59));
		Assert.AreEqual("2026-03-29 03:00 CEST", At(2026, 3, 29, 1, 0));
		Assert.AreEqual("2026-10-25 02:59 CEST", At(2026, 10, 25, 0, 59));
		Assert.AreEqual("2026-10-25 02:00 CET", At(2026, 10, 25, 1, 0));
	}

	[TestMethod]
	public void TheDayCanTurnOver()
		=> Assert.AreEqual("2026-12-01 00:30 CET", At(2026, 11, 30, 23, 30));

	[TestMethod]
	public void AnOffsetIsReadAsTheMomentItNames()
		=> Assert.AreEqual("2026-10-18 18:35 CEST",
			EuropeanClock.Format(new DateTimeOffset(2026, 10, 18, 12, 35, 0, TimeSpan.FromHours(-4))));
}

/// <summary>Issue 331: the build's name gives its commit's time on the same clock as the cores.</summary>
[TestClass]
public class BuildNameTests
{
	[TestMethod]
	public void TheCommitTimeIsWrittenInCentralEuropeanTime()
		=> Assert.AreEqual("Commit 9dbd35bac (2026-10-20 10:58 CEST)", VersionInfo.BuildName("9dbd35bac", "2026-10-20 08:58"));

	[TestMethod]
	public void ADayAloneIsKeptAsItIs()
		=> Assert.AreEqual("Commit 9dbd35bac (2026-10-20)", VersionInfo.BuildName("9dbd35bac", "2026-10-20"));

	[TestMethod]
	public void ThisBuildIsNamedThatWay()
		=> StringAssert.Matches(VersionInfo.GetBuildName(), new System.Text.RegularExpressions.Regex(@"^Commit [0-9a-f]{9} \(\d{4}-\d\d-\d\d \d\d:\d\d CES?T\)$"));
}
